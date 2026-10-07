// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Ervin Notari Junior

// Network-free parts of the RTSP protocol: responses, authentication and SDP.
// Platform independent (compiles natively for tests).
#pragma once

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace rtsp {

struct Response {
  int status = 0;
  std::string reason;
  std::vector<std::pair<std::string, std::string>> headers;
  std::string body;

  // First header with this name (case-insensitive), or "".
  std::string Header(const std::string& name) const;
  std::vector<std::string> Headers(const std::string& name) const;
  int ContentLength() const;
};

// `head` is the status + headers block, without the final "\r\n\r\n".
bool ParseResponseHead(const std::string& head, Response* out);

std::vector<uint8_t> Base64Decode(const std::string& in);
std::string Base64Encode(const std::string& in);

struct AuthChallenge {
  bool digest = false;  // false = Basic
  std::string realm;
  std::string nonce;
  std::string qop;
  std::string opaque;
  std::string algorithm;
};

// Chooses Digest when present, otherwise Basic. Returns false if neither.
bool ParseAuthChallenges(const std::vector<std::string>& www_authenticate,
                         AuthChallenge* out);

// Value of the Authorization header for a request.
std::string BuildAuthorization(const AuthChallenge& challenge,
                               const std::string& user,
                               const std::string& password,
                               const std::string& method,
                               const std::string& uri, unsigned nonce_count,
                               const std::string& cnonce);

struct SdpVideo {
  bool found = false;
  int payload_type = -1;
  std::string codec;  // e.g. "H264", "H265"
  int clock_rate = 90000;
  std::string control;
  std::vector<uint8_t> vps;  // H.265 only
  std::vector<uint8_t> sps;
  std::vector<uint8_t> pps;
  double framerate = 0;
};

// First video media of the SDP.
SdpVideo ParseSdpVideo(const std::string& sdp);

// Resolves the a=control attribute against the base URL (Content-Base).
std::string ResolveControl(const std::string& base, const std::string& control);

// Session: "abc123;timeout=60" -> id "abc123", timeout 60 (0 if missing).
void ParseSessionHeader(const std::string& value, std::string* id,
                        int* timeout);

}  // namespace rtsp
