// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Ervin Notari Junior

#include "h264.hpp"

#include <cstdio>
#include <utility>

namespace h264 {

namespace {

const uint8_t kStartCode[4] = {0, 0, 0, 1};

enum NalType : uint8_t {
  kNalIdr = 5,
  kNalSps = 7,
  kNalPps = 8,
  kNalStapA = 24,
  kNalFuA = 28,
};

void SkipScalingList(media::BitReader& r, int size) {
  int last = 8, next = 8;
  for (int j = 0; j < size; ++j) {
    if (next != 0) next = (last + r.Se() + 256) % 256;
    last = next == 0 ? last : next;
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

  uint32_t chroma_format_idc = 1;
  switch (s.profile_idc) {
    case 100: case 110: case 122: case 244: case 44: case 83:
    case 86: case 118: case 128: case 138: case 139: case 134: case 135: {
      chroma_format_idc = r.Ue();
      if (chroma_format_idc == 3) r.Bit();  // separate_colour_plane_flag
      r.Ue();   // bit_depth_luma_minus8
      r.Ue();   // bit_depth_chroma_minus8
      r.Bit();  // qpprime_y_zero_transform_bypass_flag
      if (r.Bit()) {  // seq_scaling_matrix_present_flag
        int count = chroma_format_idc != 3 ? 8 : 12;
        for (int i = 0; i < count; ++i) {
          if (r.Bit()) SkipScalingList(r, i < 6 ? 16 : 64);
        }
      }
      break;
    }
    default:
      break;
  }

  r.Ue();  // log2_max_frame_num_minus4
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
  r.Ue();   // max_num_ref_frames
  r.Bit();  // gaps_in_frame_num_value_allowed_flag
  uint32_t width_mbs = r.Ue() + 1;
  uint32_t height_map_units = r.Ue() + 1;
  uint32_t frame_mbs_only = r.Bit();
  if (!frame_mbs_only) r.Bit();  // mb_adaptive_frame_field_flag
  r.Bit();                       // direct_8x8_inference_flag

  uint32_t crop_l = 0, crop_r = 0, crop_t = 0, crop_b = 0;
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
  char codecs[16];
  std::snprintf(codecs, sizeof(codecs), "avc1.%02x%02x%02x", s.profile_idc,
                s.constraint_flags, s.level_idc);
  out->codec = media::Codec::kH264;
  out->width = s.width;
  out->height = s.height;
  out->codecs = codecs;
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
    size_t i = 1;
    while (i + 2 <= len) {
      size_t n = static_cast<size_t>(payload[i] << 8 | payload[i + 1]);
      i += 2;
      if (n == 0 || i + n > len) break;
      AddNal(payload + i, n);
      i += n;
    }
  } else if (type == kNalFuA && len >= 2) {
    bool start = payload[1] & 0x80;
    bool stop = payload[1] & 0x40;
    if (start) {
      fu_.clear();
      fu_.push_back(static_cast<uint8_t>((payload[0] & 0xe0) | (payload[1] & 0x1f)));
      fu_active_ = true;
    }
    if (fu_active_) {
      fu_.insert(fu_.end(), payload + 2, payload + len);
      if (stop) {
        AddNal(fu_.data(), fu_.size());
        fu_active_ = false;
      }
    }
  } else {
    ++ignored_packets_;  // STAP-B, MTAP, FU-B: rare in cameras
  }

  if (pkt.marker) Flush();
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
  au_.data.insert(au_.data.end(), kStartCode, kStartCode + 4);
  au_.data.insert(au_.data.end(), nal, nal + size);
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
    if (!au_has_sps_) {
      prefix.insert(prefix.end(), kStartCode, kStartCode + 4);
      prefix.insert(prefix.end(), sps_.begin(), sps_.end());
    }
    if (!au_has_pps_) {
      prefix.insert(prefix.end(), kStartCode, kStartCode + 4);
      prefix.insert(prefix.end(), pps_.begin(), pps_.end());
    }
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
