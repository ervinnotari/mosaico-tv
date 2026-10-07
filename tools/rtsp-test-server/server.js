#!/usr/bin/env node
// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Ervin Notari Junior

/* Minimal RTSP server to test the app without a camera: serves an
 * H.264 or H.265 file (Annex B, with an AUD on each frame) in a loop, over RTP
 * interleaved on TCP, like the cameras/DVRs the app uses.
 *
 * Usage: node server.js <file.h264|.hevc> [port=8554] [fps=25]
 * URL: rtsp://<pc-ip>:<port>/test
 *
 * Generate a test file (needs the AUD, aud=1):
 *   ffmpeg -f lavfi -i testsrc2=size=1280x720:rate=25 -t 20 -c:v libx265 \
 *     -x265-params keyint=25:repeat-headers=1:aud=1 -f hevc test.hevc
 */
'use strict';

const fs = require('fs');
const net = require('net');
const os = require('os');

const [file, portArg, fpsArg] = process.argv.slice(2);
if (!file) {
  console.error('usage: node server.js <file.h264|.hevc> [port] [fps]');
  process.exit(1);
}
const PORT = Number(portArg) || 8554;
const FPS = Number(fpsArg) || 25;
const MTU = 1400;
const H265 = /\.(hevc|h265|265)$/i.test(file);

// ---- Annex B -> NALs -> frames (split by the AUD) ----
function splitNals(buf) {
  const nals = [];
  let start = -1;
  for (let i = 0; i + 3 <= buf.length; i++) {
    if (buf[i] === 0 && buf[i + 1] === 0 && buf[i + 2] === 1) {
      if (start >= 0) nals.push(buf.subarray(start, i > 0 && buf[i - 1] === 0 ? i - 1 : i));
      start = i + 3;
      i += 2;
    }
  }
  if (start >= 0) nals.push(buf.subarray(start));
  return nals.filter((n) => n.length > 0);
}

const nalType = (nal) => (H265 ? (nal[0] >> 1) & 0x3f : nal[0] & 0x1f);
const AUD = H265 ? 35 : 9;
const params = {};  // first VPS/SPS/PPS, for the SDP
const frames = [];
for (const nal of splitNals(fs.readFileSync(file))) {
  const t = nalType(nal);
  if (t === AUD) { frames.push([]); continue; }
  if (!frames.length) frames.push([]);
  const name = H265 ? { 32: 'vps', 33: 'sps', 34: 'pps' }[t] : { 7: 'sps', 8: 'pps' }[t];
  if (name && !params[name]) params[name] = nal;
  frames[frames.length - 1].push(nal);
}
const playable = frames.filter((f) => f.length);
if (!playable.length || !params.sps) {
  console.error('file without frames or without SPS (generate it with aud=1 and repeat-headers=1)');
  process.exit(1);
}

function sdp(host) {
  const b64 = (n) => Buffer.from(n).toString('base64');
  const fmtp = H265
    ? `sprop-vps=${b64(params.vps)}; sprop-sps=${b64(params.sps)}; sprop-pps=${b64(params.pps)}`
    : `packetization-mode=1; sprop-parameter-sets=${b64(params.sps)},${b64(params.pps)}`;
  return [
    'v=0', `o=- 0 0 IN IP4 ${host}`, 's=Test', 't=0 0', 'a=control:*',
    'm=video 0 RTP/AVP 96', `a=rtpmap:96 ${H265 ? 'H265' : 'H264'}/90000`,
    `a=fmtp:96 ${fmtp}`, `a=framerate:${FPS}`, 'a=control:trackID=0', ''
  ].join('\r\n');
}

// ---- RTP (RFC 6184 / RFC 7798) ----
function rtpPayloads(nal) {
  if (nal.length <= MTU) return [nal];
  const out = [];
  const hdr = H265 ? 2 : 1;
  const type = nalType(nal);
  for (let off = hdr; off < nal.length; off += MTU) {
    const chunk = nal.subarray(off, Math.min(nal.length, off + MTU));
    const s = off === hdr ? 0x80 : 0;
    const e = off + MTU >= nal.length ? 0x40 : 0;
    const fu = H265
      ? Buffer.from([(nal[0] & 0x81) | (49 << 1), nal[1], s | e | type])
      : Buffer.from([(nal[0] & 0xe0) | 28, s | e | type]);
    out.push(Buffer.concat([fu, chunk]));
  }
  return out;
}

function play(client) {
  let index = 0, seq = Math.floor(Math.random() * 65536), ts = Math.floor(Math.random() * 2 ** 31);
  const ssrc = Math.floor(Math.random() * 2 ** 32);
  client.timer = setInterval(() => {
    const payloads = playable[index].flatMap(rtpPayloads);
    payloads.forEach((p, i) => {
      const rtp = Buffer.alloc(12);
      rtp[0] = 0x80;
      rtp[1] = 96 | (i === payloads.length - 1 ? 0x80 : 0);
      rtp.writeUInt16BE(seq, 2);
      rtp.writeUInt32BE(ts >>> 0, 4);
      rtp.writeUInt32BE(ssrc >>> 0, 8);
      seq = (seq + 1) & 0xffff;
      const frame = Buffer.concat([rtp, p]);
      const head = Buffer.from([0x24, client.channel, frame.length >> 8, frame.length & 0xff]);
      client.socket.write(Buffer.concat([head, frame]));
    });
    ts = (ts + 90000 / FPS) >>> 0;
    index = (index + 1) % playable.length;
  }, 1000 / FPS);
}

// ---- RTSP ----
net.createServer((socket) => {
  const client = { socket, channel: 0, timer: null };
  const peer = socket.remoteAddress;
  let buf = '';
  console.log(`[${peer}] connected`);
  socket.on('data', (data) => {
    buf += data.toString('latin1');
    let end;
    while ((end = buf.indexOf('\r\n\r\n')) >= 0) {
      const req = buf.slice(0, end);
      buf = buf.slice(end + 4);
      const [line, ...headers] = req.split('\r\n');
      const [method, url] = line.split(' ');
      const header = (n) => (headers.find((h) => h.toLowerCase().startsWith(n.toLowerCase() + ':')) || '').split(':').slice(1).join(':').trim();
      const reply = (extra, body) => socket.write(
        `RTSP/1.0 200 OK\r\nCSeq: ${header('CSeq')}\r\n${extra || ''}` +
        (body ? `Content-Length: ${Buffer.byteLength(body)}\r\n\r\n${body}` : '\r\n'));
      console.log(`[${peer}] ${method} ${url}`);
      if (method === 'DESCRIBE') {
        const base = url.endsWith('/') ? url : url + '/';
        reply(`Content-Base: ${base}\r\nContent-Type: application/sdp\r\n`, sdp(socket.localAddress));
      } else if (method === 'SETUP') {
        const m = header('Transport').match(/interleaved=(\d+)/);
        client.channel = m ? Number(m[1]) : 0;
        reply(`Transport: RTP/AVP/TCP;unicast;interleaved=${client.channel}-${client.channel + 1}\r\nSession: 12345678;timeout=60\r\n`);
      } else if (method === 'PLAY') {
        reply('Session: 12345678\r\nRange: npt=0.000-\r\n');
        if (!client.timer) play(client);
      } else if (method === 'TEARDOWN') {
        reply('Session: 12345678\r\n');
        clearInterval(client.timer);
        client.timer = null;
      } else {
        reply('Public: OPTIONS, DESCRIBE, SETUP, PLAY, TEARDOWN, GET_PARAMETER\r\n');
      }
    }
  });
  const done = () => { clearInterval(client.timer); console.log(`[${peer}] closed`); };
  socket.on('close', done);
  socket.on('error', () => {});
}).listen(PORT, () => {
  const ips = Object.values(os.networkInterfaces()).flat()
    .filter((i) => i.family === 'IPv4' && !i.internal).map((i) => i.address);
  console.log(`${H265 ? 'H.265' : 'H.264'}: ${playable.length} frames at ${FPS} fps in a loop`);
  ips.forEach((ip) => console.log(`rtsp://${ip}:${PORT}/test`));
});
