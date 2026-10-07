// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Ervin Notari Junior

// ONVIF search (WS-Discovery) over UDP. The TV receives the answers and the JS
// reads the XML; the rest of ONVIF (GetProfiles, GetStreamUri) is SOAP via XHR in JS.

#include <arpa/inet.h>
#include <emscripten.h>
#include <netinet/in.h>
#include <poll.h>
#include <pthread.h>
#include <sys/socket.h>
#include <unistd.h>

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include "events.hpp"

namespace {

using Clock = std::chrono::steady_clock;

std::atomic<bool> g_discovering{false};

struct DiscoveryRequest {
  int timeout_ms;
  std::string sweep_prefix;  // "192.168.1." to probe .1-.254 by unicast
};

std::string ProbeMessage() {
  char uuid[64];
  std::snprintf(uuid, sizeof(uuid), "%08x-%04x-4%03x-a%03x-%04x%08x",
                std::rand(), std::rand() & 0xffff, std::rand() & 0xfff,
                std::rand() & 0xfff, std::rand() & 0xffff, std::rand());
  return std::string(
             "<?xml version=\"1.0\" encoding=\"UTF-8\"?>"
             "<e:Envelope xmlns:e=\"http://www.w3.org/2003/05/soap-envelope\" "
             "xmlns:w=\"http://schemas.xmlsoap.org/ws/2004/08/addressing\" "
             "xmlns:d=\"http://schemas.xmlsoap.org/ws/2005/04/discovery\" "
             "xmlns:dn=\"http://www.onvif.org/ver10/network/wsdl\">"
             "<e:Header><w:MessageID>uuid:") +
         uuid +
         "</w:MessageID>"
         "<w:To e:mustUnderstand=\"true\">urn:schemas-xmlsoap-org:ws:2005:04:discovery</w:To>"
         "<w:Action e:mustUnderstand=\"true\">"
         "http://schemas.xmlsoap.org/ws/2005/04/discovery/Probe</w:Action>"
         "</e:Header><e:Body><d:Probe><d:Types>dn:NetworkVideoTransmitter</d:Types>"
         "</d:Probe></e:Body></e:Envelope>";
}

void Finish(int fd) {
  if (fd >= 0) close(fd);
  g_discovering = false;
  app::Emit(-1, "discovery-done", "{}");
}

void* DiscoveryThread(void* arg) {
  std::unique_ptr<DiscoveryRequest> req(static_cast<DiscoveryRequest*>(arg));
  int fd = socket(AF_INET, SOCK_DGRAM, 0);
  if (fd < 0) {
    app::Log(-1, "discovery", false, std::string("socket UDP: ") + std::strerror(errno));
    Finish(-1);
    return nullptr;
  }

  std::string probe = ProbeMessage();
  sockaddr_in dst{};
  dst.sin_family = AF_INET;
  dst.sin_port = htons(3702);

  inet_pton(AF_INET, "239.255.255.250", &dst.sin_addr);
  for (int i = 0; i < 2; ++i) {
    sendto(fd, probe.data(), probe.size(), 0, reinterpret_cast<sockaddr*>(&dst),
           sizeof(dst));
  }
  // Many Wi-Fi networks filter multicast: also probe each IP of the subnet.
  if (!req->sweep_prefix.empty()) {
    for (int host = 1; host <= 254; ++host) {
      std::string ip = req->sweep_prefix + std::to_string(host);
      if (inet_pton(AF_INET, ip.c_str(), &dst.sin_addr) != 1) break;
      sendto(fd, probe.data(), probe.size(), 0, reinterpret_cast<sockaddr*>(&dst),
             sizeof(dst));
    }
  }

  std::vector<char> buf(65536);
  auto deadline = Clock::now() + std::chrono::milliseconds(req->timeout_ms);
  while (Clock::now() < deadline) {
    int left = static_cast<int>(
        std::chrono::duration_cast<std::chrono::milliseconds>(deadline - Clock::now()).count());
    pollfd p{fd, POLLIN, 0};
    if (poll(&p, 1, std::max(1, left)) <= 0) continue;
    sockaddr_in from{};
    socklen_t from_len = sizeof(from);
    ssize_t n = recvfrom(fd, buf.data(), buf.size(), 0,
                         reinterpret_cast<sockaddr*>(&from), &from_len);
    if (n <= 0) continue;
    char ip[INET_ADDRSTRLEN] = {0};
    inet_ntop(AF_INET, &from.sin_addr, ip, sizeof(ip));
    app::Emit(-1, "discovery-match",
              "{\"from\":" + app::JsonEscape(ip) + ",\"xml\":" +
                  app::JsonEscape(std::string(buf.data(), static_cast<size_t>(n))) + "}");
  }
  Finish(fd);
  return nullptr;
}

}  // namespace

extern "C" {

// Answers arrive as "discovery-match" events and, at the end, "discovery-done".
EMSCRIPTEN_KEEPALIVE int onvif_discover(int timeout_ms, const char* sweep_prefix) {
  if (g_discovering.exchange(true)) return 0;
  auto* req = new DiscoveryRequest{timeout_ms, sweep_prefix ? sweep_prefix : ""};
  pthread_t t;
  if (pthread_create(&t, nullptr, DiscoveryThread, req) != 0) {
    delete req;
    g_discovering = false;
    return 0;
  }
  pthread_detach(t);
  return 1;
}

}  // extern "C"
