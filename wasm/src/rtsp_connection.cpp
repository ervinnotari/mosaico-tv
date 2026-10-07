// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Ervin Notari Junior

#include "rtsp_connection.hpp"

#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cerrno>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace rtsp {

using Clock = std::chrono::steady_clock;

Connection::Connection(const RtspUrl& url, const std::atomic<bool>& stop)
    : url_(url), stop_(stop) {}

Connection::~Connection() {
  if (fd_ >= 0) close(fd_);
}

bool Connection::Connect(app::Error* err) {
  in_addr addr{};
  if (inet_pton(AF_INET, url_.host.c_str(), &addr) != 1) {
    // Name (e.g. DDNS): Tizen resolves it via HostResolverSync. Synchronous, but
    // we are on the network thread.
    addrinfo hints{};
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    addrinfo* result = nullptr;
    int rc = getaddrinfo(url_.host.c_str(), nullptr, &hints, &result);
    if (rc != 0 || !result) {
      *err = {"dns_failed", url_.host};
      return false;
    }
    addr = reinterpret_cast<sockaddr_in*>(result->ai_addr)->sin_addr;
    freeaddrinfo(result);
    char ip[INET_ADDRSTRLEN] = {0};
    inet_ntop(AF_INET, &addr, ip, sizeof(ip));
    resolved_ip_ = ip;
  }
  fd_ = socket(AF_INET, SOCK_STREAM, 0);
  if (fd_ < 0) {
    *err = {"connect_failed", std::strerror(errno)};
    return false;
  }
  sockaddr_in sa{};
  sa.sin_family = AF_INET;
  sa.sin_port = htons(url_.port);
  sa.sin_addr = addr;
  if (connect(fd_, reinterpret_cast<sockaddr*>(&sa), sizeof(sa)) != 0) {
    *err = {"connect_failed", std::strerror(errno)};
    return false;
  }
  return true;
}

bool Connection::Request(const std::string& method, const std::string& uri,
                         const std::string& extra, Response* resp,
                         app::Error* err) {
  for (int attempt = 0; attempt < 2; ++attempt) {
    if (!Send(method, uri, extra, err)) return false;
    if (!ReadResponse(resp, err)) return false;
    if (resp->status != 401 || attempt == 1) break;
    if (!url_.has_credentials()) {
      *err = {"auth_missing", ""};
      return false;
    }
    if (!ParseAuthChallenges(resp->Headers("WWW-Authenticate"), &auth_)) {
      *err = {"auth_unsupported", ""};
      return false;
    }
    has_auth_ = true;
  }
  if (resp->status == 401) {
    *err = {"auth_failed", ""};
    return false;
  }
  if (resp->status != 200) {
    *err = {"rtsp_status", method + " " + std::to_string(resp->status) + " " + resp->reason};
    return false;
  }
  return true;
}

bool Connection::Send(const std::string& method, const std::string& uri,
                      const std::string& extra, app::Error* err) {
  std::string req = method + " " + uri + " RTSP/1.0\r\nCSeq: " +
                    std::to_string(++cseq_) +
                    "\r\nUser-Agent: TizenRtspPlayer/0.3\r\n";
  if (has_auth_) {
    char cnonce[17];
    std::snprintf(cnonce, sizeof(cnonce), "%08x%08x", std::rand(), std::rand());
    req += "Authorization: " +
           BuildAuthorization(auth_, url_.user, url_.password, method, uri,
                              ++nonce_count_, cnonce) +
           "\r\n";
  }
  if (!session_.empty()) req += "Session: " + session_ + "\r\n";
  req += extra + "\r\n";
  size_t off = 0;
  while (off < req.size()) {
    ssize_t n = send(fd_, req.data() + off, req.size() - off, 0);
    if (n <= 0) {
      *err = {"closed_by_camera", std::strerror(errno)};
      return false;
    }
    off += static_cast<size_t>(n);
  }
  return true;
}

int Connection::Fill(int timeout_ms) {
  pollfd p{fd_, POLLIN, 0};
  int pr = poll(&p, 1, timeout_ms);
  if (pr < 0) return -1;
  if (pr == 0) return 0;
  ssize_t n = recv(fd_, recv_buf_.data(), recv_buf_.size(), 0);
  if (n <= 0) return -1;
  if (rx_off_ > (1 << 20)) {
    rx_.erase(rx_.begin(), rx_.begin() + static_cast<long>(rx_off_));
    rx_off_ = 0;
  }
  rx_.insert(rx_.end(), recv_buf_.data(), recv_buf_.data() + n);
  return static_cast<int>(n);
}

bool Connection::TakeResponse(Response* resp) {
  const char* begin = reinterpret_cast<const char*>(data());
  std::string view(begin, available());
  size_t end = view.find("\r\n\r\n");
  if (end == std::string::npos) return false;
  Response r;
  ParseResponseHead(view.substr(0, end), &r);
  size_t total = end + 4 + static_cast<size_t>(r.ContentLength());
  if (view.size() < total) return false;
  r.body = view.substr(end + 4, total - end - 4);
  Consume(total);
  *resp = r;
  return true;
}

bool Connection::ReadResponse(Response* resp, app::Error* err) {
  auto deadline = Clock::now() + std::chrono::seconds(10);
  while (!TakeResponse(resp)) {
    if (stop_) {
      *err = {"interrupted", ""};
      return false;
    }
    if (Clock::now() > deadline) {
      *err = {"timeout", ""};
      return false;
    }
    if (Fill(500) < 0) {
      *err = {"closed_by_camera", ""};
      return false;
    }
  }
  return true;
}

}  // namespace rtsp
