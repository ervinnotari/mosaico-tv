// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Ervin Notari Junior

// Software H.264 decoder (OpenH264) for the mosaic. The TV plays only one
// native player at a time; the other cameras are decoded here.
#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

class ISVCDecoder;

namespace h264 {

// YUV 4:2:0 with packed planes (stride = plane width).
struct YuvFrame {
  int width = 0;
  int height = 0;
  std::vector<uint8_t> y;
  std::vector<uint8_t> u;
  std::vector<uint8_t> v;
};

class SoftDecoder {
 public:
  SoftDecoder();
  ~SoftDecoder();
  SoftDecoder(const SoftDecoder&) = delete;
  SoftDecoder& operator=(const SoftDecoder&) = delete;

  bool ok() const { return decoder_ != nullptr; }

  // One Annex B access unit. Returns true if a frame came out in `out`.
  bool Decode(const uint8_t* data, size_t size, YuvFrame* out);

 private:
  ISVCDecoder* decoder_ = nullptr;
};

}  // namespace h264
