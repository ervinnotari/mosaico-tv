// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Ervin Notari Junior

'use strict';

const test = require('node:test');
const assert = require('node:assert');
const { load, plain } = require('./harness');

test('Hikvision URLs use the channel and encode the password', () => {
  const { Store } = load(['store.js']);
  const urls = Store.urlsFromForm({
    brand: 'hikvision', host: ' 192.168.1.19 ', port: '554', channel: '3',
    user: 'admin', pass: 'a@b:c/d'
  });
  assert.strictEqual(urls.main, 'rtsp://admin:a%40b%3Ac%2Fd@192.168.1.19:554/Streaming/Channels/301');
  assert.strictEqual(urls.sub, 'rtsp://admin:a%40b%3Ac%2Fd@192.168.1.19:554/Streaming/Channels/302');
});

test('Intelbras/Dahua URLs and default port', () => {
  const { Store } = load(['store.js']);
  const urls = Store.urlsFromForm({ brand: 'dahua', host: '10.0.0.5', port: '', channel: '2', user: '', pass: '' });
  assert.strictEqual(urls.main, 'rtsp://10.0.0.5:554/cam/realmonitor?channel=2&subtype=0');
  assert.strictEqual(urls.sub, 'rtsp://10.0.0.5:554/cam/realmonitor?channel=2&subtype=1');
});

test('custom path: leading slash, and an empty substream falls back to the main one', () => {
  const { Store } = load(['store.js']);
  const urls = Store.urlsFromForm({
    brand: 'custom', host: '10.0.0.7', port: '8554', user: 'u', pass: 'p', mainPath: 'live/main', subPath: ''
  });
  assert.strictEqual(urls.main, 'rtsp://u:p@10.0.0.7:8554/live/main');
  assert.strictEqual(urls.sub, urls.main);
});

test('sanitize hides the password', () => {
  const { Store } = load(['store.js']);
  assert.strictEqual(Store.sanitize('rtsp://admin:segredo@1.2.3.4:554/x'), 'rtsp://admin:***@1.2.3.4:554/x');
  assert.strictEqual(Store.sanitize('rtsp://1.2.3.4/x'), 'rtsp://1.2.3.4/x');
});

test('add, move, update and remove persist in localStorage', () => {
  const ctx = load(['store.js']);
  const { Store } = ctx;
  const a = Store.add({ name: 'A', main: 'rtsp://a', sub: 'rtsp://a2' });
  const b = Store.add({ name: 'B', main: 'rtsp://b', sub: 'rtsp://b2' });
  Store.move(b.id, -1);
  assert.deepStrictEqual(plain(Store.cameras().map((c) => c.name)), ['B', 'A']);
  Store.move(b.id, -1);  // already the first: no change
  assert.deepStrictEqual(plain(Store.cameras().map((c) => c.name)), ['B', 'A']);
  Store.update(a.id, { name: 'A2' });
  Store.remove(b.id);
  assert.ok(Store.has('rtsp://a'));
  assert.ok(!Store.has('rtsp://b'));

  const saved = JSON.parse(ctx.localStorage.getItem('rtsp-player-v1'));
  assert.deepStrictEqual(saved.cameras.map((c) => c.name), ['A2']);

  // Reloading the app reads what was saved.
  const again = load(['store.js'], { localStorage: Object.fromEntries(ctx.localStorage._data) });
  assert.deepStrictEqual(plain(again.Store.cameras().map((c) => c.name)), ['A2']);
});

test('invalid saved layout goes back to 4; corrupted data starts empty', () => {
  let ctx = load(['store.js'], { localStorage: { 'rtsp-player-v1': JSON.stringify({ cameras: [], layout: 9, page: 0 }) } });
  assert.strictEqual(ctx.Store.layout(), 4);
  ctx = load(['store.js'], { localStorage: { 'rtsp-player-v1': '{quebrado' } });
  assert.strictEqual(ctx.Store.cameras().length, 0);
  assert.strictEqual(ctx.Store.layout(), 4);
});
