// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Ervin Notari Junior

'use strict';

const test = require('node:test');
const assert = require('node:assert');
const { load, plain } = require('./harness');

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

test('layouts use the whole measured capacity: The Frame and the Crystal UHD', () => {
  // Reference TV (68 Mpx/s on the clip = 33.8 of capacity).
  let ctx = setup();
  measure(ctx, 68);
  let C = ctx.Capability;
  // Typical 352x240 @25 substream = 2.1 Mpx/s per camera.
  assert.ok(C.check(4, cams(4)).ok, '1:4');
  assert.ok(C.check(8, cams(8)).ok, '1:8 (7 in software)');
  assert.ok(C.check(16, cams(16)).ok, '1:16 with 16 cameras (33.8 of 33.8)');
  assert.ok(!C.check(16, cams(16, cam(352, 240, 30))).ok, '1:16 at 30 fps does not fit');
  assert.ok(C.check(1, cams(16, cam(3840, 2160, 30))).ok, '1:1 always');

  // Crystal UHD 2021 (UN50AU7700): ~20.5 Mpx/s on the clip = 10.2 of capacity.
  ctx = setup();
  measure(ctx, 20.5);
  C = ctx.Capability;
  assert.ok(C.check(4, cams(4)).ok, '1:4 (8.4 of 10.2)');
  assert.ok(!C.check(8, cams(8)).ok, '1:8 (7 tiles, 14.8) does not fit');
  assert.ok(C.check(8, cams(4)).ok, '1:8 with only 4 cameras costs like 1:4');
});

test('the cost uses the real resolution and the heaviest cameras first', () => {
  const ctx = setup();
  measure(ctx, 68);
  const C = ctx.Capability;
  // A 1080p substream weighs ~52 Mpx/s: it alone overflows the TV.
  const mix = [cam(1920, 1080, 25), cam(), cam(), cam()];
  assert.ok(!C.check(4, mix).ok);
  // In 1:8 the large tile is native: 7 in software, the heaviest first.
  const r = C.check(8, mix);
  assert.ok(Math.abs(r.cost - (1920 * 1080 * 25 + 3 * 352 * 240 * 25) / 1e6) < 0.01);
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

// Software tiles as the stats report them: decode_ms x fps / 1000 = share of a core.
const tile = (slot, decodeMs, fps, economy) => ({ slot, decode_ms: decodeMs, fps, economy: !!economy });

test('governor: a TV at its limit puts the last non-focused tile in economy mode', () => {
  const ctx = setup();
  measure(ctx, 20.5);  // 4 cores
  const g = ctx.Capability.governor();
  // 4 tiles at 25 fps x 38 ms = 0.95 of a core each: load 95%.
  const busy = [0, 1, 2, 3].map((s) => tile(s, 38, 25));
  let r = g.update(busy, 3);
  assert.ok(Math.abs(r.load - 0.95) < 1e-9);
  assert.deepStrictEqual(plain(r.changes), [], 'waits a second cycle');
  r = g.update(busy, 3);
  assert.deepStrictEqual(plain(r.changes), [{ slot: 2, economy: true }], 'never the focused tile (3)');
  // Still above the limit two cycles later: one more tile.
  const next = busy.map((t) => (t.slot === 2 ? tile(2, 38, 0.5, true) : t));
  assert.deepStrictEqual(plain(g.update(next, 3).changes), []);
  assert.deepStrictEqual(plain(g.update(next, 3).changes), [], 'load 71.7%: below the limit');
});

test('governor: tiles come back live only when their full cost fits', () => {
  const ctx = setup();
  measure(ctx, 20.5);
  const g = ctx.Capability.governor();
  // Learns the live cost of slot 1 (0.95 of a core), then it goes to economy.
  g.update([tile(0, 38, 25), tile(1, 38, 25)], -1);
  const calm = [tile(0, 38, 25), tile(1, 38, 0.5, true)];  // load 24.2%
  assert.deepStrictEqual(plain(g.update(calm, -1).changes), []);
  assert.deepStrictEqual(plain(g.update(calm, -1).changes), [{ slot: 1, economy: false }],
    'third calm cycle (counting the first): 24% + 24% < 75%');

  // Here the economy tile would push the load over 75%: it stays in economy.
  const g2 = ctx.Capability.governor();
  g2.update([tile(0, 38, 25), tile(1, 38, 25), tile(2, 38, 25), tile(3, 38, 25)], -1);
  const near = [tile(0, 38, 25), tile(1, 38, 25), tile(2, 38, 25), tile(3, 38, 0.5, true)];  // 71.7%
  for (let i = 0; i < 5; i++) assert.deepStrictEqual(plain(g2.update(near, -1).changes), []);
});

test('governor: the focused tile returns first; reset forgets the history', () => {
  const ctx = setup();
  measure(ctx, 68);
  const g = ctx.Capability.governor();
  const eco = [tile(0, 10, 0.5, true), tile(1, 10, 0.5, true), tile(2, 10, 25)];
  for (let i = 0; i < 2; i++) g.update(eco, 1);
  assert.deepStrictEqual(plain(g.update(eco, 1).changes), [{ slot: 1, economy: false }]);
  // Without a profile it assumes 4 cores; reset clears the counters.
  const fresh = setup().Capability.governor();
  const busy = [0, 1, 2, 3].map((s) => tile(s, 38, 25));
  fresh.update(busy, -1);
  fresh.reset();
  assert.deepStrictEqual(plain(fresh.update(busy, -1).changes), [], 'counting starts again');
  assert.deepStrictEqual(plain(fresh.update(busy, -1).changes), [{ slot: 3, economy: true }]);
});

test('governor: a tile that flaps waits twice as long before returning', () => {
  const ctx = setup();
  measure(ctx, 20.5);
  const g = ctx.Capability.governor();
  const busy = [0, 1, 2, 3].map((s) => tile(s, 38, 25));
  const calm = [tile(0, 20, 25), tile(1, 20, 25), tile(2, 20, 25), tile(3, 38, 1, true)];  // 38.4%
  // Returns the number of calm cycles until slot 3 comes back.
  const cyclesToReturn = () => {
    for (let i = 1; i <= 100; i++) {
      if (g.update(calm, -1).changes.some((c) => c.slot === 3 && !c.economy)) return i;
    }
    return Infinity;
  };
  const overload = () => {
    g.update(busy, -1);
    assert.deepStrictEqual(plain(g.update(busy, -1).changes), [{ slot: 3, economy: true }]);
  };
  overload();
  const first = cyclesToReturn();
  overload();  // back to economy right after returning: flapping
  const second = cyclesToReturn();
  overload();
  const third = cyclesToReturn();
  assert.ok(first >= 3 && first <= 5, 'first hold ~5 cycles: ' + first);
  assert.ok(second >= 9 && second <= 10, 'then ~10: ' + second);
  assert.ok(third >= 19 && third <= 20, 'then ~20: ' + third);
});
