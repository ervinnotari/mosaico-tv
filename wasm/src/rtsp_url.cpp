// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Ervin Notari Junior

#include "rtsp_url.hpp"

#include <cctype>
#include <cstdlib>

namespace rtsp {

namespace {

bool StartsWithNoCase(const std::string& s, const char* prefix) {
  for (size_t i = 0; prefix[i]; ++i) {
    if (i >= s.size() ||
        std::tolower(static_cast<unsigned char>(s[i])) != prefix[i]) {
      return false;
    }
  }
  return true;
}

int HexValue(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}

// Decodes %XX in user/password (e.g. a password with '@' becomes "%40").
std::string PercentDecode(const std::string& in) {
  std::string out;
  out.reserve(in.size());
  size_t i = 0;
  while (i < in.size()) {
    int hi = -1;
    int lo = -1;
    if (in[i] == '%' && i + 2 < in.size()) {
      hi = HexValue(in[i + 1]);
      lo = HexValue(in[i + 2]);
    }
    if (hi >= 0 && lo >= 0) {
      out.push_back(static_cast<char>(hi * 16 + lo));
      i += 3;
    } else {
      out.push_back(in[i]);
      i += 1;
    }
  }
  return out;
}

std::string HostPort(const RtspUrl& u) {
  std::string hp = u.host.find(':') != std::string::npos
                       ? "[" + u.host + "]"  // IPv6
                       : u.host;
  return hp + ":" + std::to_string(u.port);
}

}  // namespace

std::string RtspUrl::request_url() const {
  return "rtsp://" + HostPort(*this) + path;
}

std::string RtspUrl::sanitized() const {
  std::string auth;
  if (has_credentials()) auth = user + ":***@";
  return "rtsp://" + auth + HostPort(*this) + path;
}

bool ParseRtspUrl(const std::string& url, RtspUrl* out, std::string* error) {
  *out = RtspUrl();
  static const char kScheme[] = "rtsp://";
  if (!StartsWithNoCase(url, kScheme)) {
    *error = "URL must start with rtsp://";
    return false;
  }
  std::string rest = url.substr(sizeof(kScheme) - 1);

  size_t slash = rest.find('/');
  std::string authority = rest.substr(0, slash);
  out->path = slash == std::string::npos ? "/" : rest.substr(slash);

  // The last '@' separates the credentials (the password may contain an unencoded '@').
  size_t at = authority.rfind('@');
  if (at != std::string::npos) {
    std::string cred = authority.substr(0, at);
    authority = authority.substr(at + 1);
    size_t colon = cred.find(':');
    out->user = PercentDecode(cred.substr(0, colon));
    if (colon != std::string::npos) {
      out->password = PercentDecode(cred.substr(colon + 1));
    }
  }

  std::string port_str;
  if (!authority.empty() && authority[0] == '[') {
    size_t close = authority.find(']');
    if (close == std::string::npos) {
      *error = "IPv6 address without ']'";
      return false;
    }
    out->host = authority.substr(1, close - 1);
    if (close + 1 < authority.size()) {
      if (authority[close + 1] != ':') {
        *error = "invalid character after the host";
        return false;
      }
      port_str = authority.substr(close + 2);
    }
  } else {
    size_t colon = authority.rfind(':');
    out->host = authority.substr(0, colon);
    if (colon != std::string::npos) port_str = authority.substr(colon + 1);
  }

  if (out->host.empty()) {
    *error = "missing host";
    return false;
  }
  if (!port_str.empty()) {
    char* end = nullptr;
    long p = std::strtol(port_str.c_str(), &end, 10);
    if (*end != '\0' || p <= 0 || p > 65535) {
      *error = "invalid port";
      return false;
    }
    out->port = static_cast<uint16_t>(p);
  }
  return true;
}

std::string SanitizeForLog(const std::string& text) {
  std::string out = text;
  size_t pos = 0;
  while (true) {
    size_t scheme = out.find("://", pos);
    if (scheme == std::string::npos) break;
    size_t start = scheme + 3;
    size_t end = out.find_first_of("/ \r\n\"'", start);
    if (end == std::string::npos) end = out.size();
    size_t at = out.rfind('@', end);
    if (at != std::string::npos && at >= start) {
      size_t colon = out.find(':', start);
      if (colon != std::string::npos && colon < at) {
        out.replace(colon + 1, at - colon - 1, "***");
        end = colon + 1 + 3 + (end - at);
      }
    }
    pos = end;
  }
  return out;
}

}  // namespace rtsp
