// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Ervin Notari Junior

// Generates the reference H.264 clip for the TV performance test.
//
// Synthetic scene similar to a surveillance camera: fixed textured background,
// sensor noise on every frame, two moving objects and slowly changing
// light. Encoded with OpenH264 in Main profile + CABAC, like the
// DVR substreams.
//
// Output: sequence of [u32 little-endian size][Annex B access unit].
// Usage (Node): gen_clip <width> <height> <frames> <fps> <kbps> <output>

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

#include "codec_api.h"

namespace {

uint32_t g_seed = 12345;
int Rand(int n) {
  g_seed = g_seed * 1103515245u + 12345u;
  return static_cast<int>((g_seed >> 16) % static_cast<uint32_t>(n));
}

uint8_t Clamp(int v) { return static_cast<uint8_t>(v < 0 ? 0 : v > 255 ? 255 : v); }

// Background: gradient + "buildings" (rectangles) + fixed fine texture.
void MakeBackground(int w, int h, std::vector<uint8_t>* y) {
  y->resize(static_cast<size_t>(w) * h);
  for (int r = 0; r < h; ++r) {
    for (int c = 0; c < w; ++c) {
      (*y)[r * w + c] = Clamp(60 + 100 * r / h + Rand(24) - 12);
    }
  }
  for (int i = 0; i < 14; ++i) {
    int bw = w / 12 + Rand(w / 5), bh = h / 6 + Rand(h / 2);
    int bx = Rand(w - bw), by = h - bh - Rand(h / 8);
    int tone = 40 + Rand(160);
    for (int r = by; r < by + bh; ++r) {
      for (int c = bx; c < bx + bw; ++c) {
        // Windows: grid of lighter squares.
        bool window = (r / 6) % 2 == 0 && (c / 5) % 3 == 0;
        (*y)[r * w + c] = Clamp(tone + (window ? 50 : 0) + Rand(10) - 5);
      }
    }
  }
}

void DrawEllipse(std::vector<uint8_t>* y, int w, int h, double cx, double cy,
                 double rx, double ry, int tone) {
  for (int r = static_cast<int>(cy - ry); r <= cy + ry; ++r) {
    for (int c = static_cast<int>(cx - rx); c <= cx + rx; ++c) {
      if (r < 0 || r >= h || c < 0 || c >= w) continue;
      double dx = (c - cx) / rx, dy = (r - cy) / ry;
      if (dx * dx + dy * dy <= 1.0) (*y)[r * w + c] = Clamp(tone + Rand(16) - 8);
    }
  }
}

}  // namespace

int main(int argc, char** argv) {
  if (argc < 7) {
    std::fprintf(stderr, "usage: gen_clip width height frames fps kbps output\n");
    return 1;
  }
  const int w = std::atoi(argv[1]), h = std::atoi(argv[2]);
  const int frames = std::atoi(argv[3]), fps = std::atoi(argv[4]);
  const int kbps = std::atoi(argv[5]);
  const char* out_path = argv[6];

  ISVCEncoder* enc = nullptr;
  if (WelsCreateSVCEncoder(&enc) != 0 || !enc) return 2;
  SEncParamExt p;
  enc->GetDefaultParams(&p);
  p.iUsageType = CAMERA_VIDEO_REAL_TIME;
  p.iPicWidth = w;
  p.iPicHeight = h;
  p.fMaxFrameRate = static_cast<float>(fps);
  p.iTargetBitrate = kbps * 1000;
  p.iMaxBitrate = kbps * 1500;
  p.iRCMode = RC_BITRATE_MODE;
  p.iSpatialLayerNum = 1;
  p.iTemporalLayerNum = 1;
  p.uiIntraPeriod = static_cast<unsigned>(fps);  // one IDR per second
  p.iEntropyCodingModeFlag = 1;                   // CABAC
  p.iMultipleThreadIdc = 1;
  p.bEnableFrameSkip = false;
  p.iNumRefFrame = 1;
  SSpatialLayerConfig& layer = p.sSpatialLayers[0];
  layer.iVideoWidth = w;
  layer.iVideoHeight = h;
  layer.fFrameRate = static_cast<float>(fps);
  layer.iSpatialBitrate = kbps * 1000;
  layer.iMaxSpatialBitrate = kbps * 1500;
  layer.uiProfileIdc = PRO_MAIN;
  layer.sSliceArgument.uiSliceMode = SM_SINGLE_SLICE;
  if (enc->InitializeExt(&p) != 0) {
    std::fprintf(stderr, "InitializeExt failed\n");
    return 3;
  }
  int fmt = videoFormatI420;
  enc->SetOption(ENCODER_OPTION_DATAFORMAT, &fmt);

  std::vector<uint8_t> bg;
  MakeBackground(w, h, &bg);
  std::vector<uint8_t> y(static_cast<size_t>(w) * h);
  std::vector<uint8_t> u(static_cast<size_t>(w / 2) * (h / 2), 128);
  std::vector<uint8_t> v(static_cast<size_t>(w / 2) * (h / 2), 128);
  // Chroma: different tones per region so it is not pure gray.
  for (int r = 0; r < h / 2; ++r) {
    for (int c = 0; c < w / 2; ++c) {
      u[r * (w / 2) + c] = Clamp(118 + (c * 20) / (w / 2));
      v[r * (w / 2) + c] = Clamp(126 + (r * 16) / (h / 2));
    }
  }

  FILE* out = std::fopen(out_path, "wb");
  if (!out) return 4;
  size_t total = 0;
  for (int f = 0; f < frames; ++f) {
    double t = static_cast<double>(f) / fps;
    int light = static_cast<int>(8 * std::sin(t * 0.7));
    for (size_t i = 0; i < y.size(); ++i) {
      y[i] = Clamp(bg[i] + light + Rand(7) - 3);  // sensor noise
    }
    // Person walking and car passing.
    DrawEllipse(&y, w, h, std::fmod(w * 0.1 + t * w * 0.12, w), h * 0.72,
                w * 0.025, h * 0.1, 35);
    DrawEllipse(&y, w, h, std::fmod(w * 0.9 - t * w * 0.35 + 2 * w, w), h * 0.86,
                w * 0.09, h * 0.05, 220);

    SSourcePicture pic;
    std::memset(&pic, 0, sizeof(pic));
    pic.iColorFormat = videoFormatI420;
    pic.iPicWidth = w;
    pic.iPicHeight = h;
    pic.iStride[0] = w;
    pic.iStride[1] = pic.iStride[2] = w / 2;
    pic.pData[0] = y.data();
    pic.pData[1] = u.data();
    pic.pData[2] = v.data();
    pic.uiTimeStamp = static_cast<long long>(t * 1000);

    SFrameBSInfo info;
    std::memset(&info, 0, sizeof(info));
    if (enc->EncodeFrame(&pic, &info) != 0) {
      std::fprintf(stderr, "EncodeFrame failed on frame %d\n", f);
      return 5;
    }
    std::vector<uint8_t> au;
    for (int l = 0; l < info.iLayerNum; ++l) {
      const SLayerBSInfo& li = info.sLayerInfo[l];
      int size = 0;
      for (int n = 0; n < li.iNalCount; ++n) size += li.pNalLengthInByte[n];
      au.insert(au.end(), li.pBsBuf, li.pBsBuf + size);
    }
    if (au.empty()) continue;
    uint32_t len = static_cast<uint32_t>(au.size());
    uint8_t hdr[4] = {static_cast<uint8_t>(len), static_cast<uint8_t>(len >> 8),
                      static_cast<uint8_t>(len >> 16), static_cast<uint8_t>(len >> 24)};
    std::fwrite(hdr, 1, 4, out);
    std::fwrite(au.data(), 1, au.size(), out);
    total += au.size();
  }
  std::fclose(out);
  enc->Uninitialize();
  WelsDestroySVCEncoder(enc);
  std::printf("%s: %d quadros, %zu bytes (%.0f kbps)\n", out_path, frames, total,
              total * 8.0 / (static_cast<double>(frames) / fps) / 1000);
  return 0;
}
