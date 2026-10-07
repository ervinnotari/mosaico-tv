// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Ervin Notari Junior

/* TV performance profile: decides which mosaic layouts the TV can handle.
 *
 * On the first run (and after each firmware update) it decodes the
 * reference clip on all cores and measures megapixels per second.
 * A layout is available only if the cost of the cameras decoded in
 * software fits in 75% of the capacity. */
'use strict';

var Capability = (function () {
  var KEY = 'rtsp-player-capability';
  var BENCH_VERSION = 4;
  var CLIP = 'bench/ref_352x240.bin';
  // Measures in windows and keeps the best one: the app startup does not interfere.
  var WINDOW_MS = 1000;
  var WINDOWS = 6;

  // Calibrated on the reference TV (The Frame 2021, SDP1404): the clip measures
  // ~68 Mpx/s on 4 threads, and 16 real substreams saturate the TV at 33.8 Mpx/s
  // (network, depacketizer, WebGL and UI also cost).
  var LIVE_FACTOR = 33.8 / 68;
  var BUDGET = 0.75;

  // Assumed cost of a camera that has not played yet: typical DVR substream.
  var DEFAULT_STREAM = { w: 352, h: 240, fps: 25 };

  var profile = null;  // {key, cores, benchMpx, capacityMpx, at}
  var buildId = '';

  function cores() { return Math.max(1, Math.min(16, navigator.hardwareConcurrency || 4)); }

  function cacheKey() { return BENCH_VERSION + '|' + buildId + '|' + cores(); }

  function load() {
    try {
      var saved = JSON.parse(localStorage.getItem(KEY) || 'null');
      if (saved && saved.key === cacheKey() && saved.capacityMpx > 0) profile = saved;
    } catch (e) { /* no cache */ }
  }

  function save() {
    try { localStorage.setItem(KEY, JSON.stringify(profile)); } catch (e) { /* no space */ }
  }

  // Current firmware: changes with each TV update, which redoes the measurement.
  function readBuild(done) {
    if (!window.tizen || !tizen.systeminfo) { done(); return; }
    try {
      tizen.systeminfo.getPropertyValue('BUILD', function (b) {
        buildId = (b && b.model ? b.model + '/' : '') + (b && b.buildVersion || '');
        done();
      }, function () { done(); });
    } catch (e) { done(); }
  }

  function measure(done) {
    var xhr = new XMLHttpRequest();
    xhr.open('GET', CLIP);
    xhr.responseType = 'arraybuffer';
    xhr.onload = function () {
      var bytes = new Uint8Array(xhr.response || new ArrayBuffer(0));
      if (!bytes.length) { done(false); return; }
      var ptr = Module._malloc(bytes.length);
      Module.HEAPU8.set(bytes, ptr);
      Player.onBench = function (r) {
        Player.onBench = null;
        Module._free(ptr);
        if (!r.ok || !(r.mpx_per_s > 0)) { done(false); return; }
        profile = {
          key: cacheKey(),
          cores: r.threads,
          benchMpx: r.mpx_per_s,
          windows: r.windows,  // Mpx/s of each window, for diagnostics
          capacityMpx: r.mpx_per_s * LIVE_FACTOR,
          at: Date.now()
        };
        save();
        console.log('[capacidade] ' + JSON.stringify(profile));
        done(true);
      };
      if (!Module._bench_start(ptr, bytes.length, cores(), WINDOW_MS, WINDOWS)) {
        Player.onBench = null;
        Module._free(ptr);
        done(false);
      }
    };
    xhr.onerror = function () { done(false); };
    xhr.send();
  }

  // Cost of decoding one camera in software, in Mpx/s.
  function streamCost(cam) {
    var s = (cam && cam.subInfo) || DEFAULT_STREAM;
    var fps = Math.min(30, Math.max(1, s.fps || DEFAULT_STREAM.fps));
    return s.w * s.h * fps / 1e6;
  }

  // How many tiles of the layout use the software decoder.
  function softTiles(n) {
    if (n === 1) return 0;
    if (n === 8) return 7;  // the large tile uses the native player
    return n;
  }

  // Worst case: the heaviest cameras in the software tiles.
  function layoutCost(n, cams) {
    var k = Math.min(softTiles(n), cams.length);
    return cams.map(streamCost).sort(function (a, b) { return b - a; })
      .slice(0, k).reduce(function (s, c) { return s + c; }, 0);
  }

  return {
    BUDGET: BUDGET,

    ready: function () { return !!profile; },
    profile: function () { return profile; },

    // Ensures a profile (from the cache or measuring now). done(measuredNow).
    ensure: function (onMeasuring, done) {
      readBuild(function () {
        load();
        if (profile) { done(false); return; }
        if (onMeasuring) onMeasuring();
        measure(function (ok) {
          if (!ok) {
            // No measurement: assume the reference TV so the app does not hang.
            profile = { key: '', cores: cores(), benchMpx: 0, capacityMpx: 33.8, at: Date.now(), fallback: true };
          }
          done(true);
        });
      });
    },

    remeasure: function (done) {
      profile = null;
      try { localStorage.removeItem(KEY); } catch (e) { /* ok */ }
      this.ensure(null, done);
    },

    // {ok, cost, budget} for layout n with the current cameras.
    check: function (n, cams) {
      var budget = profile ? profile.capacityMpx * BUDGET : Infinity;
      var cost = layoutCost(n, cams);
      return { ok: n === 1 || cost <= budget, cost: cost, budget: budget };
    },

    // Current decoder load: sum of (ms per frame x fps) / cores.
    load: function (stats) {
      var busyMsPerSecond = 0;
      stats.forEach(function (s) { busyMsPerSecond += s.decode_ms * s.fps; });
      return busyMsPerSecond / 1000 / (profile ? profile.cores : cores());
    }
  };
})();
