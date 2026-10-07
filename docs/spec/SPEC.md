# SPEC — Direct RTSP Tizen TV Player

> Original specification of the first phase of the project, kept for
> reference. The project has since gone beyond it (ONVIF, multiple cameras,
> H.265, software decoding for the mosaic); see the README for the current
> state.

## Goal
A simple screen lets the user enter an RTSP URL, connect, stop and watch the
video directly on the TV.

## UI
- RTSP URL field.
- Connect/Play.
- Stop.
- Status.
- Full-screen or nearly full-screen video.
- Remote-control navigation.
- Visible focus.
- Understandable error messages.

## Architecture
```text
Tizen Web Widget
  HTML/CSS/JS
       |
       v
 WebAssembly C++
  + RTSP client
  + SDP parser
  + RTP parser
  + H264 depacketizer
       |
       v
Samsung WASM Player
  ElementaryMediaStreamSource
  ElementaryMediaTrack
       |
       v
TV multimedia pipeline
```

## RTSP
Implement initially:
OPTIONS -> DESCRIBE -> SETUP -> PLAY -> RTP -> TEARDOWN.

Keep CSeq and Session.
Handle status, headers, SDP, Session, Transport, Content-Length and control
URLs.
Support Basic Auth initially; keep the architecture ready for Digest.

## SDP
Extract:
- video;
- H264;
- payload type;
- clock rate;
- control URI;
- fmtp;
- profile-level-id;
- sprop-parameter-sets.

Reject unsupported codecs in the first version.

## RTP/H264
First version: RTP interleaved over TCP.
Implement:
- RTP header;
- sequence;
- timestamp;
- payload type;
- SSRC;
- single NAL;
- FU-A;
- STAP-A when needed;
- SPS/PPS;
- reassembly;
- monotonic timestamps;
- wraparound.

Do not write an H264 decoder.

## WASM Player
Use the real APIs documented by Samsung:
- `samsung::html::HTMLMediaElement`
- `samsung::wasm::ElementaryMediaStreamSource`
- `samsung::wasm::ElementaryMediaTrack`

Use `kMediaElement` rendering.
Choose Normal/Low Latency according to the official sample.
The codec/extradata configuration must follow the Samsung sample and
documentation, without inventing formats.

## Sockets
Use the Tizen Sockets Extension for TCP initially.
Add the privilege:
`http://tizen.org/privilege/internet`

The extension provides APIs such as socket/connect/send/recv/poll/select/close.

## Concurrency
Do not block the UI.
Use pthreads/WASM as in the Samsung sample.

Reference flags:
`-s ENVIRONMENT_MAY_BE_TIZEN -pthread -s USE_PTHREADS=1 -s PTHREAD_POOL_SIZE=1`

## Security
Never log passwords.
Sanitize URLs in logs:
`rtsp://admin:***@host:554/path`
Do not persist passwords unless needed.

## Suggested layout
```text
rtsp-tizen-player/
  app/
    index.html
    config.xml
    css/app.css
    js/app.js
  wasm/
    include/
      rtsp_client.hpp
      rtsp_parser.hpp
      sdp_parser.hpp
      rtp_h264.hpp
      wasm_player.hpp
    src/
      rtsp_client.cpp
      rtsp_parser.cpp
      sdp_parser.cpp
      rtp_h264.cpp
      wasm_player.cpp
      main.cpp
  scripts/
    build-wasm.bat
    build-wgt.bat
  docs/
    ARCHITECTURE.md
    TROUBLESHOOTING.md
  README.md
```
The layout may be adapted to the SDK.

## Performance
Avoid unnecessary copies and allocations.
Use reusable buffers.
Do not log every packet at INFO level.
Do not accumulate RTP indefinitely.

## Reconnection
At most 3 automatic attempts with 1 s, 2 s, 4 s backoff; then show an error
and allow Retry.

## Diagnostics
Separate errors by layer:
TCP, RTSP, SDP, RTP, H264, WASM Player, rendering.

## Out of the initial scope
ONVIF, multiple cameras, PTZ, audio, H265, UDP, recording, snapshots,
cloud/backend.

## Success criterion
The H264 stream of the test URL must show up on the Samsung The Frame without
any intermediate device or server.
