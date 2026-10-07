# Architecture Decision

> Original decision record from the first phase. It still holds for full
> screen; the mosaic later added a software H.264 decoder because the TV
> allows only one native player at a time (see the README).

## Decision
Samsung WebAssembly + Tizen Sockets Extension + Tizen WASM Player.

## Rationale
The goal is to implement RTSP/RTP in the TV client itself and hand H264
elementary streams to the TV multimedia pipeline, avoiding a custom decoder
and any intermediate server.

## Flow
RTSP camera <-> TCP <-> WASM RTSP client -> RTP/H264 -> ElementaryMediaTrack
-> TV decoder/rendering.

## Initial transport
RTSP over TCP with interleaved RTP. UDP comes later.

## Why not AVPlay directly?
The solution must not depend on AVPlay's own RTSP support. The WASM Player
gives access at the level of elementary media packets.

## Why not a custom decoder?
The WASM Player feeds the packets to the TV multimedia pipeline, so the
platform decoder can be used.
