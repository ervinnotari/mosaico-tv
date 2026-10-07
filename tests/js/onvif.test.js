// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Ervin Notari Junior

'use strict';

/* onvif.js's own SHA-1 produces the WS-Security PasswordDigest. The
 * rest of ONVIF (XML via DOMParser) is tested on the TV: tools/e2e. */

const test = require('node:test');
const assert = require('node:assert');
const crypto = require('crypto');
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
