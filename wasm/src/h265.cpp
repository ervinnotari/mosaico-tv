// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Ervin Notari Junior

#include "h265.hpp"

#include <cstdio>
#include <initializer_list>
#include <utility>

namespace h265 {

namespace {

const uint8_t kStartCode[4] = {0, 0, 0, 1};

enum NalType : uint8_t {
  kIrapFirst = 16,  // BLA_W_LP .. CRA_NUT (16-21): key frames
  kIrapLast = 21,
  kVps = 32,
  kSps = 33,
  kPps = 34,
  kAggregation = 48,
  kFragmentation = 49,
};

uint8_t NalType(const uint8_t* nal) { return (nal[0] >> 1) & 0x3f; }

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
  uint8_t constraint[6];
  for (auto& b : constraint) b = static_cast<uint8_t>(r.Bits(8));
  uint32_t level_idc = r.Bits(8);
  bool sub_profile[8] = {}, sub_level[8] = {};
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

  r.Ue();  // sps_seq_parameter_set_id
  uint32_t chroma_format_idc = r.Ue();
  if (chroma_format_idc == 3) r.Skip(1);  // separate_colour_plane_flag
  uint32_t width = r.Ue();
  uint32_t height = r.Ue();
  if (r.Bit()) {  // conformance_window_flag
    uint32_t sub_w = chroma_format_idc == 1 || chroma_format_idc == 2 ? 2 : 1;
    uint32_t sub_h = chroma_format_idc == 1 ? 2 : 1;
    uint32_t left = r.Ue(), right = r.Ue(), top = r.Ue(), bottom = r.Ue();
    width -= sub_w * (left + right);
    height -= sub_h * (top + bottom);
  }
  if (!r.ok() || width == 0 || height == 0) return false;

  // Codec string (ISO/IEC 14496-15, annex E): hev1.<space><profile>.
  // <compatibility with reversed bits>.<L|H><level>.<constraints>.
  uint32_t reversed = 0;
  for (int i = 0; i < 32; ++i) reversed |= ((compat >> i) & 1u) << (31 - i);
  char buf[64];
  const char* space = profile_space == 1 ? "A" : profile_space == 2 ? "B" : profile_space == 3 ? "C" : "";
  std::snprintf(buf, sizeof(buf), "hev1.%s%u.%X.%c%u", space, profile_idc, reversed,
                tier ? 'H' : 'L', level_idc);
  std::string codecs = buf;
  int last = 5;
  while (last >= 0 && constraint[last] == 0) --last;
  for (int i = 0; i <= last; ++i) {
    std::snprintf(buf, sizeof(buf), ".%X", constraint[i]);
    codecs += buf;
  }

  out->width = static_cast<int>(width);
  out->height = static_cast<int>(height);
  out->codecs = codecs;
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
    // No DONL (sprop-max-don-diff = 0, the camera default).
    size_t i = 2;
    while (i + 2 <= len) {
      size_t n = static_cast<size_t>(payload[i] << 8 | payload[i + 1]);
      i += 2;
      if (n == 0 || i + n > len) break;
      AddNal(payload + i, n);
      i += n;
    }
  } else if (type == kFragmentation && len >= 3) {
    bool start = payload[2] & 0x80;
    bool stop = payload[2] & 0x40;
    if (start) {
      fu_.clear();
      // Rebuilds the NAL header with the original type from the FU header.
      fu_.push_back(static_cast<uint8_t>((payload[0] & 0x81) | ((payload[2] & 0x3f) << 1)));
      fu_.push_back(payload[1]);
      fu_active_ = true;
    }
    if (fu_active_) {
      fu_.insert(fu_.end(), payload + 3, payload + len);
      if (stop) {
        AddNal(fu_.data(), fu_.size());
        fu_active_ = false;
      }
    }
  } else if (type < kAggregation) {
    AddNal(payload, len);
  }  // PACI (50) and reserved types: ignored

  if (pkt.marker) Flush();
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
  au_.data.insert(au_.data.end(), kStartCode, kStartCode + 4);
  au_.data.insert(au_.data.end(), nal, nal + size);
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
    for (const auto* nal : {&vps_, &sps_, &pps_}) {
      prefix.insert(prefix.end(), kStartCode, kStartCode + 4);
      prefix.insert(prefix.end(), nal->begin(), nal->end());
    }
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
