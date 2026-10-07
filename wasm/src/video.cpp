// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Ervin Notari Junior

#include "video.hpp"

namespace media {

bool ParseRtp(const uint8_t* p, size_t size, RtpPacket* out) {
  if (size < 12 || (p[0] >> 6) != 2) return false;
  bool padding = p[0] & 0x20;
  bool extension = p[0] & 0x10;
  size_t csrc_count = p[0] & 0x0f;
  out->marker = p[1] & 0x80;
  out->payload_type = p[1] & 0x7f;
  out->seq = static_cast<uint16_t>(p[2] << 8 | p[3]);
  out->timestamp = static_cast<uint32_t>(p[4]) << 24 | static_cast<uint32_t>(p[5]) << 16 |
                   static_cast<uint32_t>(p[6]) << 8 | p[7];

  size_t off = 12 + csrc_count * 4;
  if (extension) {
    if (off + 4 > size) return false;
    size_t ext_words = static_cast<size_t>(p[off + 2] << 8 | p[off + 3]);
    off += 4 + ext_words * 4;
  }
  size_t end = size;
  if (padding && end > off) end -= p[end - 1];
  if (off >= end) return false;
  out->payload = p + off;
  out->size = end - off;
  return true;
}

std::vector<uint8_t> ToRbsp(const uint8_t* nal, size_t size) {
  std::vector<uint8_t> out;
  out.reserve(size);
  int zeros = 0;
  for (size_t i = 0; i < size; ++i) {
    if (zeros >= 2 && nal[i] == 3) {
      zeros = 0;
      continue;
    }
    zeros = nal[i] == 0 ? zeros + 1 : 0;
    out.push_back(nal[i]);
  }
  return out;
}

uint32_t BitReader::Bit() {
  if (pos_ >= data_.size() * 8) {
    pos_ = data_.size() * 8 + 1;  // marks an error
    return 0;
  }
  uint32_t b = (data_[pos_ / 8] >> (7 - pos_ % 8)) & 1;
  ++pos_;
  return b;
}

uint32_t BitReader::Bits(int n) {
  uint32_t v = 0;
  for (int i = 0; i < n; ++i) v = (v << 1) | Bit();
  return v;
}

void BitReader::Skip(int n) {
  for (int i = 0; i < n; ++i) Bit();
}

uint32_t BitReader::Ue() {
  int zeros = 0;
  while (Bit() == 0) {
    if (++zeros > 31 || !ok()) return 0;
  }
  return ((1u << zeros) - 1) + Bits(zeros);
}

int32_t BitReader::Se() {
  uint32_t v = Ue();
  return (v & 1) ? static_cast<int32_t>((v + 1) / 2) : -static_cast<int32_t>(v / 2);
}

}  // namespace media
