// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Ervin Notari Junior

// TV native player (ElementaryMediaStreamSource, hardware decoder) on the
// <video id="video">. The TV accepts only ONE playback at a time, so only one
// slot can use this mode.
#pragma once

#include <atomic>
#include <memory>

#include "video.hpp"
#include "samsung/html/html_media_element.h"
#include "samsung/wasm/elementary_media_stream_source.h"
#include "samsung/wasm/elementary_media_stream_source_listener.h"
#include "samsung/wasm/elementary_media_track.h"
#include "samsung/wasm/elementary_media_track_listener.h"

namespace app {

// Lives on the main thread. The network thread only calls Append() and track_open().
class NativePlayer : public samsung::wasm::ElementaryMediaStreamSourceListener,
                     public samsung::wasm::ElementaryMediaTrackListener {
 public:
  NativePlayer(int slot, const media::VideoInfo& info, double fps);

  void Start();

  // Network thread. Returns false if the packet was rejected.
  bool Append(const media::AccessUnit& au, double pts, double duration);
  bool track_open() const { return track_open_.load(); }

  // Main thread: closes the source to unblock a blocked AppendPacket()
  // and let the session end.
  void Interrupt();

  // ElementaryMediaStreamSourceListener
  void OnSourceClosed() override;
  void OnSourceOpenPending() override;
  void OnPipelineError(samsung::wasm::MediaPipelineError err,
                       const char* msg) override;

  // ElementaryMediaTrackListener
  void OnTrackOpen() override;
  void OnTrackClosed(samsung::wasm::ElementaryMediaTrack::CloseReason reason) override;
  void OnSessionIdChanged(samsung::wasm::SessionId id) override;
  void OnAppendError(samsung::wasm::OperationResult r) override;

 private:
  int slot_;
  media::VideoInfo info_;
  double fps_;
  // Order matters: the HTMLMediaElement must outlive the source.
  std::unique_ptr<samsung::html::HTMLMediaElement> media_;
  std::unique_ptr<samsung::wasm::ElementaryMediaStreamSource> source_;
  samsung::wasm::ElementaryMediaTrack track_;
  bool track_requested_ = false;
  bool interrupted_ = false;  // closed on purpose: not an error
  std::atomic<bool> track_open_{false};
  std::atomic<samsung::wasm::SessionId> session_id_{0};
  int append_errors_ = 0;
};

}  // namespace app
