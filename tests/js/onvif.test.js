// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Ervin Notari Junior

'use strict';

/* onvif.js's own SHA-1 and the WS-Security header (PasswordDigest). The
 * rest of ONVIF (XML via DOMParser) is tested on the TV: tools/e2e. */

const test = require('node:test');
const assert = require('node:assert');
const crypto = require('node:crypto');
const { load } = require('./harness');

const hex = (bytes) => Buffer.from(bytes).toString('hex');

test('SHA-1: FIPS 180 vectors', () => {
  const { Onvif } = load(['onvif.js']);
  assert.strictEqual(hex(Onvif._sha1(new Uint8Array(0))), 'da39a3ee5e6b4b0d3255bfef95601890afd80709');
  assert.strictEqual(hex(Onvif._sha1(Buffer.from('abc'))), 'a9993e364706816aba3e25717850c26c9cd0d89d');
  assert.strictEqual(
    hex(Onvif._sha1(Buffer.from('abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq'))),
    '84983e441c3bd26ebaae4aa1f95129e5e54670f1');
});

test('SHA-1 matches Node at sizes that cross 64-byte blocks', () => {
  const { Onvif } = load(['onvif.js']);
  for (const n of [1, 55, 56, 63, 64, 65, 119, 120, 1000]) {
    const data = crypto.randomBytes(n);
    assert.strictEqual(hex(Onvif._sha1(new Uint8Array(data))),
      crypto.createHash('sha1').update(data).digest('hex'), `tamanho ${n}`);
  }
});

test('WS-Security header: PasswordDigest = base64(SHA-1(nonce + created + password))', () => {
  const { Onvif } = load(['onvif.js']);
  const xml = Onvif._security('ad<min', 'sénha', 60000);
  const nonce = Buffer.from(/<Nonce [^>]*>([^<]+)</.exec(xml)[1], 'base64');
  const created = /<Created [^>]*>([^<]+)</.exec(xml)[1];
  const digest = /<Password [^>]*>([^<]+)</.exec(xml)[1];
  assert.strictEqual(nonce.length, 16);
  assert.match(created, /^\d{4}-\d\d-\d\dT\d\d:\d\d:\d\dZ$/);
  // The clock offset (one minute) is applied to the timestamp.
  assert.ok(Math.abs(Date.parse(created) - (Date.now() + 60000)) < 5000);
  const expected = crypto.createHash('sha1')
    .update(Buffer.concat([nonce, Buffer.from(created), Buffer.from('sénha', 'utf8')]))
    .digest('base64');
  assert.strictEqual(digest, expected);
  assert.ok(xml.includes('<Username>ad&lt;min</Username>'), 'user name is XML-escaped');
  assert.strictEqual(Onvif._security('', 'x', 0), '');
  // A new nonce every time.
  assert.notStrictEqual(Onvif._security('a', 'b', 0), Onvif._security('a', 'b', 0));
});

test('RTSP URLs from the device get the login, replacing any existing one', () => {
  const { Onvif } = load(['onvif.js']);
  assert.strictEqual(Onvif._withCredentials('rtsp://10.0.0.2/ch1', 'admin', 'p@ss'),
    'rtsp://admin:p%40ss@10.0.0.2/ch1');
  assert.strictEqual(Onvif._withCredentials('rtsp://old:x@10.0.0.2/ch1', 'u', 'v'), 'rtsp://u:v@10.0.0.2/ch1');
  assert.strictEqual(Onvif._withCredentials('rtsp://10.0.0.2/ch1', '', 'v'), 'rtsp://10.0.0.2/ch1');
});
