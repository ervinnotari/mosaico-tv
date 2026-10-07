// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Ervin Notari Junior

// MD5 (RFC 1321), used only by RTSP Digest authentication.
#pragma once

#include <string>

namespace rtsp {

// Returns the hash in lowercase hexadecimal (32 characters).
std::string Md5Hex(const std::string& data);

}  // namespace rtsp
