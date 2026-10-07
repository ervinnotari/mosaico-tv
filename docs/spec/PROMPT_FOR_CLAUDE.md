# Initial prompt

> Original prompt that started the project. The test password has been
> replaced with a placeholder.

Read `CLAUDE_INSTRUCTIONS.md` and `SPEC.md`.

Implement the complete direct-RTSP project for Samsung Tizen TV.

Test URL:
`rtsp://admin:<password>@<camera-ip>:554/Streaming/Unicast/channels/101`

Before coding:
1. Check the official Samsung documentation.
2. Check the official Tizen WASM Player sample.
3. Check the Tizen Sockets Extension.
4. Confirm the real APIs and headers.

Implement incrementally:
- minimal WASM build;
- TCP socket;
- RTSP;
- SDP;
- interleaved RTP;
- H264;
- WASM Player;
- UI;
- Stop/retry;
- `.wgt` package.

The first version must support only H264 and RTSP over TCP. Do not use an
intermediate server.

At the end, deliver code, tests, documentation, scripts and the `.wgt` when
possible.
