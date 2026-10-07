// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Ervin Notari Junior

// Parts shared by the video codecs (H.264 and H.265): access unit, RTP
// header and depacketizer interface. Platform independent.
#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <utility>
#include <vector>

namespace media {

enum class Codec { kH264, kH265 };

struct AccessUnit {
  std::vector<uint8_t> data;  // NALs with start code 00 00 00 01
  uint32_t rtp_timestamp = 0;
  bool key_frame = false;
};

// Data read from the stream parameters (SPS).
struct VideoInfo {
  Codec codec = Codec::kH264;
  int width = 0;
  int height = 0;
  std::string codecs;  // value of codecs= in the MIME type, e.g. "avc1.4d001f"
};

struct RtpPacket {
  const uint8_t* payload = nullptr;
  size_t size = 0;
  bool marker = false;
  uint16_t seq = 0;
  uint32_t timestamp = 0;
  int payload_type = 0;
};

// Returns false if it is not valid RTP v2. Removes CSRC, extension and padding.
bool ParseRtp(const uint8_t* p, size_t size, RtpPacket* out);

// Removes the emulation prevention bytes (00 00 03).
std::vector<uint8_t> ToRbsp(const uint8_t* nal, size_t size);

// Bit reader over RBSP, with Exp-Golomb.
class BitReader {
 public:
  explicit BitReader(std::vector<uint8_t> data) : data_(std::move(data)) {}
  bool ok() const { return pos_ <= data_.size() * 8; }
  uint32_t Bit();
  uint32_t Bits(int n);
  void Skip(int n);
  uint32_t Ue();
  int32_t Se();

 private:
  std::vector<uint8_t> data_;
  size_t pos_ = 0;
};

class Depacketizer {
 public:
  using Callback = std::function<void(AccessUnit&)>;
  virtual ~Depacketizer() = default;

  // One complete RTP packet (header included).
  virtual void Push(const uint8_t* packet, size_t size) = 0;

  // Resolution and codec from the current SPS (SDP or in-band).
  virtual bool Info(VideoInfo* out) const = 0;

  virtual uint32_t lost_packets() const = 0;
};

}  // namespace media
