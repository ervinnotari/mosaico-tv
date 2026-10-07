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

}  // namespace

int main() {
  TestMd5();
  TestDigest();
  TestResponseAndSdp();
  TestDepacketizer();
  TestH265();
  TestUrl();
  std::printf(g_failures ? "%d failure(s)\n" : "all tests passed\n", g_failures);
  return g_failures ? 1 : 0;
}
