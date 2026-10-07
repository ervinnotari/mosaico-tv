// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Ervin Notari Junior

#include "h265.hpp"

#include <array>
#include <cstdio>
#include <initializer_list>
#include <utility>

namespace h265 {

namespace {

constexpr std::array<uint8_t, 4> kStartCode = {0, 0, 0, 1};

// NAL unit types (H.265 table 7-1, RFC 7798).
constexpr uint8_t kIrapFirst = 16;  // BLA_W_LP .. CRA_NUT (16-21): key frames
constexpr uint8_t kIrapLast = 21;
constexpr uint8_t kVps = 32;
constexpr uint8_t kSps = 33;
constexpr uint8_t kPps = 34;
constexpr uint8_t kAggregation = 48;
constexpr uint8_t kFragmentation = 49;

uint8_t NalType(const uint8_t* nal) { return (nal[0] >> 1) & 0x3f; }

void AppendNal(std::vector<uint8_t>* out, const uint8_t* nal, size_t size) {
  out->insert(out->end(), kStartCode.begin(), kStartCode.end());
  out->insert(out->end(), nal, nal + size);
}

// Sub-layer part of profile_tier_level(): only skipped.
void SkipSubLayers(media::BitReader& r, uint32_t max_sub_layers_minus1) {
  std::array<bool, 8> sub_profile = {};
  std::array<bool, 8> sub_level = {};
  for (uint32_t i = 0; i < max_sub_layers_minus1; ++i) {
    sub_profile[i] = r.Bit();
    sub_level[i] = r.Bit();
  }
  if (max_sub_layers_minus1 > 0) {
    for (uint32_t i = max_sub_layers_minus1; i < 8; ++i) r.Skip(2);
  }
  for (uint32_t i = 0; i < max_sub_layers_minus1; ++i) {
    if (sub_profile[i]) r.Skip(88);
    if (sub_level[i]) r.Skip(8);
  }
}

const char* ProfileSpace(uint32_t profile_space) {
  switch (profile_space) {
    case 1: return "A";
    case 2: return "B";
    case 3: return "C";
    default: return "";
  }
}

// Codec string (ISO/IEC 14496-15, annex E): hev1.<space><profile>.
// <compatibility with reversed bits>.<L|H><level>.<constraints>.
std::string CodecString(uint32_t profile_space, uint32_t tier, uint32_t profile_idc,
                        uint32_t compat, uint32_t level_idc,
                        const std::array<uint8_t, 6>& constraint) {
  uint32_t reversed = 0;
  for (int i = 0; i < 32; ++i) reversed |= ((compat >> i) & 1u) << (31 - i);
  std::array<char, 64> buf;
  std::snprintf(buf.data(), buf.size(), "hev1.%s%u.%X.%c%u", ProfileSpace(profile_space),
                profile_idc, reversed, tier ? 'H' : 'L', level_idc);
  std::string codecs = buf.data();
  int last = 5;
  while (last >= 0 && constraint[last] == 0) --last;
  for (int i = 0; i <= last; ++i) {
    std::snprintf(buf.data(), buf.size(), ".%X", constraint[i]);
    codecs += buf.data();
  }
  return codecs;
}

}  // namespace

bool ParseSps(const uint8_t* nal, size_t size, SpsInfo* out) {
  if (size < 4 || NalType(nal) != kSps) return false;
  media::BitReader r(media::ToRbsp(nal + 2, size - 2));
  r.Skip(4);  // sps_video_parameter_set_id
  uint32_t max_sub_layers_minus1 = r.Bits(3);
  r.Skip(1);  // sps_temporal_id_nesting_flag

  // profile_tier_level(1, max_sub_layers_minus1)
  uint32_t profile_space = r.Bits(2);
  uint32_t tier = r.Bit();
  uint32_t profile_idc = r.Bits(5);
  uint32_t compat = r.Bits(32);
  std::array<uint8_t, 6> constraint;
  for (auto& b : constraint) b = static_cast<uint8_t>(r.Bits(8));
  uint32_t level_idc = r.Bits(8);
  SkipSubLayers(r, max_sub_layers_minus1);

  r.Ue();  // sps_seq_parameter_set_id
  uint32_t chroma_format_idc = r.Ue();
  if (chroma_format_idc == 3) r.Skip(1);  // separate_colour_plane_flag
  uint32_t width = r.Ue();
  uint32_t height = r.Ue();
  if (r.Bit()) {  // conformance_window_flag
    uint32_t sub_w = chroma_format_idc == 1 || chroma_format_idc == 2 ? 2 : 1;
    uint32_t sub_h = chroma_format_idc == 1 ? 2 : 1;
    uint32_t left = r.Ue();
    uint32_t right = r.Ue();
    uint32_t top = r.Ue();
    uint32_t bottom = r.Ue();
    width -= sub_w * (left + right);
    height -= sub_h * (top + bottom);
  }
  if (!r.ok() || width == 0 || height == 0) return false;

  out->width = static_cast<int>(width);
  out->height = static_cast<int>(height);
  out->codecs = CodecString(profile_space, tier, profile_idc, compat, level_idc, constraint);
  return true;
}

Depacketizer::Depacketizer(int payload_type, Callback on_access_unit)
    : payload_type_(payload_type), on_access_unit_(std::move(on_access_unit)) {}

void Depacketizer::SetParameterSets(const std::vector<uint8_t>& vps,
                                    const std::vector<uint8_t>& sps,
                                    const std::vector<uint8_t>& pps) {
  if (!vps.empty()) vps_ = vps;
  if (!sps.empty()) sps_ = sps;
  if (!pps.empty()) pps_ = pps;
}

bool Depacketizer::Info(media::VideoInfo* out) const {
  SpsInfo s;
  if (sps_.empty() || !ParseSps(sps_.data(), sps_.size(), &s)) return false;
  out->codec = media::Codec::kH265;
  out->width = s.width;
  out->height = s.height;
  out->codecs = s.codecs;
  return true;
}

void Depacketizer::Push(const uint8_t* p, size_t size) {
  media::RtpPacket pkt;
  if (!media::ParseRtp(p, size, &pkt) || pkt.payload_type != payload_type_ || pkt.size < 2) {
    return;
  }

  if (have_seq_ && pkt.seq != next_seq_) {
    lost_packets_ += static_cast<uint16_t>(pkt.seq - next_seq_);
    au_broken_ = true;
    fu_active_ = false;
  }
  have_seq_ = true;
  next_seq_ = static_cast<uint16_t>(pkt.seq + 1);

  if (!au_.data.empty() && pkt.timestamp != au_.rtp_timestamp) Flush();
  au_.rtp_timestamp = pkt.timestamp;

  const uint8_t* payload = pkt.payload;
  size_t len = pkt.size;
  uint8_t type = NalType(payload);

  if (type == kAggregation) {
    AddAggregate(payload, len);
  } else if (type == kFragmentation && len >= 3) {
    AddFragment(payload, len);
  } else if (type < kAggregation) {
    AddNal(payload, len);
  }  // PACI (50) and reserved types: ignored

  if (pkt.marker) Flush();
}

// AP: several NALs, each preceded by a 16-bit size. No DONL
// (sprop-max-don-diff = 0, the camera default).
void Depacketizer::AddAggregate(const uint8_t* payload, size_t len) {
  size_t i = 2;
  while (i + 2 <= len) {
    auto n = static_cast<size_t>(payload[i] << 8 | payload[i + 1]);
    i += 2;
    if (n == 0 || i + n > len) break;
    AddNal(payload + i, n);
    i += n;
  }
}

// FU: one NAL split across packets.
void Depacketizer::AddFragment(const uint8_t* payload, size_t len) {
  bool start = payload[2] & 0x80;
  bool stop = payload[2] & 0x40;
  if (start) {
    fu_.clear();
    // Rebuilds the NAL header with the original type from the FU header.
    fu_.push_back(static_cast<uint8_t>((payload[0] & 0x81) | ((payload[2] & 0x3f) << 1)));
    fu_.push_back(payload[1]);
    fu_active_ = true;
  }
  if (!fu_active_) return;
  fu_.insert(fu_.end(), payload + 3, payload + len);
  if (stop) {
    AddNal(fu_.data(), fu_.size());
    fu_active_ = false;
  }
}

void Depacketizer::AddNal(const uint8_t* nal, size_t size) {
  if (size < 2) return;
  uint8_t type = NalType(nal);
  if (type == kVps) {
    vps_.assign(nal, nal + size);
    au_has_vps_ = true;
  } else if (type == kSps) {
    sps_.assign(nal, nal + size);
    au_has_sps_ = true;
  } else if (type == kPps) {
    pps_.assign(nal, nal + size);
    au_has_pps_ = true;
  } else if (type >= kIrapFirst && type <= kIrapLast) {
    au_.key_frame = true;
  }
  AppendNal(&au_.data, nal, size);
}

void Depacketizer::Flush() {
  if (au_.data.empty() || au_broken_) {
    Drop();
    return;
  }
  // VPS/SPS/PPS before each key frame (Annex B, no hvcC). If any is
  // missing, inserts all three to keep the order; repeats do no harm.
  bool complete = au_has_vps_ && au_has_sps_ && au_has_pps_;
  if (au_.key_frame && !complete && !vps_.empty() && !sps_.empty() && !pps_.empty()) {
    std::vector<uint8_t> prefix;
    for (const auto* nal : {&vps_, &sps_, &pps_}) AppendNal(&prefix, nal->data(), nal->size());
    au_.data.insert(au_.data.begin(), prefix.begin(), prefix.end());
  }
  on_access_unit_(au_);
  Drop();
}

void Depacketizer::Drop() {
  au_.data.clear();
  au_.key_frame = false;
  au_has_vps_ = false;
  au_has_sps_ = false;
  au_has_pps_ = false;
  au_broken_ = false;
}

}  // namespace h265
