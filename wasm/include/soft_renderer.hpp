// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Ervin Notari Junior

// Draws the software-decoded frames on a single 1920x1080
// <canvas id="canvas"> via WebGL, one rectangle per slot.
#pragma once

#include "soft_decoder.hpp"

namespace render {

constexpr int kMaxSlots = 16;

// Main thread.
bool Init();
void SetRect(int slot, int x, int y, int w, int h);
void ClearAll();
void ClearSlot(int slot);

// Any thread: hands over the latest frame of the slot. The content of
// `frame` is swapped with the old buffer (no copy).
void Publish(int slot, h264::YuvFrame* frame);

// Any thread: discards the pending frame (camera stopped).
void Drop(int slot);

}  // namespace render
