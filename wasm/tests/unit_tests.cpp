// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Ervin Notari Junior

// Tests of the network-free parts. Builds with em++ and runs on Node:
//   scripts\test-wasm.bat
#include <algorithm>
#include <cstdio>
#include <string>
#include <vector>

#include "h264.hpp"
#include "h265.hpp"
#include "md5.hpp"
#include "rtsp_protocol.hpp"
#include "rtsp_url.hpp"

namespace {

int g_failures = 0;

#define EXPECT(cond)                                                  \
  do {                                                                \
    if (!(cond)) {                                                    \
      std::printf("FAILED %s:%d: %s\n", __FILE__, __LINE__, #cond);   \
      ++g_failures;                                                   \
    }                                                                 \
  } while (0)

std::vector<uint8_t> Hex(const char* s) {
  std::vector<uint8_t> out;
  while (s[0] && s[1]) {
    if (s[0] == ' ') { ++s; continue; }
    out.push_back(static_cast<uint8_t>(std::stoi(std::string(s, 2), nullptr, 16)));
    s += 2;
  }
  return out;
}

void TestMd5() {
  EXPECT(rtsp::Md5Hex("") == "d41d8cd98f00b204e9800998ecf8427e");
  EXPECT(rtsp::Md5Hex("abc") == "900150983cd24fb0d6963f7d28e17f72");
  EXPECT(rtsp::Md5Hex("The quick brown fox jumps over the lazy dog") ==
         "9e107d9d372bb6826bd81d3542a419d6");
  EXPECT(rtsp::Md5Hex(std::string(64, 'a')) == "014842d480b571495a4a0363793f7367");
}

void TestDigest() {
  // Example from RFC 2617, section 3.5.
  rtsp::AuthChallenge c;
  EXPECT(rtsp::ParseAuthChallenges(
      {"Basic realm=\"x\"",
       "Digest realm=\"testrealm@host.com\", qop=\"auth,auth-int\", "
       "nonce=\"dcd98b7102dd2f0e8b11d0f600bfb0c093\", "
       "opaque=\"5ccc069c403ebaf9f0171e9517f40e41\""},
      &c));
  EXPECT(c.digest);
  EXPECT(c.realm == "testrealm@host.com");
  std::string auth = rtsp::BuildAuthorization(c, "Mufasa", "Circle Of Life", "GET",
                                              "/dir/index.html", 1, "0a4f113b");
  EXPECT(auth.find("response=\"6629fae49393a05397450978507c4ef1\"") != std::string::npos);
  EXPECT(auth.find("nc=00000001") != std::string::npos);

  // Challenge from the test camera (no qop).
  rtsp::AuthChallenge h;
  EXPECT(rtsp::ParseAuthChallenges(
      {"Digest realm=\"65ff48a2f39ba638e0acf3b7\", nonce=\"17e3a6a72\", algorithm=\"MD5\""}, &h));
  std::string ha1 = rtsp::Md5Hex("admin:65ff48a2f39ba638e0acf3b7:pw");
  std::string ha2 = rtsp::Md5Hex("DESCRIBE:rtsp://cam/x");
  std::string expected = rtsp::Md5Hex(ha1 + ":17e3a6a72:" + ha2);
  auth = rtsp::BuildAuthorization(h, "admin", "pw", "DESCRIBE", "rtsp://cam/x", 1, "c");
  EXPECT(auth.find("response=\"" + expected + "\"") != std::string::npos);
  EXPECT(auth.find("qop") == std::string::npos);

  rtsp::AuthChallenge b;
  EXPECT(rtsp::ParseAuthChallenges({"Basic realm=\"cam\""}, &b));
  EXPECT(rtsp::BuildAuthorization(b, "Aladdin", "open sesame", "DESCRIBE", "u", 1, "") ==
         "Basic QWxhZGRpbjpvcGVuIHNlc2FtZQ==");
}

void TestResponseAndSdp() {
  rtsp::Response r;
  EXPECT(rtsp::ParseResponseHead(
      "RTSP/1.0 200 OK\r\nCSeq: 3\r\nContent-Base: rtsp://1.2.3.4/ch/\r\n"
      "Content-Length: 10\r\nSession: 1234ABCD;timeout=60",
      &r));
  EXPECT(r.status == 200);
  EXPECT(r.Header("content-base") == "rtsp://1.2.3.4/ch/");
  EXPECT(r.ContentLength() == 10);
  std::string id;
  int timeout;
  rtsp::ParseSessionHeader(r.Header("Session"), &id, &timeout);
  EXPECT(id == "1234ABCD");
  EXPECT(timeout == 60);

  const char* sdp =
      "v=0\r\no=- 1 1 IN IP4 0.0.0.0\r\ns=Media\r\nt=0 0\r\n"
      "a=control:*\r\n"
      "m=video 0 RTP/AVP 96\r\n"
      "a=rtpmap:96 H264/90000\r\n"
      "a=fmtp:96 profile-level-id=420029; packetization-mode=1; "
      "sprop-parameter-sets=Z2QAKKzZQHgCJ+XARAAAAwAEAAADAPA8YMZY,aOvjyyLA\r\n"
      "a=control:trackID=1\r\n"
      "m=audio 0 RTP/AVP 0\r\na=control:trackID=2\r\n";
  rtsp::SdpVideo v = rtsp::ParseSdpVideo(sdp);
  EXPECT(v.found);
  EXPECT(v.payload_type == 96);
  EXPECT(v.codec == "H264");
  EXPECT(v.clock_rate == 90000);
  EXPECT(v.control == "trackID=1");
  EXPECT(v.sps.size() > 4 && (v.sps[0] & 0x1f) == 7);
  EXPECT(v.pps.size() > 1 && (v.pps[0] & 0x1f) == 8);
  EXPECT(rtsp::ResolveControl("rtsp://1.2.3.4/ch/", "trackID=1") == "rtsp://1.2.3.4/ch/trackID=1");
  EXPECT(rtsp::ResolveControl("rtsp://1.2.3.4/ch", "trackID=1") == "rtsp://1.2.3.4/ch/trackID=1");
  EXPECT(rtsp::ResolveControl("rtsp://a/b", "rtsp://c/d") == "rtsp://c/d");

  h264::SpsInfo s;
  EXPECT(h264::ParseSps(v.sps.data(), v.sps.size(), &s));
  EXPECT(s.width == 1920);
  EXPECT(s.height == 1080);
  EXPECT(s.profile_idc == 100);
  EXPECT(s.level_idc == 40);
}

std::vector<uint8_t> Rtp(uint16_t seq, uint32_t ts, bool marker,
                         const std::vector<uint8_t>& payload) {
  std::vector<uint8_t> p = {0x80, static_cast<uint8_t>((marker ? 0x80 : 0) | 96),
                            static_cast<uint8_t>(seq >> 8), static_cast<uint8_t>(seq),
                            static_cast<uint8_t>(ts >> 24), static_cast<uint8_t>(ts >> 16),
                            static_cast<uint8_t>(ts >> 8), static_cast<uint8_t>(ts),
                            1, 2, 3, 4};
  p.insert(p.end(), payload.begin(), payload.end());
  return p;
}

void TestDepacketizer() {
  std::vector<h264::AccessUnit> aus;
  h264::Depacketizer d(96, [&](h264::AccessUnit& au) { aus.push_back(au); });
  d.SetParameterSets(Hex("6764"), Hex("68ee"));

  // IDR (0x65 aa bb cc dd) in a 2-piece FU-A: indicator 0x7c, header S/E|5.
  auto p1 = Rtp(65535, 1000, false, Hex("7c85aabb"));
  auto p2 = Rtp(0, 1000, true, Hex("7c45ccdd"));
  d.Push(p1.data(), p1.size());
  d.Push(p2.data(), p2.size());
  EXPECT(aus.size() == 1);
  if (aus.size() == 1) {
    EXPECT(aus[0].key_frame);
    EXPECT(aus[0].data == Hex("00000001 6764 00000001 68ee 00000001 65aabbccdd"));
  }

  // STAP-A with SEI + P-slice, no marker: closes when the timestamp changes.
  auto p3 = Rtp(1, 4000, false, Hex("18 0002 0601 0003 41aabb"));
  auto p4 = Rtp(2, 7000, true, Hex("41cc"));
  d.Push(p3.data(), p3.size());
  d.Push(p4.data(), p4.size());
  EXPECT(aus.size() == 3);
  if (aus.size() == 3) {
    EXPECT(!aus[1].key_frame);
    EXPECT(aus[1].data == Hex("00000001 0601 00000001 41aabb"));
    EXPECT(aus[1].rtp_timestamp == 4000);
    EXPECT(aus[2].data == Hex("00000001 41cc"));
  }
  EXPECT(d.lost_packets() == 0);

  // Lost packet: the incomplete frame is dropped.
  auto p5 = Rtp(4, 10000, false, Hex("7c81aa"));
  auto p6 = Rtp(5, 10000, true, Hex("7c41bb"));
  d.Push(p5.data(), p5.size());
  d.Push(p6.data(), p6.size());
  EXPECT(d.lost_packets() == 1);
  EXPECT(aus.size() == 3);
}

void TestH265() {
  // SPS produced by x265 (ffmpeg testsrc2): 1920x1080 (1088 with a conformance
  // window) level 4 and 640x360 level 2.1.
  auto sps1080 = Hex("420101016000000300900000030000030078a003c08010e596566924caf0168080000003008000000c84");
  auto sps360 = Hex("42010101600000030090000003000003003fa00502016965959a4932bc05a02000000300200000030321");
  h265::SpsInfo s;
  EXPECT(h265::ParseSps(sps1080.data(), sps1080.size(), &s));
  EXPECT(s.width == 1920);
  EXPECT(s.height == 1080);
  EXPECT(s.codecs == "hev1.1.6.L120.90");
  EXPECT(h265::ParseSps(sps360.data(), sps360.size(), &s));
  EXPECT(s.width == 640);
  EXPECT(s.height == 360);
  EXPECT(s.codecs == "hev1.1.6.L63.90");

  const char* sdp =
      "v=0\r\nm=video 0 RTP/AVP 96\r\na=rtpmap:96 H265/90000\r\n"
      "a=fmtp:96 sprop-vps=QAEMAf//AWAAAAMAkAAAAwAAAwB4lZgJ; "
      "sprop-sps=QgEBAWAAAAMAkAAAAwAAAwB4oAPAgBDlllZpJMrwFoCAAAADAIAAAAyE; sprop-pps=RAHBcrRiQA==\r\n"
      "a=control:trackID=1\r\n";
  rtsp::SdpVideo v = rtsp::ParseSdpVideo(sdp);
  EXPECT(v.codec == "H265");
  EXPECT(v.vps.size() > 2 && ((v.vps[0] >> 1) & 0x3f) == 32);
  EXPECT(v.sps == sps1080);
  EXPECT(v.pps.size() > 2 && ((v.pps[0] >> 1) & 0x3f) == 34);

  std::vector<h264::AccessUnit> aus;
  h265::Depacketizer d(96, [&](media::AccessUnit& au) { aus.push_back(au); });
  d.SetParameterSets(v.vps, v.sps, v.pps);
  media::VideoInfo info;
  EXPECT(d.Info(&info));
  EXPECT(info.codecs == "hev1.1.6.L120.90" && info.width == 1920);

  // IDR_W_RADL (19) in FU (49): header 62 01, FU header S|19, E|19.
  auto p1 = Rtp(10, 9000, false, Hex("6201 93 aabb"));
  auto p2 = Rtp(11, 9000, true, Hex("6201 53 ccdd"));
  d.Push(p1.data(), p1.size());
  d.Push(p2.data(), p2.size());
  EXPECT(aus.size() == 1);
  if (aus.size() == 1) {
    EXPECT(aus[0].key_frame);
    // VPS, SPS, PPS from the SDP and then the reassembled NAL (26 01 = type 19).
    std::vector<uint8_t> tail = Hex("00000001 2601 aabbccdd");
    EXPECT(aus[0].data.size() > tail.size() &&
           std::equal(tail.begin(), tail.end(), aus[0].data.end() - tail.size()));
    std::vector<uint8_t> head = Hex("00000001");
    head.insert(head.end(), v.vps.begin(), v.vps.end());
    EXPECT(std::equal(head.begin(), head.end(), aus[0].data.begin()));
  }

  // AP (48) with two TRAIL_R (1) NALs: 02 01.
  auto p3 = Rtp(12, 12600, true, Hex("6001 0003 0201aa 0003 0201bb"));
  d.Push(p3.data(), p3.size());
  EXPECT(aus.size() == 2);
  if (aus.size() == 2) {
    EXPECT(!aus[1].key_frame);
    EXPECT(aus[1].data == Hex("00000001 0201aa 00000001 0201bb"));
  }
}

void TestUrl() {
  rtsp::RtspUrl u;
  std::string err;
  EXPECT(rtsp::ParseRtspUrl(
      "rtsp://admin:s3cret@192.168.1.19:554/Streaming/Unicast/channels/101", &u, &err));
  EXPECT(u.request_url() == "rtsp://192.168.1.19:554/Streaming/Unicast/channels/101");
  EXPECT(u.sanitized().find("s3cret") == std::string::npos);
}


// Builds NAL units bit by bit (Exp-Golomb included), with the RBSP trailing
// bit and emulation prevention, to exercise the less common SPS fields.
class BitWriter {
 public:
  void Bit(uint32_t b) {
    if (bits_ % 8 == 0) data_.push_back(0);
    if (b) data_.back() |= static_cast<uint8_t>(0x80 >> (bits_ % 8));
    ++bits_;
  }
  void Bits(uint32_t v, int n) {
    for (int i = n - 1; i >= 0; --i) Bit((v >> i) & 1u);
  }
  void Ue(uint32_t v) {
    uint32_t code = v + 1;
    int len = 0;
    while ((code >> len) > 1) ++len;
    Bits(0, len);
    Bits(code, len + 1);
  }
  void Se(int32_t v) { Ue(v > 0 ? static_cast<uint32_t>(2 * v - 1) : static_cast<uint32_t>(-2 * v)); }

  // NAL = header + escaped RBSP with the stop bit.
  std::vector<uint8_t> Nal(const std::vector<uint8_t>& header) {
    Bit(1);
    while (bits_ % 8) Bit(0);
    std::vector<uint8_t> out = header;
    int zeros = 0;
    for (uint8_t b : data_) {
      if (zeros >= 2 && b <= 3) {
        out.push_back(3);
        zeros = 0;
      }
      out.push_back(b);
      zeros = b == 0 ? zeros + 1 : 0;
    }
    return out;
  }

 private:
  std::vector<uint8_t> data_;
  int bits_ = 0;
};

// High profile with scaling matrices and picture order count type 1.
void TestH264HighProfileSps() {
  BitWriter w;
  w.Bits(100, 8);  // profile_idc: High
  w.Bits(0, 8);    // constraint flags
  w.Bits(40, 8);   // level 4.0
  w.Ue(0);         // seq_parameter_set_id
  w.Ue(1);         // chroma_format_idc 4:2:0
  w.Ue(0);         // bit_depth_luma_minus8
  w.Ue(0);         // bit_depth_chroma_minus8
  w.Bit(0);        // qpprime_y_zero_transform_bypass_flag
  w.Bit(1);        // seq_scaling_matrix_present_flag
  for (int i = 0; i < 8; ++i) {
    bool present = i == 0 || i == 6;  // one 4x4 and one 8x8 list
    w.Bit(present);
    if (present) w.Se(-8);  // next_scale becomes 0: the list ends here
  }
  w.Ue(0);    // log2_max_frame_num_minus4
  w.Ue(1);    // pic_order_cnt_type 1
  w.Bit(0);   // delta_pic_order_always_zero_flag
  w.Se(0);    // offset_for_non_ref_pic
  w.Se(0);    // offset_for_top_to_bottom_field
  w.Ue(2);    // num_ref_frames_in_pic_order_cnt_cycle
  w.Se(1);
  w.Se(-1);
  w.Ue(1);    // max_num_ref_frames
  w.Bit(0);   // gaps_in_frame_num_value_allowed_flag
  w.Ue(119);  // pic_width_in_mbs_minus1: 1920
  w.Ue(67);   // pic_height_in_map_units_minus1: 1088
  w.Bit(1);   // frame_mbs_only_flag
  w.Bit(1);   // direct_8x8_inference_flag
  w.Bit(1);   // frame_cropping_flag
  w.Ue(0);
  w.Ue(0);
  w.Ue(0);
  w.Ue(4);    // 8 lines cropped at the bottom: 1080
  w.Bit(0);   // vui_parameters_present_flag
  std::vector<uint8_t> sps = w.Nal({0x67});

  h264::SpsInfo s;
  EXPECT(h264::ParseSps(sps.data(), sps.size(), &s));
  EXPECT(s.width == 1920);
  EXPECT(s.height == 1080);
  EXPECT(s.profile_idc == 100);

  h264::Depacketizer d(96, [](h264::AccessUnit&) {});
  d.SetParameterSets(sps, Hex("68ee"));
  media::VideoInfo info;
  EXPECT(d.Info(&info));
  EXPECT(info.codecs == "avc1.640028");
  EXPECT(info.width == 1920 && info.height == 1080);

  // Not an SPS, or too short.
  EXPECT(!h264::ParseSps(sps.data(), 3, &s));
  auto pps = Hex("68ee3c80");
  EXPECT(!h264::ParseSps(pps.data(), pps.size(), &s));

  // STAP-B (25) is not supported: the packet is counted and ignored.
  auto p = Rtp(1, 100, true, Hex("19 0002 0601"));
  d.Push(p.data(), p.size());
  EXPECT(d.ignored_packets() == 1);
}

// Main 10 in profile space 1, high tier, two sub-layers and a conformance
// window: the less common paths of profile_tier_level and the codec string.
void TestH265SpsVariants() {
  for (uint32_t space = 1; space <= 3; ++space) {
    BitWriter w;
    w.Bits(0, 4);       // sps_video_parameter_set_id
    w.Bits(1, 3);       // sps_max_sub_layers_minus1
    w.Bit(1);           // sps_temporal_id_nesting_flag
    w.Bits(space, 2);   // general_profile_space
    w.Bit(1);           // general_tier_flag: High
    w.Bits(2, 5);       // general_profile_idc: Main 10
    w.Bits(0x20000000, 32);  // compatibility flag 2
    w.Bits(0xb0, 8);    // constraint flags
    for (int i = 0; i < 5; ++i) w.Bits(0, 8);
    w.Bits(150, 8);     // general_level_idc 5.0
    w.Bit(1);           // sub_layer_profile_present_flag[0]
    w.Bit(1);           // sub_layer_level_present_flag[0]
    for (int i = 1; i < 8; ++i) w.Bits(0, 2);  // reserved_zero_2bits
    for (int i = 0; i < 11; ++i) w.Bits(0, 8);  // sub-layer profile (88 bits)
    w.Bits(0, 8);       // sub_layer_level_idc
    w.Ue(0);            // sps_seq_parameter_set_id
    w.Ue(1);            // chroma_format_idc 4:2:0
    w.Ue(3840);         // pic_width_in_luma_samples
    w.Ue(2176);         // pic_height_in_luma_samples
    w.Bit(1);           // conformance_window_flag
    w.Ue(0);
    w.Ue(0);
    w.Ue(0);
    w.Ue(8);            // 16 lines cropped: 2160
    w.Bits(0, 8);       // the rest of the SPS is not read
    std::vector<uint8_t> sps = w.Nal({0x42, 0x01});

    h265::SpsInfo s;
    EXPECT(h265::ParseSps(sps.data(), sps.size(), &s));
    EXPECT(s.width == 3840);
    EXPECT(s.height == 2160);
    const char* letter = space == 1 ? "A" : (space == 2 ? "B" : "C");
    EXPECT(s.codecs == std::string("hev1.") + letter + "2.4.H150.B0");
  }

  // A single NAL (TRAIL_R, type 1) in its own packet.
  std::vector<media::AccessUnit> aus;
  h265::Depacketizer d(96, [&](media::AccessUnit& au) { aus.push_back(au); });
  auto p = Rtp(1, 100, true, Hex("0201 aabb"));
  d.Push(p.data(), p.size());
  EXPECT(aus.size() == 1);
  if (aus.size() == 1) EXPECT(aus[0].data == Hex("00000001 0201aabb"));
}

void TestSdpAndAuthExtras() {
  rtsp::SdpVideo v = rtsp::ParseSdpVideo(
      "v=0\r\nm=video 0 RTP/AVP 97\r\na=rtpmap:97 H264/90000\r\na=framerate:25.0\r\n"
      "a=fmtp:97\r\na=control:rtsp://1.2.3.4/live/track1\r\n");
  EXPECT(v.payload_type == 97);
  EXPECT(v.framerate == 25.0);
  EXPECT(v.sps.empty());
  EXPECT(v.control == "rtsp://1.2.3.4/live/track1");
  // No video media at all.
  EXPECT(!rtsp::ParseSdpVideo("v=0\r\nm=audio 0 RTP/AVP 0\r\n").found);

  rtsp::AuthChallenge c;
  EXPECT(rtsp::ParseAuthChallenges(
      {"Basic realm=\"x\"", "Digest realm=\"cam\", nonce=\"n1\", opaque=\"o\", algorithm=MD5"}, &c));
  EXPECT(c.digest && c.realm == "cam" && c.nonce == "n1" && c.opaque == "o" && c.algorithm == "MD5");
  std::string h = rtsp::BuildAuthorization(c, "u", "p", "DESCRIBE", "rtsp://h/", 1, "");
  EXPECT(h.find("opaque=\"o\"") != std::string::npos);
  EXPECT(h.find("algorithm=MD5") != std::string::npos);
  EXPECT(h.find("qop=") == std::string::npos);
  EXPECT(rtsp::ParseAuthChallenges({"Basic realm=\"x\""}, &c) && !c.digest);
  EXPECT(!rtsp::ParseAuthChallenges({"Negotiate"}, &c));
}

void TestUrlExtras() {
  rtsp::RtspUrl u;
  std::string err;
  // %XX in the credentials; a '%' that is not an escape stays as it is.
  EXPECT(rtsp::ParseRtspUrl("rtsp://user:p%40ss%3A1%zz@cam.local/stream", &u, &err));
  EXPECT(u.user == "user");
  EXPECT(u.password == "p@ss:1%zz");
  EXPECT(u.host == "cam.local");
  EXPECT(u.port == 554);
  EXPECT(!rtsp::ParseRtspUrl("http://cam/stream", &u, &err));
  EXPECT(!rtsp::ParseRtspUrl("rtsp://cam:99999/stream", &u, &err));
}

// RTP with a header extension (one 32-bit word) and padding.
void TestRtpExtension() {
  auto p = Hex("90 60 0001 00000064 01020304 bede 0001 aabbccdd 4142 0002");
  p[0] |= 0x20;  // padding flag: the last byte (2) is the padding length
  media::RtpPacket pkt;
  EXPECT(media::ParseRtp(p.data(), p.size(), &pkt));
  EXPECT(pkt.size == 2 && pkt.payload[0] == 0x41 && pkt.payload[1] == 0x42);
  EXPECT(!media::ParseRtp(p.data(), 14, &pkt));  // extension cut short
}


void TestUrlEdgeCases() {
  rtsp::RtspUrl u;
  std::string err;
  // Upper-case scheme, no path, user without password.
  EXPECT(rtsp::ParseRtspUrl("RTSP://viewer@cam", &u, &err));
  EXPECT(u.host == "cam" && u.path == "/" && u.user == "viewer" && u.password.empty());
  EXPECT(u.has_credentials());
  EXPECT(u.sanitized() == "rtsp://viewer:***@cam:554/");
  // IPv6, with and without port.
  EXPECT(rtsp::ParseRtspUrl("rtsp://[fe80::1]:8554/live?x=1", &u, &err));
  EXPECT(u.host == "fe80::1" && u.port == 8554 && u.path == "/live?x=1");
  EXPECT(u.request_url() == "rtsp://[fe80::1]:8554/live?x=1");
  EXPECT(!u.has_credentials() && u.sanitized() == "rtsp://[fe80::1]:8554/live?x=1");
  EXPECT(rtsp::ParseRtspUrl("rtsp://[::1]/a", &u, &err) && u.port == 554);
  // Errors.
  EXPECT(!rtsp::ParseRtspUrl("rtsp", &u, &err));
  EXPECT(!rtsp::ParseRtspUrl("rtsp://[fe80::1/a", &u, &err) && err.find(']') != std::string::npos);
  EXPECT(!rtsp::ParseRtspUrl("rtsp://[::1]x/a", &u, &err) && err == "invalid character after the host");
  EXPECT(!rtsp::ParseRtspUrl("rtsp://:554/a", &u, &err) && err == "missing host");
  EXPECT(!rtsp::ParseRtspUrl("rtsp://u:p@/a", &u, &err) && err == "missing host");
  EXPECT(!rtsp::ParseRtspUrl("rtsp://cam:0/a", &u, &err) && err == "invalid port");
  EXPECT(!rtsp::ParseRtspUrl("rtsp://cam:12a/a", &u, &err) && err == "invalid port");
  // Percent-decoding: valid escapes (any case), truncated or invalid ones kept.
  EXPECT(rtsp::ParseRtspUrl("rtsp://a%2fb:%4A%6b%G1%4@cam/", &u, &err));
  EXPECT(u.user == "a/b" && u.password == "Jk%G1%4");

  // Passwords removed from any text with URLs; text without them unchanged.
  EXPECT(rtsp::SanitizeForLog("open rtsp://a:b@h/x and http://c:pa@ss@e:80/y done") ==
         "open rtsp://a:***@h/x and http://c:***@e:80/y done");
  EXPECT(rtsp::SanitizeForLog("rtsp://user@host/x rtsp://h:554/x") == "rtsp://user@host/x rtsp://h:554/x");
  EXPECT(rtsp::SanitizeForLog("\"rtsp://u:p@h\"") == "\"rtsp://u:***@h\"");
  EXPECT(rtsp::SanitizeForLog("no url here") == "no url here");
}

void TestResponseEdgeCases() {
  rtsp::Response r;
  EXPECT(!rtsp::ParseResponseHead("", &r));
  EXPECT(!rtsp::ParseResponseHead("HTTP/1.1 200 OK", &r));
  EXPECT(!rtsp::ParseResponseHead("RTSP/1.0", &r));
  EXPECT(!rtsp::ParseResponseHead("RTSP/1.0 abc", &r));
  // Status without reason; a line without ':' is skipped; a blank line ends it.
  EXPECT(rtsp::ParseResponseHead(
      "RTSP/1.0 401\r\nWWW-Authenticate: Basic realm=\"a\"\r\ngarbage\r\n"
      "www-authenticate: Digest realm=\"b\", nonce=\"n\"\r\n\r\nCSeq: 9", &r));
  EXPECT(r.status == 401 && r.reason.empty());
  EXPECT(r.Headers("WWW-Authenticate").size() == 2);
  EXPECT(r.Header("CSeq").empty());
  EXPECT(r.ContentLength() == 0);

  // Base64 (also URL-safe, spaces and padding ignored) both ways.
  EXPECT(rtsp::Base64Encode("a") == "YQ==");
  EXPECT(rtsp::Base64Encode("ab") == "YWI=");
  EXPECT(rtsp::Base64Encode("abc") == "YWJj");
  EXPECT(rtsp::Base64Encode("") == "");
  EXPECT(rtsp::Base64Decode("YW Jj") == std::vector<uint8_t>({'a', 'b', 'c'}));
  EXPECT(rtsp::Base64Decode("-_8=") == std::vector<uint8_t>({0xfb, 0xff}));
  EXPECT(rtsp::Base64Decode("+/8=") == std::vector<uint8_t>({0xfb, 0xff}));

  // Digest parameters: unquoted values, unterminated quote, trailing text.
  rtsp::AuthChallenge c;
  EXPECT(rtsp::ParseAuthChallenges({"Digest realm=cam,  qop=auth, nonce=\"abc"}, &c));
  EXPECT(c.realm == "cam" && c.qop == "auth" && c.nonce == "abc");
  EXPECT(rtsp::ParseAuthChallenges({"Digest realm=\"x\", stale"}, &c) && c.realm == "x");
  EXPECT(rtsp::BuildAuthorization(rtsp::AuthChallenge(), "Aladdin", "open sesame", "OPTIONS",
                                  "rtsp://h/", 1, "") == "Basic QWxhZGRpbjpvcGVuIHNlc2FtZQ==");

  EXPECT(rtsp::ResolveControl("rtsp://h/a/", "") == "rtsp://h/a/");
  EXPECT(rtsp::ResolveControl("rtsp://h/a/", "*") == "rtsp://h/a/");
  EXPECT(rtsp::ResolveControl("", "track1") == "/track1");
  std::string id;
  int timeout = -1;
  rtsp::ParseSessionHeader(" 12AB ", &id, &timeout);
  EXPECT(id == "12AB" && timeout == 0);
  rtsp::ParseSessionHeader("12AB;Timeout=30", &id, &timeout);
  EXPECT(timeout == 30);
  rtsp::ParseSessionHeader("12AB;foo", &id, &timeout);
  EXPECT(timeout == 0);

  // SDP: a second video media is ignored; H.264 with only the SPS.
  rtsp::SdpVideo v = rtsp::ParseSdpVideo(
      "m=video 0 RTP/AVP 96\na=rtpmap:96 H264/90000\n"
      "a=fmtp:96 sprop-parameter-sets=Z0IAHpWoLQSZ\n"
      "a=rtpmap:97 H265/90000\n"
      "m=video 0 RTP/AVP 98\na=rtpmap:98 JPEG/90000\n");
  EXPECT(v.codec == "H264" && !v.sps.empty() && v.pps.empty());
  v = rtsp::ParseSdpVideo("m=video 0 RTP/AVP 26\na=rtpmap:26 JPEG\n");
  EXPECT(v.codec == "JPEG" && v.clock_rate == 90000);
}

void TestVideoEdgeCases() {
  media::RtpPacket pkt;
  auto ok = Hex("80 60 0001 00000064 01020304 41");
  EXPECT(!media::ParseRtp(ok.data(), 11, &pkt));                // too short
  auto v1 = Hex("40 60 0001 00000064 01020304 41");
  EXPECT(!media::ParseRtp(v1.data(), v1.size(), &pkt));         // RTP version 1
  auto csrc = Hex("81 e0 0001 00000064 01020304 0a0b0c0d 41");  // one CSRC, marker
  EXPECT(media::ParseRtp(csrc.data(), csrc.size(), &pkt));
  EXPECT(pkt.size == 1 && pkt.payload[0] == 0x41 && pkt.marker);
  auto empty = Hex("80 60 0001 00000064 01020304");
  EXPECT(!media::ParseRtp(empty.data(), empty.size(), &pkt));   // no payload
  auto pad = Hex("a0 60 0001 00000064 01020304 41 05");          // padding > payload
  EXPECT(!media::ParseRtp(pad.data(), pad.size(), &pkt));

  // Emulation prevention: 00 00 03 xx -> 00 00 xx; a lone 03 stays.
  auto nal = Hex("00 00 03 01 03 00 00 03");
  EXPECT(media::ToRbsp(nal.data(), nal.size()) == Hex("00 00 01 03 00 00"));

  // Reading past the end marks an error; Exp-Golomb signed values.
  media::BitReader r(Hex("a6 42 80"));  // 1 | 010 | 011 | 00100 | 001010 | 0...
  EXPECT(r.Ue() == 0 && r.Ue() == 1 && r.Se() == -1 && r.Se() == 2 && r.Se() == -2);
  EXPECT(r.ok());
  r.Skip(20);
  EXPECT(!r.ok());
  EXPECT(r.Bit() == 0);
  media::BitReader zeros(std::vector<uint8_t>(8, 0));
  EXPECT(zeros.Ue() == 0);  // more than 31 leading zeros: invalid
  media::BitReader short_ue(Hex("00"));
  EXPECT(short_ue.Ue() == 0 && !short_ue.ok());
}

void TestDepacketizerEdgeCases() {
  std::vector<media::AccessUnit> aus;
  h264::Depacketizer d(96, [&](media::AccessUnit& au) { aus.push_back(au); });
  media::VideoInfo info;
  EXPECT(!d.Info(&info));  // no SPS yet
  auto other_pt = Hex("80 61 0001 00000064 01020304 41aa");
  d.Push(other_pt.data(), other_pt.size());
  auto garbage = Hex("00 01 02");
  d.Push(garbage.data(), garbage.size());
  EXPECT(d.ignored_packets() == 2);
  // FU-A end without its start (joined mid-frame): ignored; then a key frame
  // without SPS/PPS known is sent as it is.
  auto mid = Rtp(1, 100, true, Hex("7c45ccdd"));
  d.Push(mid.data(), mid.size());
  auto short_fu = Rtp(2, 200, true, Hex("7c"));
  d.Push(short_fu.data(), short_fu.size());
  auto idr = Rtp(3, 300, true, Hex("65aabb"));
  d.Push(idr.data(), idr.size());
  EXPECT(aus.size() == 1);
  if (aus.size() == 1) EXPECT(aus[0].key_frame && aus[0].data == Hex("00000001 65aabb"));
  // STAP-A with a bad size: stops at the broken NAL; empty NALs are skipped.
  auto stap = Rtp(4, 400, true, Hex("18 0002 41aa 0009 41"));
  d.Push(stap.data(), stap.size());
  EXPECT(aus.size() == 2);
  // SPS/PPS already in the access unit: nothing is added before the IDR.
  d.SetParameterSets(Hex("6764"), Hex("68ee"));
  auto full = Rtp(5, 500, true, Hex("18 0002 6764 0002 68ee 0003 65aabb"));
  d.Push(full.data(), full.size());
  EXPECT(aus.size() == 3);
  if (aus.size() == 3) EXPECT(aus[2].data == Hex("00000001 6764 00000001 68ee 00000001 65aabb"));
  d.SetParameterSets({}, {});  // empty sets keep the known ones

  std::vector<media::AccessUnit> aus5;
  h265::Depacketizer d5(96, [&](media::AccessUnit& au) { aus5.push_back(au); });
  EXPECT(!d5.Info(&info));
  auto tiny = Rtp(1, 100, true, Hex("02"));  // payload shorter than a NAL header
  d5.Push(tiny.data(), tiny.size());
  auto other = Hex("80 61 0001 00000064 01020304 0201aa");
  d5.Push(other.data(), other.size());
  auto paci = Rtp(2, 200, true, Hex("6401 aabb"));  // PACI (50): ignored
  d5.Push(paci.data(), paci.size());
  auto fu_mid = Rtp(3, 300, true, Hex("6201 53 ccdd"));  // FU end without start
  d5.Push(fu_mid.data(), fu_mid.size());
  EXPECT(aus5.empty());
  // Lost packet in the middle of an FU: the frame is dropped.
  auto f1 = Rtp(4, 400, false, Hex("6201 93 aa"));
  auto f3 = Rtp(6, 400, true, Hex("6201 53 cc"));
  d5.Push(f1.data(), f1.size());
  d5.Push(f3.data(), f3.size());
  EXPECT(aus5.empty() && d5.lost_packets() == 1);
  // AP with a broken size and a 1-byte NAL (too short): only the valid one.
  auto ap = Rtp(7, 500, true, Hex("6001 0001 02 0003 0201aa 0009 02"));
  d5.Push(ap.data(), ap.size());
  EXPECT(aus5.size() == 1);
  if (aus5.size() == 1) EXPECT(aus5[0].data == Hex("00000001 0201aa"));
  // A key frame with VPS/SPS/PPS in band: nothing is added.
  auto vps = Hex("40010c01");
  auto sps = Hex("42010101");
  auto pps = Hex("4401c172");
  d5.SetParameterSets(vps, sps, pps);
  auto key = Rtp(8, 600, true, Hex("6001 0004 40010c01 0004 42010101 0004 4401c172 0003 2601aa"));
  d5.Push(key.data(), key.size());
  EXPECT(aus5.size() == 2);
  if (aus5.size() == 2) EXPECT(aus5[1].key_frame && aus5[1].data.size() == 4 * 4 + 4 * 3 + 3);
  d5.SetParameterSets({}, {}, {});
}

// Interlaced baseline SPS, H.265 4:4:4 and truncated SPS units.
void TestSpsEdgeCases() {
  BitWriter w;
  w.Bits(66, 8);  // Baseline
  w.Bits(0xc0, 8);
  w.Bits(30, 8);
  w.Ue(0);
  w.Ue(0);   // log2_max_frame_num_minus4
  w.Ue(2);   // pic_order_cnt_type 2
  w.Ue(1);
  w.Bit(0);
  w.Ue(44);  // 720 wide
  w.Ue(17);  // 18 map units of 32 lines (field pairs): 576
  w.Bit(0);  // frame_mbs_only_flag: interlaced
  w.Bit(0);  // mb_adaptive_frame_field_flag
  w.Bit(1);
  w.Bit(0);  // no cropping
  w.Bit(0);
  auto sps = w.Nal({0x67});
  h264::SpsInfo s;
  EXPECT(h264::ParseSps(sps.data(), sps.size(), &s));
  EXPECT(s.width == 720 && s.height == 576 && s.profile_idc == 66);
  // Cut in the middle: invalid.
  EXPECT(!h264::ParseSps(sps.data(), 6, &s));

  BitWriter w5;
  w5.Bits(0, 4);
  w5.Bits(0, 3);      // no sub-layers
  w5.Bit(1);
  w5.Bits(0, 2);      // profile space 0
  w5.Bit(0);          // Main tier
  w5.Bits(4, 5);      // RExt
  w5.Bits(0x08000000, 32);
  for (int i = 0; i < 6; ++i) w5.Bits(0, 8);  // no constraint flags
  w5.Bits(93, 8);
  w5.Ue(0);
  w5.Ue(3);           // chroma 4:4:4
  w5.Bit(0);          // separate_colour_plane_flag
  w5.Ue(1280);
  w5.Ue(720);
  w5.Bit(1);          // conformance window, 4:4:4 units are 1 sample
  w5.Ue(0);
  w5.Ue(0);
  w5.Ue(0);
  w5.Ue(0);
  auto sps5 = w5.Nal({0x42, 0x01});
  h265::SpsInfo s5;
  EXPECT(h265::ParseSps(sps5.data(), sps5.size(), &s5));
  EXPECT(s5.width == 1280 && s5.height == 720);
  EXPECT(s5.codecs == "hev1.4.10.L93");
  EXPECT(!h265::ParseSps(sps5.data(), 3, &s5));
  auto vps = Hex("40010c01ffff");
  EXPECT(!h265::ParseSps(vps.data(), vps.size(), &s5));
  EXPECT(!h265::ParseSps(sps5.data(), 8, &s5));  // cut: invalid
}

}  // namespace

int main() {
  TestMd5();
  TestDigest();
  TestResponseAndSdp();
  TestDepacketizer();
  TestH265();
  TestUrl();
  TestH264HighProfileSps();
  TestH265SpsVariants();
  TestSdpAndAuthExtras();
  TestUrlExtras();
  TestRtpExtension();
  TestUrlEdgeCases();
  TestResponseEdgeCases();
  TestVideoEdgeCases();
  TestDepacketizerEdgeCases();
  TestSpsEdgeCases();
  std::printf(g_failures ? "%d failure(s)\n" : "all tests passed\n", g_failures);
  return g_failures ? 1 : 0;
}
