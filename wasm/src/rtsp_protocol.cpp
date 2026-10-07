// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Ervin Notari Junior

#include "rtsp_protocol.hpp"

#include <array>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <sstream>

#include "md5.hpp"

namespace rtsp {

namespace {

std::string Trim(const std::string& s) {
  size_t b = 0;
  size_t e = s.size();
  while (b < e && std::isspace(static_cast<unsigned char>(s[b]))) ++b;
  while (e > b && std::isspace(static_cast<unsigned char>(s[e - 1]))) --e;
  return s.substr(b, e - b);
}

std::string Lower(std::string s) {
  for (auto& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return s;
}

bool StartsWithNoCase(const std::string& s, const std::string& prefix) {
  return s.size() >= prefix.size() && Lower(s.substr(0, prefix.size())) == Lower(prefix);
}

// Reads comma-separated "key=value" parameters, with quoted values.
std::vector<std::pair<std::string, std::string>> ParseAuthParams(
    const std::string& s) {
  std::vector<std::pair<std::string, std::string>> out;
  size_t i = 0;
  while (i < s.size()) {
    while (i < s.size() && (s[i] == ',' || std::isspace(static_cast<unsigned char>(s[i])))) ++i;
    size_t eq = s.find('=', i);
    if (eq == std::string::npos) break;
    std::string key = Lower(Trim(s.substr(i, eq - i)));
    i = eq + 1;
    std::string value;
    if (i < s.size() && s[i] == '"') {
      size_t end = s.find('"', i + 1);
      if (end == std::string::npos) end = s.size();
      value = s.substr(i + 1, end - i - 1);
      i = end + 1;
    } else {
      size_t end = s.find(',', i);
      if (end == std::string::npos) end = s.size();
      value = Trim(s.substr(i, end - i));
      i = end;
    }
    out.emplace_back(key, value);
  }
  return out;
}

}  // namespace

std::string Response::Header(const std::string& name) const {
  for (const auto& h : headers) {
    if (Lower(h.first) == Lower(name)) return h.second;
  }
  return "";
}

std::vector<std::string> Response::Headers(const std::string& name) const {
  std::vector<std::string> out;
  for (const auto& h : headers) {
    if (Lower(h.first) == Lower(name)) out.push_back(h.second);
  }
  return out;
}

int Response::ContentLength() const {
  std::string v = Header("Content-Length");
  return v.empty() ? 0 : std::atoi(v.c_str());
}

bool ParseResponseHead(const std::string& head, Response* out) {
  *out = Response();
  std::istringstream in(head);
  std::string line;
  if (!std::getline(in, line)) return false;
  if (!line.empty() && line.back() == '\r') line.pop_back();
  if (line.compare(0, 5, "RTSP/") != 0) return false;
  size_t sp1 = line.find(' ');
  if (sp1 == std::string::npos) return false;
  out->status = std::atoi(line.c_str() + sp1 + 1);
  size_t sp2 = line.find(' ', sp1 + 1);
  if (sp2 != std::string::npos) out->reason = line.substr(sp2 + 1);
  while (std::getline(in, line)) {
    if (!line.empty() && line.back() == '\r') line.pop_back();
    if (line.empty()) break;
    size_t colon = line.find(':');
    if (colon == std::string::npos) continue;
    out->headers.emplace_back(Trim(line.substr(0, colon)),
                              Trim(line.substr(colon + 1)));
  }
  return out->status > 0;
}

std::vector<uint8_t> Base64Decode(const std::string& in) {
  std::vector<uint8_t> out;
  uint32_t buf = 0;
  int bits = 0;
  for (char c : in) {
    int v;
    if (c >= 'A' && c <= 'Z') v = c - 'A';
    else if (c >= 'a' && c <= 'z') v = c - 'a' + 26;
    else if (c >= '0' && c <= '9') v = c - '0' + 52;
    else if (c == '+' || c == '-') v = 62;
    else if (c == '/' || c == '_') v = 63;
    else continue;  // '=' and spaces
    buf = (buf << 6) | static_cast<uint32_t>(v);
    bits += 6;
    if (bits >= 8) {
      bits -= 8;
      out.push_back(static_cast<uint8_t>((buf >> bits) & 0xff));
    }
  }
  return out;
}

std::string Base64Encode(const std::string& in) {
  static const char kTable[] =
      "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  std::string out;
  size_t i = 0;
  while (i + 2 < in.size()) {
    uint32_t n = static_cast<uint8_t>(in[i]) << 16 |
                 static_cast<uint8_t>(in[i + 1]) << 8 |
                 static_cast<uint8_t>(in[i + 2]);
    out += kTable[n >> 18];
    out += kTable[(n >> 12) & 63];
    out += kTable[(n >> 6) & 63];
    out += kTable[n & 63];
    i += 3;
  }
  size_t rest = in.size() - i;
  if (rest) {
    uint32_t n = static_cast<uint8_t>(in[i]) << 16;
    if (rest == 2) n |= static_cast<uint8_t>(in[i + 1]) << 8;
    out += kTable[n >> 18];
    out += kTable[(n >> 12) & 63];
    out += rest == 2 ? kTable[(n >> 6) & 63] : '=';
    out += '=';
  }
  return out;
}

namespace {

AuthChallenge ParseDigest(const std::string& params) {
  AuthChallenge c;
  c.digest = true;
  for (const auto& p : ParseAuthParams(params)) {
    if (p.first == "realm") c.realm = p.second;
    else if (p.first == "nonce") c.nonce = p.second;
    else if (p.first == "qop") c.qop = p.second;
    else if (p.first == "opaque") c.opaque = p.second;
    else if (p.first == "algorithm") c.algorithm = p.second;
  }
  return c;
}

}  // namespace

bool ParseAuthChallenges(const std::vector<std::string>& www_authenticate,
                         AuthChallenge* out) {
  bool has_basic = false;
  for (const auto& h : www_authenticate) {
    if (StartsWithNoCase(h, "Digest")) {
      *out = ParseDigest(h.substr(6));
      return true;
    }
    if (StartsWithNoCase(h, "Basic")) has_basic = true;
  }
  if (has_basic) {
    *out = AuthChallenge();
    return true;
  }
  return false;
}

std::string BuildAuthorization(const AuthChallenge& c, const std::string& user,
                               const std::string& password,
                               const std::string& method,
                               const std::string& uri, unsigned nonce_count,
                               const std::string& cnonce) {
  if (!c.digest) return "Basic " + Base64Encode(user + ":" + password);

  std::string ha1 = Md5Hex(user + ":" + c.realm + ":" + password);
  std::string ha2 = Md5Hex(method + ":" + uri);
  // Only "auth" is supported; cameras with "auth-int" are rare.
  bool use_qop = c.qop.find("auth") != std::string::npos;
  std::array<char, 9> nc_buf;
  std::snprintf(nc_buf.data(), nc_buf.size(), "%08x", nonce_count);
  const std::string nc = nc_buf.data();
  std::string response =
      use_qop ? Md5Hex(ha1 + ":" + c.nonce + ":" + nc + ":" + cnonce + ":auth:" + ha2)
              : Md5Hex(ha1 + ":" + c.nonce + ":" + ha2);

  std::string out = "Digest username=\"" + user + "\", realm=\"" + c.realm +
                    "\", nonce=\"" + c.nonce + "\", uri=\"" + uri +
                    "\", response=\"" + response + "\"";
  if (!c.algorithm.empty()) out += ", algorithm=" + c.algorithm;
  if (!c.opaque.empty()) out += ", opaque=\"" + c.opaque + "\"";
  if (use_qop) {
    out += ", qop=auth, nc=" + nc + ", cnonce=\"" + cnonce + "\"";
  }
  return out;
}

namespace {

// a=rtpmap:96 H264/90000
void ParseRtpMap(const std::string& value, SdpVideo* v) {
  std::istringstream m(value);
  int pt = -1;
  std::string enc;
  m >> pt >> enc;
  if (pt != v->payload_type) return;
  size_t slash = enc.find('/');
  v->codec = enc.substr(0, slash);
  if (slash != std::string::npos) v->clock_rate = std::atoi(enc.c_str() + slash + 1);
}

// Only the first set of each type (several separated by commas).
std::vector<uint8_t> FirstSet(const std::string& list) {
  return Base64Decode(list.substr(0, list.find(',')));
}

// One fmtp parameter with parameter sets (H.264 or H.265).
void ParseFmtpParam(const std::string& p, SdpVideo* v) {
  if (StartsWithNoCase(p, "sprop-parameter-sets=")) {  // H.264
    std::string sets = p.substr(21);
    size_t comma = sets.find(',');
    v->sps = Base64Decode(sets.substr(0, comma));
    if (comma != std::string::npos) v->pps = Base64Decode(sets.substr(comma + 1));
  } else if (StartsWithNoCase(p, "sprop-vps=")) {  // H.265
    v->vps = FirstSet(p.substr(10));
  } else if (StartsWithNoCase(p, "sprop-sps=")) {
    v->sps = FirstSet(p.substr(10));
  } else if (StartsWithNoCase(p, "sprop-pps=")) {
    v->pps = FirstSet(p.substr(10));
  }
}

// a=fmtp:96 packetization-mode=1;sprop-parameter-sets=...
void ParseFmtp(const std::string& line, SdpVideo* v) {
  size_t sp = line.find(' ');
  if (sp == std::string::npos) return;
  std::istringstream ps(line.substr(sp + 1));
  std::string p;
  while (std::getline(ps, p, ';')) ParseFmtpParam(Trim(p), v);
}

// An attribute line of the video media.
void ParseVideoAttribute(const std::string& line, SdpVideo* v) {
  if (line.compare(0, 9, "a=rtpmap:") == 0) {
    ParseRtpMap(line.substr(9), v);
  } else if (line.compare(0, 7, "a=fmtp:") == 0) {
    ParseFmtp(line, v);
  } else if (line.compare(0, 10, "a=control:") == 0) {
    v->control = Trim(line.substr(10));
  } else if (line.compare(0, 12, "a=framerate:") == 0) {
    v->framerate = std::atof(line.c_str() + 12);
  }
}

}  // namespace

SdpVideo ParseSdpVideo(const std::string& sdp) {
  SdpVideo v;
  std::istringstream in(sdp);
  std::string line;
  bool in_video = false;
  while (std::getline(in, line)) {
    if (!line.empty() && line.back() == '\r') line.pop_back();
    if (line.compare(0, 2, "m=") != 0) {
      if (in_video) ParseVideoAttribute(line, &v);
      continue;
    }
    if (v.found) break;  // only the first video media
    in_video = line.compare(2, 5, "video") == 0;
    if (in_video) {
      v.found = true;
      std::istringstream m(line.substr(2));
      std::string media;
      std::string port;
      std::string proto;
      m >> media >> port >> proto >> v.payload_type;
    }
  }
  return v;
}

std::string ResolveControl(const std::string& base, const std::string& control) {
  if (control.empty() || control == "*") return base;
  if (StartsWithNoCase(control, "rtsp://")) return control;
  if (!base.empty() && base.back() == '/') return base + control;
  return base + "/" + control;
}

void ParseSessionHeader(const std::string& value, std::string* id,
                        int* timeout) {
  size_t semi = value.find(';');
  *id = Trim(value.substr(0, semi));
  *timeout = 0;
  if (semi == std::string::npos) return;
  size_t t = Lower(value).find("timeout=", semi);
  if (t != std::string::npos) *timeout = std::atoi(value.c_str() + t + 8);
}

}  // namespace rtsp
