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
  std::printf(g_failures ? "%d failure(s)\n" : "all tests passed\n", g_failures);
  return g_failures ? 1 : 0;
}
