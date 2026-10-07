// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Ervin Notari Junior

#include "h264.hpp"

#include <array>
#include <cstdio>
#include <utility>

namespace h264 {

namespace {

constexpr std::array<uint8_t, 4> kStartCode = {0, 0, 0, 1};

// NAL unit types (H.264 table 7-1, RFC 6184).
constexpr uint8_t kNalIdr = 5;
constexpr uint8_t kNalSps = 7;
constexpr uint8_t kNalPps = 8;
constexpr uint8_t kNalStapA = 24;
constexpr uint8_t kNalFuA = 28;

void AppendNal(std::vector<uint8_t>* out, const uint8_t* nal, size_t size) {
  out->insert(out->end(), kStartCode.begin(), kStartCode.end());
  out->insert(out->end(), nal, nal + size);
}

void SkipScalingList(media::BitReader& r, int size) {
  int last = 8;
  int next = 8;
  for (int j = 0; j < size; ++j) {
    if (next != 0) next = (last + r.Se() + 256) % 256;
    last = next == 0 ? last : next;
  }
}

bool IsHighProfile(uint8_t profile_idc) {
  switch (profile_idc) {
    case 100: case 110: case 122: case 244: case 44: case 83:
    case 86: case 118: case 128: case 138: case 139: case 134: case 135:
      return true;
    default:
      return false;
  }
}

// High profiles carry the chroma format, bit depths and scaling matrices;
// returns chroma_format_idc (1, 4:2:0, for the other profiles).
uint32_t ReadChromaInfo(media::BitReader& r, uint8_t profile_idc) {
  if (!IsHighProfile(profile_idc)) return 1;
  uint32_t chroma_format_idc = r.Ue();
  if (chroma_format_idc == 3) r.Bit();  // separate_colour_plane_flag
  r.Ue();   // bit_depth_luma_minus8
  r.Ue();   // bit_depth_chroma_minus8
  r.Bit();  // qpprime_y_zero_transform_bypass_flag
  if (!r.Bit()) return chroma_format_idc;  // seq_scaling_matrix_present_flag
  int count = chroma_format_idc != 3 ? 8 : 12;
  for (int i = 0; i < count; ++i) {
    if (r.Bit()) SkipScalingList(r, i < 6 ? 16 : 64);
  }
  return chroma_format_idc;
}

void SkipPicOrderCount(media::BitReader& r) {
  uint32_t poc_type = r.Ue();
  if (poc_type == 0) {
    r.Ue();  // log2_max_pic_order_cnt_lsb_minus4
  } else if (poc_type == 1) {
    r.Bit();
    r.Se();
    r.Se();
    uint32_t n = r.Ue();
    for (uint32_t i = 0; i < n && r.ok(); ++i) r.Se();
  }
}

}  // namespace

bool ParseSps(const uint8_t* nal, size_t size, SpsInfo* out) {
  if (size < 4 || (nal[0] & 0x1f) != kNalSps) return false;
  media::BitReader r(media::ToRbsp(nal + 1, size - 1));
  SpsInfo s;
  s.profile_idc = static_cast<uint8_t>(r.Bits(8));
  s.constraint_flags = static_cast<uint8_t>(r.Bits(8));
  s.level_idc = static_cast<uint8_t>(r.Bits(8));
  r.Ue();  // seq_parameter_set_id
  uint32_t chroma_format_idc = ReadChromaInfo(r, s.profile_idc);
  r.Ue();  // log2_max_frame_num_minus4
  SkipPicOrderCount(r);
  r.Ue();   // max_num_ref_frames
  r.Bit();  // gaps_in_frame_num_value_allowed_flag
  uint32_t width_mbs = r.Ue() + 1;
  uint32_t height_map_units = r.Ue() + 1;
  uint32_t frame_mbs_only = r.Bit();
  if (!frame_mbs_only) r.Bit();  // mb_adaptive_frame_field_flag
  r.Bit();                       // direct_8x8_inference_flag

  uint32_t crop_l = 0;
  uint32_t crop_r = 0;
  uint32_t crop_t = 0;
  uint32_t crop_b = 0;
  if (r.Bit()) {
    crop_l = r.Ue();
    crop_r = r.Ue();
    crop_t = r.Ue();
    crop_b = r.Ue();
  }
  if (!r.ok()) return false;

  uint32_t sub_w = chroma_format_idc == 3 ? 1 : 2;
  uint32_t sub_h = chroma_format_idc == 1 ? 2 : 1;
  uint32_t crop_unit_x = chroma_format_idc == 0 ? 1 : sub_w;
  uint32_t crop_unit_y = (chroma_format_idc == 0 ? 1 : sub_h) * (2 - frame_mbs_only);

  s.width = static_cast<int>(width_mbs * 16 - crop_unit_x * (crop_l + crop_r));
  s.height = static_cast<int>((2 - frame_mbs_only) * height_map_units * 16 -
                              crop_unit_y * (crop_t + crop_b));
  if (s.width <= 0 || s.height <= 0) return false;
  *out = s;
  return true;
}

Depacketizer::Depacketizer(int payload_type, Callback on_access_unit)
    : payload_type_(payload_type), on_access_unit_(std::move(on_access_unit)) {}

void Depacketizer::SetParameterSets(const std::vector<uint8_t>& sps,
                                    const std::vector<uint8_t>& pps) {
  if (!sps.empty()) sps_ = sps;
  if (!pps.empty()) pps_ = pps;
}

bool Depacketizer::Info(media::VideoInfo* out) const {
  SpsInfo s;
  if (sps_.empty() || !ParseSps(sps_.data(), sps_.size(), &s)) return false;
  std::array<char, 16> codecs;
  std::snprintf(codecs.data(), codecs.size(), "avc1.%02x%02x%02x", s.profile_idc,
                s.constraint_flags, s.level_idc);
  out->codec = media::Codec::kH264;
  out->width = s.width;
  out->height = s.height;
  out->codecs = codecs.data();
  return true;
}

void Depacketizer::Push(const uint8_t* p, size_t size) {
  media::RtpPacket pkt;
  if (!media::ParseRtp(p, size, &pkt) || pkt.payload_type != payload_type_) {
    ++ignored_packets_;
    return;
  }

  if (have_seq_ && pkt.seq != next_seq_) {
    lost_packets_ += static_cast<uint16_t>(pkt.seq - next_seq_);
    au_broken_ = true;  // the current frame is corrupted
    fu_active_ = false;
  }
  have_seq_ = true;
  next_seq_ = static_cast<uint16_t>(pkt.seq + 1);

  if (!au_.data.empty() && pkt.timestamp != au_.rtp_timestamp) Flush();
  au_.rtp_timestamp = pkt.timestamp;

  const uint8_t* payload = pkt.payload;
  size_t len = pkt.size;
  uint8_t type = payload[0] & 0x1f;

  if (type >= 1 && type <= 23) {
    AddNal(payload, len);
  } else if (type == kNalStapA) {
    AddAggregate(payload, len);
  } else if (type == kNalFuA && len >= 2) {
    AddFragment(payload, len);
  } else {
    ++ignored_packets_;  // STAP-B, MTAP, FU-B: rare in cameras
  }

  if (pkt.marker) Flush();
}

// STAP-A: several NALs, each preceded by a 16-bit size.
void Depacketizer::AddAggregate(const uint8_t* payload, size_t len) {
  size_t i = 1;
  while (i + 2 <= len) {
    auto n = static_cast<size_t>(payload[i] << 8 | payload[i + 1]);
    i += 2;
    if (n == 0 || i + n > len) break;
    AddNal(payload + i, n);
    i += n;
  }
}

// FU-A: one NAL split across packets; the header is rebuilt from the FU
// indicator (NRI) and the FU header (type).
void Depacketizer::AddFragment(const uint8_t* payload, size_t len) {
  bool start = payload[1] & 0x80;
  bool stop = payload[1] & 0x40;
  if (start) {
    fu_.clear();
    fu_.push_back(static_cast<uint8_t>((payload[0] & 0xe0) | (payload[1] & 0x1f)));
    fu_active_ = true;
  }
  if (!fu_active_) return;
  fu_.insert(fu_.end(), payload + 2, payload + len);
  if (stop) {
    AddNal(fu_.data(), fu_.size());
    fu_active_ = false;
  }
}

void Depacketizer::AddNal(const uint8_t* nal, size_t size) {
  if (size == 0) return;
  uint8_t type = nal[0] & 0x1f;
  if (type == kNalSps) {
    sps_.assign(nal, nal + size);
    au_has_sps_ = true;
  } else if (type == kNalPps) {
    pps_.assign(nal, nal + size);
    au_has_pps_ = true;
  } else if (type == kNalIdr) {
    au_.key_frame = true;
  }
  AppendNal(&au_.data, nal, size);
}

void Depacketizer::Flush() {
  if (au_.data.empty() || au_broken_) {
    Drop();
    return;
  }
  // The decoder needs SPS/PPS before each IDR (Annex B, no avcC).
  if (au_.key_frame && (!au_has_sps_ || !au_has_pps_) && !sps_.empty() &&
      !pps_.empty()) {
    std::vector<uint8_t> prefix;
    if (!au_has_sps_) AppendNal(&prefix, sps_.data(), sps_.size());
    if (!au_has_pps_) AppendNal(&prefix, pps_.data(), pps_.size());
    au_.data.insert(au_.data.begin(), prefix.begin(), prefix.end());
  }
  on_access_unit_(au_);
  Drop();
}

void Depacketizer::Drop() {
  au_.data.clear();
  au_.key_frame = false;
  au_has_sps_ = false;
  au_has_pps_ = false;
  au_broken_ = false;
}

}  // namespace h264
