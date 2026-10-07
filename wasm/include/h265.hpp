// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Ervin Notari Junior

// RTP H.265/HEVC (RFC 7798) -> Annex B access units, and SPS parsing.
// Only the TV native player decodes H.265; the software mosaic is H.264.
// Platform independent (compiles natively for tests).
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "video.hpp"

namespace h265 {

struct SpsInfo {
  int width = 0;
  int height = 0;
  std::string codecs;  // e.g. "hev1.1.6.L120.90"
};

// `nal` starts at the NAL header (2 bytes, no start code).
bool ParseSps(const uint8_t* nal, size_t size, SpsInfo* out);

class Depacketizer : public media::Depacketizer {
 public:
  Depacketizer(int payload_type, Callback on_access_unit);

  // Parameters from the SDP (sprop-vps/sps/pps); in-band ones replace them.
  void SetParameterSets(const std::vector<uint8_t>& vps, const std::vector<uint8_t>& sps,
                        const std::vector<uint8_t>& pps);

  void Push(const uint8_t* packet, size_t size) override;
  bool Info(media::VideoInfo* out) const override;
  uint32_t lost_packets() const override { return lost_packets_; }

 private:
  void AddNal(const uint8_t* nal, size_t size);
  void AddAggregate(const uint8_t* payload, size_t len);
  void AddFragment(const uint8_t* payload, size_t len);
  void Flush();
  void Drop();

  int payload_type_;
  Callback on_access_unit_;
  std::vector<uint8_t> vps_;
  std::vector<uint8_t> sps_;
  std::vector<uint8_t> pps_;

  media::AccessUnit au_;
  bool au_has_vps_ = false;
  bool au_has_sps_ = false;
  bool au_has_pps_ = false;
  bool au_broken_ = false;
  std::vector<uint8_t> fu_;
  bool fu_active_ = false;

  bool have_seq_ = false;
  uint16_t next_seq_ = 0;
  uint32_t lost_packets_ = 0;
};

}  // namespace h265
