// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Ervin Notari Junior

#include "events.hpp"

#include <emscripten.h>

#include <cstdio>

#include "rtsp_url.hpp"

namespace app {

std::string JsonEscape(const std::string& s) {
  std::string out = "\"";
  for (unsigned char c : s) {
    switch (c) {
      case '"': out += "\\\""; break;
      case '\\': out += "\\\\"; break;
      case '\n': out += "\\n"; break;
      case '\r': out += "\\r"; break;
      case '\t': out += "\\t"; break;
      default:
        if (c < 0x20) {
          char buf[8];
          std::snprintf(buf, sizeof(buf), "\\u%04x", c);
          out += buf;
        } else {
          out.push_back(static_cast<char>(c));
        }
    }
  }
  return out + "\"";
}

void Emit(int slot, const char* kind, const std::string& json) {
  MAIN_THREAD_EM_ASM(
      {
        if (Module.onEvent) Module.onEvent($0, UTF8ToString($1), UTF8ToString($2));
      },
      slot, kind, json.c_str());
}

void Log(int slot, const char* kind, bool ok, const std::string& text) {
  Emit(slot, kind, "{\"ok\":" + std::string(ok ? "true" : "false") +
                       ",\"text\":" + JsonEscape(rtsp::SanitizeForLog(text)) + "}");
}

void LogError(int slot, const char* kind, const std::string& code,
              const std::string& detail) {
  Emit(slot, kind, "{\"ok\":false,\"code\":" + JsonEscape(code) +
                       ",\"text\":" + JsonEscape(rtsp::SanitizeForLog(detail)) + "}");
}

}  // namespace app
