// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Ervin Notari Junior

// RTP H.264 (RFC 6184) -> Annex B access units, and SPS parsing.
// Platform independent (compiles natively for tests).
#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "video.hpp"

namespace h264 {

using AccessUnit = media::AccessUnit;

struct SpsInfo {
  int width = 0;
  int height = 0;
  uint8_t profile_idc = 0;
  uint8_t constraint_flags = 0;
  uint8_t level_idc = 0;
};

// `nal` starts at the NAL header (no start code).
bool ParseSps(const uint8_t* nal, size_t size, SpsInfo* out);

class Depacketizer : public media::Depacketizer {
 public:
  Depacketizer(int payload_type, Callback on_access_unit);

  // Parameters from the SDP; in-band SPS/PPS NALs replace these.
  void SetParameterSets(const std::vector<uint8_t>& sps,
                        const std::vector<uint8_t>& pps);

  void Push(const uint8_t* packet, size_t size) override;
  bool Info(media::VideoInfo* out) const override;
  uint32_t lost_packets() const override { return lost_packets_; }

  const std::vector<uint8_t>& sps() const { return sps_; }
  const std::vector<uint8_t>& pps() const { return pps_; }
  uint32_t ignored_packets() const { return ignored_packets_; }

 private:
  void AddNal(const uint8_t* nal, size_t size);
  void Flush();
  void Drop();

  int payload_type_;
  Callback on_access_unit_;
  std::vector<uint8_t> sps_;
  std::vector<uint8_t> pps_;

  AccessUnit au_;
  bool au_has_sps_ = false;
  bool au_has_pps_ = false;
  bool au_broken_ = false;
  std::vector<uint8_t> fu_;
  bool fu_active_ = false;

  bool have_seq_ = false;
  uint16_t next_seq_ = 0;
  uint32_t lost_packets_ = 0;
  uint32_t ignored_packets_ = 0;
};

}  // namespace h264
