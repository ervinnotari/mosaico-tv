# Instructions for Claude Code

> Original instructions given to the coding assistant at the start of the
> project. The test password has been replaced with a placeholder.

Read `SPEC.md` before implementing.

## Main rule
The goal is direct RTSP on the Samsung TV. Do not use an intermediate server.

## Tools
Use:
- C++.
- Samsung Emscripten SDK/Fastcomp 1.39.4.7.
- Tizen Sockets Extension.
- Samsung Tizen WASM Player.
- HTML/CSS/JavaScript for the UI.
- Tizen tools to build the `.wgt`.

Do not use:
- external FFmpeg;
- VLC;
- external GStreamer;
- Node/Python as a server;
- RTSP->HLS proxy;
- WebSocket proxy;
- cloud.

## Required research
Before coding, check the current official Samsung documentation and the
official WASM Player sample. Confirm header, class and API names in the SDK.
Do not invent APIs.

## Incremental implementation
Phase 1: Web widget + minimal WASM + build.
Phase 2: TCP socket.
Phase 3: OPTIONS/DESCRIBE + SDP.
Phase 4: SETUP/PLAY + interleaved RTP.
Phase 5: H264 SPS/PPS + FU-A.
Phase 6: WASM Player + video.
Phase 7: UI, Stop/TEARDOWN, retry and errors.

Validate each phase before the next one.

## RTSP
Keep CSeq and Session. Resolve control URLs correctly. Support Basic Auth
initially.

## H264
Do not implement a decoder. Hand the elementary stream to the WASM Player.

## Build
When applicable, use the flags recommended by the Samsung sample:
`-s ENVIRONMENT_MAY_BE_TIZEN -pthread -s USE_PTHREADS=1 -s PTHREAD_POOL_SIZE=1`
Do not use `MODULARIZE=1` if it conflicts with the sample loader.

## Initial URL
`rtsp://admin:<password>@<camera-ip>:554/Streaming/Unicast/channels/101`

The URL must be configurable and the password must never appear in logs.

## Final delivery
Produce:
1. source code;
2. Tizen project;
3. WASM module;
4. build scripts;
5. parser tests;
6. documentation;
7. a signed `.wgt` if the environment has a certificate;
8. installation and test instructions.

Never claim it works just because it compiled. Real success is H264 video
visible on the TV.
