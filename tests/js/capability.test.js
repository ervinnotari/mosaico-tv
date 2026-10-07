// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Ervin Notari Junior

'use strict';

const test = require('node:test');
const assert = require('node:assert');
const { load } = require('./harness');

// Loads Player + Capability with a fake WASM that accepts the benchmark.
function setup(opts) {
  const ctx = load(['player.js', 'capability.js'], opts);
  ctx.benchCalls = [];
  Object.assign(ctx.Module, {
    HEAPU8: new Uint8Array(1 << 20),
    _malloc: () => 16,
    _free: () => {},
    _bench_start: (...args) => { ctx.benchCalls.push(args); return 1; }
  });
  return ctx;
}

// Runs ensure() answering with the clip and the benchmark result.
function measure(ctx, mpx) {
  let measuredNow = null;
  ctx.Capability.ensure(null, (now) => { measuredNow = now; });
  const xhr = ctx.requests.find((r) => /bench\/ref_352x240\.bin$/.test(r.url));
  assert.ok(xhr, 'requested the reference clip');
  xhr.respond(200, new ArrayBuffer(1000));
  assert.strictEqual(ctx.benchCalls.length, 1, 'started the benchmark');
  ctx.Player.onBench({ ok: true, threads: 4, mpx_per_s: mpx, windows: [mpx] });
  return measuredNow;
}

const cam = (w, h, fps) => ({ subInfo: w ? { w, h, fps } : undefined });
const cams = (n, info) => Array.from({ length: n }, () => info || cam());

test('calibration: 68 Mpx/s on the clip equals ~33.8 of real capacity', () => {
  const ctx = setup();
  assert.strictEqual(measure(ctx, 68), true);
  const p = ctx.Capability.profile();
  assert.ok(Math.abs(p.capacityMpx - 33.8) < 0.01, `capacidade ${p.capacityMpx}`);
  // Benchmark: all cores, 1 s windows.
  const [, size, threads, windowMs, windows] = ctx.benchCalls[0];
  assert.strictEqual(size, 1000);
  assert.strictEqual(threads, 4);
  assert.strictEqual(windowMs, 1000);
  assert.ok(windows >= 3);
});

test('75% rule on the reference TV', () => {
  const ctx = setup();
  measure(ctx, 68);
  const C = ctx.Capability;
  // Typical 352x240 @25 substream = 2.1 Mpx/s per camera.
  assert.ok(C.check(4, cams(4)).ok, '1:4');
  assert.ok(C.check(8, cams(8)).ok, '1:8 (7 in software)');
  assert.ok(!C.check(16, cams(16)).ok, '1:16 with 16 cameras');
  assert.ok(C.check(16, cams(4)).ok, '1:16 with only 4 cameras costs like 1:4');
  assert.ok(C.check(1, cams(16, cam(3840, 2160, 30))).ok, '1:1 sempre');
  // How far it goes: budget 25.35 Mpx/s; 12 cameras (25.34) fit, 13 do not.
  assert.ok(C.check(16, cams(12)).ok);
  assert.ok(!C.check(16, cams(13)).ok);
});

test('the cost uses the real resolution and the heaviest cameras first', () => {
  const ctx = setup();
  measure(ctx, 68);
  const C = ctx.Capability;
  // A 720p substream weighs ~23 Mpx/s: with 3 more light cameras, 1:4 overflows.
  const mix = [cam(1280, 720, 25), cam(), cam(), cam()];
  assert.ok(!C.check(4, mix).ok);
  // In 1:8 the large tile is native: 7 in software, the heaviest first.
  const r = C.check(8, mix);
  assert.ok(Math.abs(r.cost - (1280 * 720 * 25 + 3 * 352 * 240 * 25) / 1e6) < 0.01);
  // fps above 30 counts as 30.
  assert.ok(Math.abs(C.check(4, [cam(352, 240, 60)]).cost - 352 * 240 * 30 / 1e6) < 0.01);
});

test('profile is cached: the second start does not measure again', () => {
  const first = setup();
  measure(first, 68);
  const saved = Object.fromEntries(first.localStorage._data);

  const second = setup({ localStorage: saved });
  let measuredNow = null;
  second.Capability.ensure(() => assert.fail('should not measure'), (now) => { measuredNow = now; });
  assert.strictEqual(measuredNow, false);
  assert.strictEqual(second.benchCalls.length, 0);
  assert.ok(second.Capability.ready());
});

test('a different number of cores invalidates the cache', () => {
  const first = setup();
  measure(first, 68);
  const second = setup({ localStorage: Object.fromEntries(first.localStorage._data), hardwareConcurrency: 8 });
  assert.strictEqual(measure(second, 120), true);
  assert.strictEqual(second.benchCalls[0][2], 8);
});

test('if the measurement fails, assumes the reference TV without blocking the app', () => {
  const ctx = setup();
  let done = false;
  ctx.Capability.ensure(null, () => { done = true; });
  ctx.requests[0].fail();
  assert.ok(done);
  const p = ctx.Capability.profile();
  assert.ok(p.fallback);
  assert.strictEqual(p.capacityMpx, 33.8);
  assert.ok(ctx.Capability.check(8, cams(8)).ok);
});

test('decoder load: ms per frame x fps / cores', () => {
  const ctx = setup();
  measure(ctx, 68);
  const load = ctx.Capability.load([{ decode_ms: 40, fps: 25 }, { decode_ms: 40, fps: 25 }]);
  assert.strictEqual(load, 0.5);  // 2 x 1000 ms/s on 4 cores
});

test('a firmware update (new build) measures again', () => {
  const build = (model, version) => ({
    systeminfo: { getPropertyValue: (prop, ok) => { assert.strictEqual(prop, 'BUILD'); ok({ model, buildVersion: version }); } }
  });
  const first = setup();
  first.tizen = build('QN55LS03A', 'T-KSU2EAKUC-1000');
  measure(first, 68);
  assert.match(first.Capability.profile().key, /QN55LS03A\/T-KSU2EAKUC-1000/);

  const same = setup({ localStorage: Object.fromEntries(first.localStorage._data) });
  same.tizen = build('QN55LS03A', 'T-KSU2EAKUC-1000');
  let measured = null;
  same.Capability.ensure(null, (now) => { measured = now; });
  assert.strictEqual(measured, false, 'same firmware: cached');

  const updated = setup({ localStorage: Object.fromEntries(first.localStorage._data) });
  updated.tizen = build('QN55LS03A', 'T-KSU2EAKUC-1100');
  assert.strictEqual(measure(updated, 70), true, 'new firmware: measured again');
});

test('the build is optional: failure or exception reading it still measures', () => {
  for (const getPropertyValue of [
    (prop, ok, fail) => fail(new Error('denied')),
    () => { throw new Error('not supported'); }
  ]) {
    const ctx = setup();
    ctx.tizen = { systeminfo: { getPropertyValue } };
    assert.strictEqual(measure(ctx, 68), true);
  }
});

test('benchmark problems fall back to the reference TV', () => {
  // Empty clip, the benchmark does not start, or it reports a failure.
  const cases = [
    (ctx) => ctx.requests[0].respond(200, new ArrayBuffer(0)),
    (ctx) => { ctx.Module._bench_start = () => 0; ctx.requests[0].respond(200, new ArrayBuffer(100)); },
    (ctx) => { ctx.requests[0].respond(200, new ArrayBuffer(100)); ctx.Player.onBench({ ok: false }); },
    (ctx) => { ctx.requests[0].respond(200, new ArrayBuffer(100)); ctx.Player.onBench({ ok: true, mpx_per_s: 0 }); }
  ];
  for (const fail of cases) {
    const ctx = setup();
    let measuring = false;
    let measured = null;
    ctx.Capability.ensure(() => { measuring = true; }, (now) => { measured = now; });
    fail(ctx);
    assert.ok(measuring);
    assert.strictEqual(measured, true);
    assert.ok(ctx.Capability.profile().fallback);
  }
});

test('remeasure discards the cached profile', () => {
  const ctx = setup();
  measure(ctx, 68);
  let done = false;
  ctx.Capability.remeasure(() => { done = true; });
  assert.ok(!ctx.Capability.ready(), 'profile cleared while measuring');
  ctx.requests.at(-1).respond(200, new ArrayBuffer(100));
  ctx.Player.onBench({ ok: true, threads: 4, mpx_per_s: 80, windows: [80] });
  assert.ok(done);
  assert.strictEqual(ctx.Capability.profile().benchMpx, 80);
});

test('before any profile every layout is allowed; 1:1 always is', () => {
  const ctx = setup();
  assert.ok(ctx.Capability.check(16, cams(16)).ok);
  measure(ctx, 10);
  assert.ok(ctx.Capability.check(1, cams(16, cam(3840, 2160, 60))).ok);
  assert.ok(!ctx.Capability.check(4, cams(4, cam(3840, 2160, 60))).ok);
  // Load without a profile uses the TV cores.
  assert.strictEqual(setup().Capability.load([{ decode_ms: 40, fps: 25 }]), 0.25);
});
