// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Ervin Notari Junior

// Direct RTSP player with up to 16 cameras:
//   Tizen Sockets -> RTSP/RTP interleaved -> H.264 ->
//     native mode:   ElementaryMediaStreamSource -> <video id="video">
//                    (TV decoder; only ONE slot at a time, a TV limit);
//     software mode: OpenH264 -> YUV -> WebGL on the <canvas id="canvas">.
//
// Threads:
//   main    - native player (listeners), WebGL and draw loop;
//   network - one pthread per slot: socket, RTSP, depacketizer and
//             AppendPacket() or software decoding.
//
// Events go to the JS via Module.onEvent(slot, kind, json).

#include <emscripten.h>
#include <emscripten/threading.h>
#include <pthread.h>

#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>

#include "events.hpp"
#include "h264.hpp"
#include "h265.hpp"
#include "native_player.hpp"
#include "rtsp_connection.hpp"
#include "rtsp_protocol.hpp"
#include "rtsp_url.hpp"
#include "soft_decoder.hpp"
#include "soft_renderer.hpp"

namespace {

using app::Emit;
using app::Log;
using Clock = std::chrono::steady_clock;

constexpr int kMaxSlots = render::kMaxSlots;

// Above this, decoding fell behind: drop until the next IDR.
constexpr size_t kMaxBacklogBytes = 384 * 1024;

enum class Mode { kNative = 0, kSoftware = 1 };

// Error that reconnecting does not fix (e.g. unsupported codec): the JS
// shows the message translated from the code and does not retry.
void Fatal(int slot, const std::string& code, const std::string& detail = "") {
  app::LogError(slot, "fatal", code, detail);
}

void Fail(int slot, const app::Error& err) { app::LogError(slot, "rtsp", err.code, err.detail); }

struct Slot {
  std::atomic<bool> running{false};
  std::atomic<bool> stop{false};
  std::atomic<app::NativePlayer*> native{nullptr};
};

Slot g_slots[kMaxSlots];
std::atomic<int> g_native_slot{-1};  // slot using the native player

struct NativeRequest {
  int slot;
  media::VideoInfo info;
  double fps;
};

void CreateNativeOnMain(int arg) {
  std::unique_ptr<NativeRequest> req(reinterpret_cast<NativeRequest*>(arg));
  auto* player = new app::NativePlayer(req->slot, req->info, req->fps);
  g_slots[req->slot].native = player;
  player->Start();
}

void FinishSlotOnMain(int slot) {
  delete g_slots[slot].native.exchange(nullptr);
  render::ClearSlot(slot);
  int expected = slot;
  g_native_slot.compare_exchange_strong(expected, -1);
  g_slots[slot].running = false;
  EM_ASM({ if (Module.onStopped) Module.onStopped($0); }, slot);
}

struct SessionRequest {
  int slot;
  rtsp::RtspUrl url;
  Mode mode;
};

struct Stats {
  uint64_t bytes = 0;
  uint32_t frames = 0;   // frames delivered (native) or drawn (software)
  uint32_t dropped = 0;
  double decode_ms = 0;  // sum over the period
  uint32_t decoded = 0;
};

void RunSession(const SessionRequest& req) {
  const int slot = req.slot;
  Slot& state = g_slots[slot];
  const rtsp::RtspUrl& url = req.url;
  rtsp::Connection conn(url, state.stop);
  app::Error err;
  rtsp::Response resp;

  Log(slot, "rtsp", true, "connecting to " + url.host + ":" + std::to_string(url.port));
  if (!conn.Connect(&err)) {
    Fail(slot, err);
    return;
  }
  if (!conn.resolved_ip().empty()) {
    Log(slot, "rtsp", true, url.host + " = " + conn.resolved_ip());
  }

  const std::string base_uri = url.request_url();

  // OPTIONS first: some cameras require it, and it settles authentication. The
  // Public header says whether GET_PARAMETER can be the keepalive; without it,
  // the keepalive is OPTIONS itself, which every camera accepts.
  std::string keepalive_method = "OPTIONS";
  if (conn.Request("OPTIONS", base_uri, "", &resp, &err)) {
    std::string methods = resp.Header("Public");
    std::transform(methods.begin(), methods.end(), methods.begin(), ::toupper);
    if (methods.find("GET_PARAMETER") != std::string::npos) keepalive_method = "GET_PARAMETER";
  } else if (resp.status == 0 || resp.status == 401) {
    // No response (network) or login refused: DESCRIBE would fail the same way.
    Fail(slot, err);
    return;
  }
  // Other OPTIONS errors (404, 405, 501...) do not prevent DESCRIBE.

  if (!conn.Request("DESCRIBE", base_uri, "Accept: application/sdp\r\n", &resp, &err)) {
    Fail(slot, err);
    return;
  }
  std::string content_base = resp.Header("Content-Base");
  if (content_base.empty()) content_base = resp.Header("Content-Location");
  if (content_base.empty()) content_base = base_uri;

  rtsp::SdpVideo video = rtsp::ParseSdpVideo(resp.body);
  const bool is_h265 = video.codec == "H265" || video.codec == "HEVC";
  if (!video.found || (video.codec != "H264" && !is_h265)) {
    Fatal(slot, "codec_unsupported", video.codec.empty() ? std::string("none") : video.codec);
    return;
  }
  // The software decoder (mosaic) is H.264 only; H.265 uses the TV player.
  if (is_h265 && req.mode == Mode::kSoftware) {
    Fatal(slot, "h265_mosaic");
    return;
  }

  std::string track_uri = rtsp::ResolveControl(content_base, video.control);
  if (!conn.Request("SETUP", track_uri,
                    "Transport: RTP/AVP/TCP;unicast;interleaved=0-1\r\n", &resp, &err)) {
    Fail(slot, err);
    return;
  }
  std::string session_id;
  int session_timeout = 0;
  rtsp::ParseSessionHeader(resp.Header("Session"), &session_id, &session_timeout);
  conn.set_session(session_id);
  int rtp_channel = 0;
  std::string transport = resp.Header("Transport");
  size_t il = transport.find("interleaved=");
  if (il != std::string::npos) rtp_channel = std::atoi(transport.c_str() + il + 12);

  if (!conn.Request("PLAY", content_base, "Range: npt=0.000-\r\n", &resp, &err)) {
    Fail(slot, err);
    return;
  }
  Log(slot, "rtsp", true, "PLAY ok");
  const double play_ms = emscripten_get_now();
  Emit(slot, "play", std::string("{\"mode\":\"") +
                         (req.mode == Mode::kNative ? "native" : "software") +
                         "\",\"codec\":\"" + (is_h265 ? "H265" : "H264") + "\"}");

  Stats stats;
  bool waiting_key = true;
  bool native_requested = false;
  bool first_frame = true;
  bool have_base_ts = false;
  uint32_t last_ts = 0;
  int64_t ts_offset = 0;  // accumulates the wraps of the 32-bit timestamp
  double clock_rate = video.clock_rate > 0 ? video.clock_rate : 90000;
  double frame_duration = 1.0 / (video.framerate > 0 ? video.framerate : 30);
  // Interval between key frames (GOP), measured by the RTP timestamp.
  bool have_key_ts = false;
  uint32_t last_key_ts = 0;
  double gop_s = 0;
  media::VideoInfo info;
  std::unique_ptr<media::Depacketizer> depack;
  std::unique_ptr<h264::SoftDecoder> decoder;
  h264::YuvFrame yuv;
  if (req.mode == Mode::kSoftware) {
    decoder.reset(new h264::SoftDecoder());
    if (!decoder->ok()) {
      app::LogError(slot, "rtsp", "decoder_init", "OpenH264");
      return;
    }
  }

  // Time from PLAY to the first picture (diagnostics for slow opening).
  auto mark_first_frame = [&]() {
    if (!first_frame) return;
    first_frame = false;
    Emit(slot, "first-frame",
         "{\"ms\":" + std::to_string(static_cast<int>(emscripten_get_now() - play_ms)) + "}");
  };

  auto on_native = [&](media::AccessUnit& au) {
    if (!native_requested) {
      native_requested = true;
      auto* nreq = new NativeRequest{slot, info, video.framerate};
      emscripten_async_run_in_main_runtime_thread(
          EM_FUNC_SIG_VI, CreateNativeOnMain, reinterpret_cast<int>(nreq));
      return;
    }
    app::NativePlayer* player = state.native.load();
    if (!player || !player->track_open()) {
      waiting_key = true;
      ++stats.dropped;
      return;
    }
    if (waiting_key) {
      if (!au.key_frame) {
        ++stats.dropped;
        return;
      }
      waiting_key = false;
    }
    if (!have_base_ts) {
      have_base_ts = true;
      last_ts = au.rtp_timestamp;
    }
    // Signed difference handles the 32-bit wraparound.
    ts_offset += static_cast<int32_t>(au.rtp_timestamp - last_ts);
    last_ts = au.rtp_timestamp;
    if (player->Append(au, static_cast<double>(ts_offset) / clock_rate, frame_duration)) {
      ++stats.frames;
      mark_first_frame();
    } else {
      ++stats.dropped;
      waiting_key = true;
    }
  };

  auto on_software = [&](media::AccessUnit& au) {
    // If the CPU fell behind, the socket piles up: skip to the next IDR.
    if (conn.available() > kMaxBacklogBytes && !au.key_frame) {
      waiting_key = true;
    }
    if (waiting_key) {
      if (!au.key_frame) {
        ++stats.dropped;
        return;
      }
      waiting_key = false;
    }
    double t0 = emscripten_get_now();
    bool got = decoder->Decode(au.data.data(), au.data.size(), &yuv);
    stats.decode_ms += emscripten_get_now() - t0;
    ++stats.decoded;
    if (got) {
      render::Publish(slot, &yuv);
      ++stats.frames;
      mark_first_frame();
    }
  };

  auto on_access_unit = [&](media::AccessUnit& au) {
    if (info.width == 0 && !depack->Info(&info)) return;  // no SPS yet
    if (au.key_frame) {
      if (have_key_ts) {
        double interval = static_cast<uint32_t>(au.rtp_timestamp - last_key_ts) / clock_rate;
        gop_s = gop_s > 0 ? gop_s * 0.7 + interval * 0.3 : interval;
      }
      have_key_ts = true;
      last_key_ts = au.rtp_timestamp;
    }
    if (req.mode == Mode::kNative) {
      on_native(au);
    } else {
      on_software(au);
    }
  };
  if (is_h265) {
    auto* d = new h265::Depacketizer(video.payload_type, on_access_unit);
    d->SetParameterSets(video.vps, video.sps, video.pps);
    depack.reset(d);
  } else {
    auto* d = new h264::Depacketizer(video.payload_type, on_access_unit);
    d->SetParameterSets(video.sps, video.pps);
    depack.reset(d);
  }

  int keepalive_s = session_timeout > 0 ? std::max(5, session_timeout / 2) : 25;
  auto next_keepalive = Clock::now() + std::chrono::seconds(keepalive_s);
  auto next_stats = Clock::now() + std::chrono::seconds(2);
  auto last_data = Clock::now();

  while (!state.stop) {
    int n = conn.Fill(200);
    if (n < 0) {
      app::LogError(slot, "rtsp", "closed_by_camera");
      break;
    }
    if (n > 0) {
      stats.bytes += static_cast<uint64_t>(n);
      last_data = Clock::now();
    }

    // Interleaved frames "$" + channel + length, or RTSP messages in between.
    while (conn.available() > 0 && !state.stop) {
      const uint8_t* d = conn.data();
      if (d[0] == '$') {
        if (conn.available() < 4) break;
        int channel = d[1];
        size_t len = static_cast<size_t>(d[2] << 8 | d[3]);
        if (conn.available() < 4 + len) break;
        if (channel == rtp_channel) depack->Push(d + 4, len);
        conn.Consume(4 + len);
      } else if (conn.available() >= 5 && std::memcmp(d, "RTSP/", 5) == 0) {
        rtsp::Response r;
        if (!conn.TakeResponse(&r)) break;  // keepalive response
      } else {
        conn.Consume(1);  // resynchronizes up to the next '$'
      }
    }

    auto now = Clock::now();
    if (now > next_keepalive) {
      next_keepalive = now + std::chrono::seconds(keepalive_s);
      conn.Send(keepalive_method, content_base, "", &err);
    }
    if (now > next_stats) {
      next_stats = now + std::chrono::seconds(2);
      double decode_ms = stats.decoded ? stats.decode_ms / stats.decoded : 0;
      Emit(slot, "stats",
           "{\"kbps\":" + std::to_string(stats.bytes * 8 / 2000) +
               ",\"width\":" + std::to_string(info.width) +
               ",\"height\":" + std::to_string(info.height) +
               ",\"fps\":" + std::to_string(stats.frames / 2.0) +
               ",\"dropped\":" + std::to_string(stats.dropped) +
               ",\"decode_ms\":" + std::to_string(decode_ms) +
               ",\"lost\":" + std::to_string(depack->lost_packets()) +
               ",\"gop_s\":" + std::to_string(gop_s) +
               ",\"codec\":\"" + (is_h265 ? "H265" : "H264") + "\"" +
               ",\"mode\":\"" + (req.mode == Mode::kNative ? "native" : "software") + "\"}");
      stats = Stats();
    }
    if (now - last_data > std::chrono::seconds(10)) {
      app::LogError(slot, "rtsp", "no_data", "10 s");
      break;
    }
  }

  render::Drop(slot);
  conn.Send("TEARDOWN", content_base, "", &err);
}

void* SessionThread(void* arg) {
  std::unique_ptr<SessionRequest> req(static_cast<SessionRequest*>(arg));
  RunSession(*req);
  emscripten_async_run_in_main_runtime_thread(EM_FUNC_SIG_VI, FinishSlotOnMain,
                                              req->slot);
  return nullptr;
}

}  // namespace

extern "C" {

// mode: 0 native (<video id="video">, one slot at a time), 1 software (canvas).
EMSCRIPTEN_KEEPALIVE int player_start(int slot, const char* url_cstr, int mode) {
  if (slot < 0 || slot >= kMaxSlots) return 0;
  Slot& state = g_slots[slot];
  if (state.running.exchange(true)) {
    app::LogError(slot, "rtsp", "slot_busy");
    return 0;
  }
  auto* req = new SessionRequest();
  req->slot = slot;
  req->mode = mode == 0 ? Mode::kNative : Mode::kSoftware;
  if (req->mode == Mode::kNative) {
    int expected = -1;
    if (!g_native_slot.compare_exchange_strong(expected, slot)) {
      app::LogError(slot, "rtsp", "native_busy");
      delete req;
      state.running = false;
      return 0;
    }
  } else if (!render::Init()) {
    delete req;
    state.running = false;
    return 0;
  }

  std::string error;
  if (!rtsp::ParseRtspUrl(url_cstr ? url_cstr : "", &req->url, &error)) {
    app::LogError(slot, "rtsp", "invalid_url", error);
    if (req->mode == Mode::kNative) g_native_slot = -1;
    delete req;
    state.running = false;
    return 0;
  }
  state.stop = false;

  pthread_t t;
  int rc = pthread_create(&t, nullptr, SessionThread, req);
  if (rc != 0) {
    app::LogError(slot, "rtsp", "thread_failed", std::strerror(rc));
    if (req->mode == Mode::kNative) g_native_slot = -1;
    delete req;
    state.running = false;
    return 0;
  }
  pthread_detach(t);
  return 1;
}

EMSCRIPTEN_KEEPALIVE void player_stop(int slot) {
  if (slot < 0 || slot >= kMaxSlots) return;
  g_slots[slot].stop = true;
  // Called from the main thread: if the network thread is stuck in an
  // AppendPacket() of the native player, closing the source unblocks it.
  if (app::NativePlayer* native = g_slots[slot].native.load()) native->Interrupt();
}

EMSCRIPTEN_KEEPALIVE int player_running(int slot) {
  return slot >= 0 && slot < kMaxSlots && g_slots[slot].running ? 1 : 0;
}

// Slot rectangle on the 1920x1080 canvas (software mode).
EMSCRIPTEN_KEEPALIVE void player_set_rect(int slot, int x, int y, int w, int h) {
  render::SetRect(slot, x, y, w, h);
}

EMSCRIPTEN_KEEPALIVE void player_clear_canvas() { render::ClearAll(); }

}  // extern "C"

int main() {
  std::srand(static_cast<unsigned>(emscripten_get_now()));
  EM_ASM(Module['noExitRuntime'] = true; if (Module.onReady) Module.onReady(););
  return 0;
}
