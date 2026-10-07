// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Ervin Notari Junior

// RTSP connection over TCP (Tizen Sockets). Can only be used off the main
// thread.
#pragma once

#include <atomic>
#include <cstdint>
#include <string>
#include <vector>

#include "events.hpp"
#include "rtsp_protocol.hpp"
#include "rtsp_url.hpp"

namespace rtsp {

class Connection {
 public:
  Connection(const RtspUrl& url, const std::atomic<bool>& stop);
  ~Connection();
  Connection(const Connection&) = delete;
  Connection& operator=(const Connection&) = delete;

  bool Connect(app::Error* err);

  // IP obtained from DNS when the URL uses a name (empty if it was already an IP).
  const std::string& resolved_ip() const { return resolved_ip_; }

  // Request with an expected response (before PLAY). Retries once with
  // authentication if the camera answers 401.
  bool Request(const std::string& method, const std::string& uri,
               const std::string& extra, Response* resp, app::Error* err);

  bool Send(const std::string& method, const std::string& uri,
            const std::string& extra, app::Error* err);

  // Reads more bytes from the socket. -1 error/closed, 0 timeout, >0 bytes read.
  int Fill(int timeout_ms);

  size_t available() const { return rx_.size() - rx_off_; }
  const uint8_t* data() const { return rx_.data() + rx_off_; }
  void Consume(size_t n) { rx_off_ += n; }

  // If there is a complete RTSP message in the buffer, consumes it and fills `resp`.
  bool TakeResponse(Response* resp);

  void set_session(const std::string& s) { session_ = s; }

 private:
  bool ReadResponse(Response* resp, app::Error* err);

  RtspUrl url_;
  const std::atomic<bool>& stop_;
  int fd_ = -1;
  std::string resolved_ip_;
  int cseq_ = 0;
  unsigned nonce_count_ = 0;
  bool has_auth_ = false;
  AuthChallenge auth_;
  std::string session_;
  std::vector<uint8_t> rx_;
  size_t rx_off_ = 0;
  std::vector<uint8_t> recv_buf_ = std::vector<uint8_t>(65536);
};

}  // namespace rtsp
