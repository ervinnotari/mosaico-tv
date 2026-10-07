# Acceptance Tests

> Original acceptance tests of the first phase. The automated tests in
> `tests/js`, `wasm/tests` and `tools/e2e` cover these and more.

1. The app starts without crashing.
2. The URL can be edited.
3. Connect establishes TCP.
4. OPTIONS returns 200.
5. DESCRIBE returns 200.
6. The SDP identifies H264.
7. SETUP returns 200 and the Session is stored.
8. PLAY returns 200.
9. Interleaved RTP arrives.
10. SPS/PPS are detected.
11. FU-A is reassembled when needed.
12. The WASM Player creates a video track.
13. Video shows up on the TV.
14. Stop sends TEARDOWN and closes the socket.
15. An invalid URL shows an error without crashing.
16. An authentication failure does not expose the password.
17. A network failure allows Retry.
