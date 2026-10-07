// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Ervin Notari Junior

#include "native_player.hpp"

#include <cstdio>
#include <string>
#include <utility>

#include "events.hpp"
#include "samsung/wasm/elementary_media_packet.h"
#include "samsung/wasm/elementary_video_track_config.h"

namespace app {

using samsung::wasm::ElementaryMediaPacket;
using samsung::wasm::ElementaryMediaStreamSource;
using samsung::wasm::ElementaryMediaTrack;
using samsung::wasm::ElementaryVideoTrackConfig;
using samsung::wasm::EmssLatencyMode;
using samsung::wasm::EmssRenderingMode;
using samsung::wasm::OperationResult;
using samsung::wasm::Seconds;

namespace {

std::string ResultStr(OperationResult r) {
  return "OperationResult " + std::to_string(static_cast<int>(r));
}

}  // namespace

NativePlayer::NativePlayer(int slot, const media::VideoInfo& info, double fps)
    : slot_(slot), info_(info), fps_(fps) {}

void NativePlayer::Start() {
  media_.reset(new samsung::html::HTMLMediaElement("video"));
  if (!media_->IsValid()) {
    Log(slot_, "player", false, "element <video id=\"video\"> not found");
    return;
  }
  source_.reset(new ElementaryMediaStreamSource(EmssLatencyMode::kLow,
                                                EmssRenderingMode::kMediaElement));
  if (!source_->IsValid()) {
    Log(slot_, "player", false, "invalid ElementaryMediaStreamSource");
    return;
  }
  source_->SetListener(this);
  auto r = media_->SetSrc(source_.get());
  if (!r) Log(slot_, "player", false, "SetSrc: " + ResultStr(r.operation_result));
}

bool NativePlayer::Append(const media::AccessUnit& au, double pts, double duration) {
  ElementaryMediaPacket p{};
  p.pts = Seconds(pts);
  p.dts = Seconds(pts);
  p.duration = Seconds(duration);
  p.is_key_frame = au.key_frame;
  p.data_size = au.data.size();
  p.data = au.data.data();
  p.session_id = session_id_.load();
  // Synchronous, as in Samsung's Moonlight: the frame buffer is reused
  // right after. If it blocks, Interrupt() closes the source and unblocks it.
  auto r = track_.AppendPacket(p);
  if (!r && ++append_errors_ <= 5) {
    Log(slot_, "player", false, "AppendPacket: " + ResultStr(r.operation_result));
  }
  return static_cast<bool>(r);
}

void NativePlayer::Interrupt() {
  track_open_ = false;
  track_requested_ = true;  // do not recreate the track when the source closes
  interrupted_ = true;
  if (source_) source_->Close([](OperationResult) {});
}

void NativePlayer::OnSourceClosed() {
  if (track_requested_) return;
  track_requested_ = true;

  std::string mime = "video/mp4; codecs=\"" + info_.codecs + "\"";
  int fps = fps_ > 0 ? static_cast<int>(fps_ + 0.5) : 30;
  // Empty extradata: the parameters (SPS/PPS, and VPS for H.265) come in-band
  // before each key frame (Annex B).
  ElementaryVideoTrackConfig cfg{mime, {}, static_cast<uint32_t>(info_.width),
                                 static_cast<uint32_t>(info_.height),
                                 static_cast<uint32_t>(fps), 1};
  source_->AddTrack(cfg, [this](OperationResult res, ElementaryMediaTrack t) {
    if (res != OperationResult::kSuccess || !t.IsValid()) {
      Log(slot_, "player", false, "AddTrack failed: " + ResultStr(res));
      return;
    }
    track_ = std::move(t);
    track_.SetListener(this);
    int slot = slot_;
    source_->Open([slot](OperationResult r) {
      if (r != OperationResult::kSuccess) {
        Log(slot, "player", false, "Open failed: " + ResultStr(r));
      }
    });
  });
}

// In low latency the source leaves "open pending" only after Play().
void NativePlayer::OnSourceOpenPending() {
  int slot = slot_;
  media_->Play([slot](OperationResult r) {
    if (r != OperationResult::kSuccess) {
      Log(slot, "player", false, "Play failed: " + ResultStr(r));
    }
  });
}

void NativePlayer::OnPipelineError(samsung::wasm::MediaPipelineError err,
                                   const char* msg) {
  Log(slot_, "player", false, "pipeline error " +
                                  std::to_string(static_cast<int>(err)) + " " +
                                  (msg ? msg : ""));
}

void NativePlayer::OnTrackOpen() {
  auto id = track_.GetSessionId();
  if (id) session_id_ = id.value;
  track_open_ = true;
}

void NativePlayer::OnTrackClosed(ElementaryMediaTrack::CloseReason reason) {
  track_open_ = false;
  if (interrupted_) return;
  Log(slot_, "player", false, "track closed, reason " +
                                  std::to_string(static_cast<int>(reason)));
}

void NativePlayer::OnSessionIdChanged(samsung::wasm::SessionId id) {
  session_id_ = id;
}

void NativePlayer::OnAppendError(OperationResult r) {
  Log(slot_, "player", false, "append error: " + ResultStr(r));
}

}  // namespace app
