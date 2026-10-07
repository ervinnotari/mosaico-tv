// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Ervin Notari Junior

'use strict';

const test = require('node:test');
const assert = require('node:assert');
const { load } = require('./harness');

const HIK = 'rtsp://admin:p%40ss@192.168.1.19:554/Streaming/Unicast/channels/301';
const RECT = { x: 0, y: 0, w: 960, h: 540 };

function setup(opts) {
  const ctx = load(['player.js'], opts);
  ctx.status = {};
  ctx.Player.onStatus = (slot, status, text) => { ctx.status[slot] = { status, text }; };
  ctx.Module.onReady();
  return ctx;
}

const starts = (ctx) => ctx.wasmCalls.filter((c) => c[0] === 'player_start');
const stops = (ctx) => ctx.wasmCalls.filter((c) => c[0] === 'player_stop');
const event = (ctx, slot, kind, data) => ctx.Module.onEvent(slot, kind, JSON.stringify(data));
const want = (key, url, mode) => ({ key, url: url || 'rtsp://cam/' + key, mode: mode === undefined ? 1 : mode, rect: RECT });

test('starts only after the WASM module is ready', () => {
  const ctx = load(['player.js']);
  ctx.Player.show([want('a')], true);
  assert.strictEqual(starts(ctx).length, 0);
  ctx.Module.onReady();
  assert.strictEqual(starts(ctx).length, 1);
});

test('software sets the rectangle before starting; native does not', () => {
  const ctx = setup();
  ctx.Player.show([want('a', null, 0), want('b', null, 1)], true);
  const calls = ctx.wasmCalls.map((c) => c[0] + ':' + c[1]);
  assert.ok(calls.indexOf('player_set_rect:1') < calls.indexOf('player_start:1'));
  assert.ok(!calls.includes('player_set_rect:0'));
  assert.deepStrictEqual(starts(ctx).map((c) => c[3]), [0, 1]);  // modes
  assert.strictEqual(ctx.status[0].status, 'connecting');
});

test('changing the slot camera: stops, waits for the end and starts the new one', () => {
  const ctx = setup();
  ctx.Player.show([want('a')], true);
  ctx.Player.show([want('b')], false);
  assert.deepStrictEqual(stops(ctx).map((c) => c[1]), [0]);
  assert.strictEqual(starts(ctx).length, 1, 'does not start before stopping');
  ctx.Module.onStopped(0);
  assert.strictEqual(starts(ctx).length, 2);
  assert.strictEqual(starts(ctx)[1][2], 'rtsp://cam/b');
});

test('same camera in another position only moves the rectangle, no reconnect', () => {
  const ctx = setup();
  ctx.Player.show([want('a')], true);
  ctx.Player.show([Object.assign(want('a'), { rect: { x: 10, y: 20, w: 30, h: 40 } })], true);
  assert.strictEqual(stops(ctx).length, 0);
  assert.strictEqual(starts(ctx).length, 1);
  const rects = ctx.wasmCalls.filter((c) => c[0] === 'player_set_rect');
  assert.deepStrictEqual(rects[rects.length - 1].slice(1), [0, 10, 20, 30, 40]);
});

test('the TV plays only one native: the next waits for the previous to stop', () => {
  const ctx = setup();
  ctx.Player.show([want('a', null, 0)], true);
  // Full screen of another camera in slot 1; slot 0 becomes software.
  ctx.Player.show([want('c', null, 1), want('b', null, 0)], true);
  assert.deepStrictEqual(stops(ctx).map((c) => c[1]), [0]);
  assert.ok(!starts(ctx).some((c) => c[1] === 1), 'new native waiting');
  ctx.Module.onStopped(0);
  assert.ok(starts(ctx).some((c) => c[1] === 1 && c[3] === 0), 'new native started');
  assert.ok(starts(ctx).some((c) => c[1] === 0 && c[3] === 1), 'slot 0 in software');
});

test('unexpected drop: reconnects after 5 s', () => {
  const ctx = setup();
  ctx.Player.show([want('a')], true);
  event(ctx, 0, 'rtsp', { ok: false, code: 'closed_by_camera', text: '' });
  ctx.Module.onStopped(0);
  assert.strictEqual(ctx.status[0].text, 'The camera closed the connection');
  ctx.clock.tick(4999);
  assert.strictEqual(starts(ctx).length, 1);
  ctx.clock.tick(1);
  assert.strictEqual(starts(ctx).length, 2);
});

test('permanent error (H.265 in the mosaic): shows the message and does not reconnect', () => {
  const ctx = setup();
  ctx.Player.show([want('a')], true);
  event(ctx, 0, 'fatal', { ok: false, code: 'h265_mosaic', text: '' });
  ctx.Module.onStopped(0);
  ctx.clock.tick(60000);
  assert.strictEqual(starts(ctx).length, 1);
  assert.strictEqual(ctx.status[0].status, 'error');
  assert.match(ctx.status[0].text, /^H.265 only in full screen/);
  // Changing the screen (layout/page) tries again.
  ctx.Player.show([want('a')], true);
  assert.strictEqual(starts(ctx).length, 2);
});

test('full-screen watchdog: no picture in 6 s, restarts right away', () => {
  const ctx = setup();
  ctx.Player.show([want('a', null, 0)], true);
  event(ctx, 0, 'play', { mode: 'native', codec: 'H264' });
  ctx.clock.tick(5999);
  assert.strictEqual(stops(ctx).length, 0);
  ctx.clock.tick(1);
  assert.deepStrictEqual(stops(ctx).map((c) => c[1]), [0]);
  ctx.Module.onStopped(0);
  assert.strictEqual(starts(ctx).length, 2, 'restarted without waiting the 5 s');
});

test('watchdog does nothing when the picture arrives', () => {
  const ctx = setup();
  ctx.Player.show([want('a', null, 0)], true);
  event(ctx, 0, 'play', { mode: 'native', codec: 'H264' });
  event(ctx, 0, 'first-frame', { ms: 120 });
  ctx.clock.tick(30000);
  assert.strictEqual(stops(ctx).length, 0);
});

test('full screen on Hikvision requests a key frame via ISAPI with the URL login', () => {
  const ctx = setup();
  ctx.Player.show([want('a', HIK, 0)], true);
  event(ctx, 0, 'play', { mode: 'native', codec: 'H264' });
  assert.strictEqual(ctx.requests.length, 1);
  const r = ctx.requests[0];
  assert.strictEqual(r.method, 'PUT');
  assert.strictEqual(r.url, 'http://192.168.1.19/ISAPI/Streaming/channels/301/requestKeyFrame');
  assert.strictEqual(r.user, 'admin');
  assert.strictEqual(r.pass, 'p@ss');
});

test('no key frame for other brands or for the mosaic', () => {
  const ctx = setup();
  ctx.Player.show([want('a', 'rtsp://u:p@10.0.0.5:554/cam/realmonitor?channel=1&subtype=0', 0), want('b', HIK, 1)], true);
  event(ctx, 0, 'play', { mode: 'native', codec: 'H264' });
  event(ctx, 1, 'play', { mode: 'software', codec: 'H264' });
  assert.strictEqual(ctx.requests.length, 0);
});

test('stats with video mark the tile as live and reach the app', () => {
  const ctx = setup();
  const seen = [];
  ctx.Player.onStats = (slot, s) => seen.push([slot, s.fps]);
  ctx.Player.show([want('a')], true);
  event(ctx, 0, 'stats', { fps: 25, width: 352, height: 240, decode_ms: 10, mode: 'software' });
  assert.strictEqual(ctx.status[0].status, 'live');
  assert.strictEqual(seen.length, 1);
  assert.strictEqual(seen[0][1], 25);
});

test('stopAll stops everything and clears the canvas', () => {
  const ctx = setup();
  ctx.Player.show([want('a'), want('b')], true);
  ctx.Player.stopAll();
  assert.deepStrictEqual(stops(ctx).map((c) => c[1]).sort(), [0, 1]);
  ctx.Module.onStopped(0);
  ctx.Module.onStopped(1);
  ctx.clock.tick(60000);
  assert.strictEqual(starts(ctx).length, 2, 'does not reconnect after stopAll');
  assert.ok(ctx.wasmCalls.filter((c) => c[0] === 'player_clear_canvas').length >= 2);
});

test('WASM errors: code translated to the TV language, with the detail', () => {
  const ctx = setup({ language: 'pt-BR' });
  ctx.Player.show([want('a'), want('b'), want('c')], true);
  event(ctx, 0, 'rtsp', { ok: false, code: 'rtsp_status', text: 'DESCRIBE 404 Not Found' });
  assert.strictEqual(ctx.status[0].text, 'A câmera recusou o pedido (DESCRIBE 404 Not Found)');
  // Native player: only the technical detail, inside the generic message.
  event(ctx, 1, 'player', { ok: false, text: 'pipeline error 3' });
  assert.strictEqual(ctx.status[1].text, 'Erro no player da TV (pipeline error 3)');
  // Unknown code (newer WASM version): shows the detail as it came.
  event(ctx, 2, 'rtsp', { ok: false, code: 'something_new', text: 'raw detail' });
  assert.strictEqual(ctx.status[2].text, 'raw detail');
});

test('connection status follows the language', () => {
  const pt = setup({ language: 'pt-BR' });
  pt.Player.show([want('a')], true);
  assert.strictEqual(pt.status[0].text, 'Conectando…');
  const en = setup({ language: 'de-DE' });
  en.Player.show([want('a')], true);
  assert.strictEqual(en.status[0].text, 'Connecting…');
});
