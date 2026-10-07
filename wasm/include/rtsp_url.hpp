// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Ervin Notari Junior

// RTSP URL parser. Platform independent (compiles natively for tests).
#pragma once

#include <cstdint>
#include <string>

namespace rtsp {

struct RtspUrl {
  std::string user;
  std::string password;
  std::string host;
  uint16_t port = 554;
  std::string path = "/";  // includes the query, always starts with '/'

  bool has_credentials() const { return !user.empty() || !password.empty(); }

  // URL without credentials, used in RTSP requests (the login goes in the header).
  std::string request_url() const;

  // URL safe for logs: password replaced by "***".
  std::string sanitized() const;
};

// Returns false and fills `error` if the URL is invalid.
bool ParseRtspUrl(const std::string& url, RtspUrl* out, std::string* error);

// Removes the password from any text that contains "rtsp://user:password@".
std::string SanitizeForLog(const std::string& text);

}  // namespace rtsp
