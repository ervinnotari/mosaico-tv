// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Ervin Notari Junior

// Events for the JS: Module.onEvent(slot, kind, json). Slot -1 is global.
#pragma once

#include <string>

namespace app {

std::string JsonEscape(const std::string& s);

// Synchronous: works on the main thread and on pthreads.
void Emit(int slot, const char* kind, const std::string& json);

// {"ok":..,"text":..} with the password of RTSP URLs removed. For diagnostics
// (console); what the user sees is LogError.
void Log(int slot, const char* kind, bool ok, const std::string& text);

// Error for the user: {"ok":false,"code":..,"text":..}. The JS translates the
// code (i18n: err.<code>); "text" is the technical detail, in English, that
// may go into the message (host, RTSP status, system error).
void LogError(int slot, const char* kind, const std::string& code,
              const std::string& detail = "");

// User error with code and detail, passed between the layers.
struct Error {
  std::string code;
  std::string detail;
};

}  // namespace app
