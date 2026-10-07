// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Ervin Notari Junior

#include "soft_decoder.hpp"

#include <cstring>

#include "codec_api.h"

namespace h264 {

namespace {

void CopyPlane(const uint8_t* src, int stride, int width, int height,
               std::vector<uint8_t>* dst) {
  dst->resize(static_cast<size_t>(width) * height);
  for (int row = 0; row < height; ++row) {
    std::memcpy(dst->data() + static_cast<size_t>(row) * width,
                src + static_cast<size_t>(row) * stride, static_cast<size_t>(width));
  }
}

}  // namespace

SoftDecoder::SoftDecoder() {
  if (WelsCreateDecoder(&decoder_) != 0 || !decoder_) {
    decoder_ = nullptr;
    return;
  }
  int log_level = WELS_LOG_QUIET;
  decoder_->SetOption(DECODER_OPTION_TRACE_LEVEL, &log_level);

  SDecodingParam param;
  std::memset(&param, 0, sizeof(param));
  param.sVideoProperty.eVideoBsType = VIDEO_BITSTREAM_AVC;
  param.eEcActiveIdc = ERROR_CON_SLICE_COPY;  // hides losses with the previous frame
  if (decoder_->Initialize(&param) != 0) {
    WelsDestroyDecoder(decoder_);
    decoder_ = nullptr;
  }
}

SoftDecoder::~SoftDecoder() {
  if (decoder_) {
    decoder_->Uninitialize();
    WelsDestroyDecoder(decoder_);
  }
}

bool SoftDecoder::Decode(const uint8_t* data, size_t size, YuvFrame* out) {
  if (!decoder_) return false;
  unsigned char* planes[3] = {nullptr, nullptr, nullptr};
  SBufferInfo info;
  std::memset(&info, 0, sizeof(info));
  decoder_->DecodeFrameNoDelay(data, static_cast<int>(size), planes, &info);
  if (info.iBufferStatus != 1 || !planes[0]) return false;

  const SSysMEMBuffer& buf = info.UsrData.sSystemBuffer;
  out->width = buf.iWidth;
  out->height = buf.iHeight;
  CopyPlane(planes[0], buf.iStride[0], buf.iWidth, buf.iHeight, &out->y);
  CopyPlane(planes[1], buf.iStride[1], buf.iWidth / 2, buf.iHeight / 2, &out->u);
  CopyPlane(planes[2], buf.iStride[1], buf.iWidth / 2, buf.iHeight / 2, &out->v);
  return true;
}

}  // namespace h264
