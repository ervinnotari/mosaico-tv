// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Ervin Notari Junior

/* Bridge to the WASM module and management of the 16 video slots.
 *
 * The UI only says what each slot should show (Player.show); this module
 * stops what changed, starts what is missing and reconnects by itself when a
 * camera drops. Mode 0 = TV native player (one at a time), 1 = software decoder. */
'use strict';

var Player = (function () {
  var MAX_SLOTS = 16;
  var RETRY_MS = 5000;
  var ready = false;
  var slots = [];
  for (var i = 0; i < MAX_SLOTS; i++) {
    // fatalKey: camera that failed in a way reconnecting does not fix
    // (e.g. H.265 in the mosaic); no retry until the screen changes.
    slots.push({ want: null, running: false, key: null, mode: -1, stopping: false, retry: null,
                 fatalKey: null });
  }

  // Hikvision DVRs/cameras send a key frame right away when asked
  // (ISAPI). Without it, full screen waits for the next one, which can take seconds.
  function requestKeyFrame(url) {
    var m = /^rtsp:\/\/(?:([^:@/]*)(?::([^@/]*))?@)?([^:/]+)(?::\d+)?\/Streaming\/(?:Unicast\/)?channels\/(\d+)/i.exec(String(url));
    if (!m) return;
    var xhr = new XMLHttpRequest();
    // Plain HTTP on purpose: the DVR is on the local network and speaks only
    // HTTP (or HTTPS with a self-signed certificate the TV rejects), like the
    // RTSP stream itself. A failure only delays the first frame.
    xhr.open('PUT', 'http://' + m[3] + '/ISAPI/Streaming/channels/' + m[4] + '/requestKeyFrame', true, // NOSONAR

      decodeURIComponent(m[1] || ''), decodeURIComponent(m[2] || ''));
    xhr.timeout = 3000;
    xhr.onload = function () { console.log('[keyframe] ' + m[4] + ' HTTP ' + xhr.status); };
    xhr.send();
  }

  function setStatus(slot, status, text) {
    if (Player.onStatus) Player.onStatus(slot, status, text || '');
  }

  // WASM errors come with a code (translated here); the native player ones
  // only with the technical detail.
  function errorText(kind, data) {
    if (data.code) return I18n.error(data.code, data.text);
    if (kind === 'player') return I18n.error('player_error', data.text);
    return data.text || '';
  }

  function nativeBusy(exceptSlot) {
    return slots.some(function (s, i) { return i !== exceptSlot && s.running && s.mode === 0; });
  }

  function start(i) {
    var s = slots[i];
    var w = s.want;
    if (w.mode === 1) Module._player_set_rect(i, w.rect.x, w.rect.y, w.rect.w, w.rect.h);
    var ok = Module.ccall('player_start', 'number', ['number', 'string', 'number'], [i, w.url, w.mode]);
    if (!ok) { scheduleRetry(i); return; }
    s.running = true;
    s.key = w.key;
    s.mode = w.mode;
    s.stopping = false;
    setStatus(i, 'connecting', I18n.t('player.connecting'));
  }

  // Full-screen watchdog: sometimes the TV player does not start. With no
  // picture in FIRST_FRAME_MS, restart the session right away (no RETRY_MS wait).
  var FIRST_FRAME_MS = 6000;
  function watchFirstFrame(i) {
    var s = slots[i];
    var key = s.key;
    clearTimeout(s.firstFrameTimer);
    s.firstFrameTimer = setTimeout(function () {
      if (!s.running || s.stopping || s.key !== key) return;
      console.warn('[player ' + i + '] no picture in ' + FIRST_FRAME_MS + ' ms; restarting');
      s.restartNow = true;
      s.stopping = true;
      Module._player_stop(i);
    }, FIRST_FRAME_MS);
  }

  function scheduleRetry(i) {
    var s = slots[i];
    if (s.retry) return;
    s.retry = setTimeout(function () { s.retry = null; reconcile(); }, RETRY_MS);
  }

  function reconcile() {
    if (!ready) return;
    // First stop what changed; the slot restarts only after onStopped.
    slots.forEach(function (s, i) {
      if (s.running && !s.stopping && (!s.want || s.want.key !== s.key)) {
        s.stopping = true;
        Module._player_stop(i);
      }
    });
    slots.forEach(function (s, i) {
      if (!s.want) {
        if (s.retry) { clearTimeout(s.retry); s.retry = null; }
        return;
      }
      if (s.running) {
        if (s.key === s.want.key && s.mode === 1) {
          var r = s.want.rect;
          Module._player_set_rect(i, r.x, r.y, r.w, r.h);
        }
        return;
      }
      if (s.retry || s.fatalKey === s.want.key) return;
      // The TV plays only one native video: wait for the previous one to stop.
      if (s.want.mode === 0 && nativeBusy(i)) return;
      start(i);
    });
  }

  var Player = {
    MAX_SLOTS: MAX_SLOTS,
    onStatus: null,
    onReady: null,

    isReady: function () { return ready; },

    // wants[i] = {key, url, mode, rect:{x,y,w,h}} or null. `clear` clears the
    // canvas (layout change).
    show: function (wants, clear) {
      for (var i = 0; i < MAX_SLOTS; i++) {
        var w = wants[i] || null;
        var s = slots[i];
        var changed = !s.want || !w || s.want.key !== w.key;
        s.want = w;
        if (changed && s.retry) { clearTimeout(s.retry); s.retry = null; }
        if (clear) s.fatalKey = null;  // new screen: worth trying again
      }
      if (ready && clear) Module._player_clear_canvas();
      reconcile();
    },

    stopAll: function () { this.show([], true); },

    discover: function (timeoutMs, prefix) {
      if (!ready) return false;
      return Module.ccall('onvif_discover', 'number', ['number', 'string'], [timeoutMs, prefix || '']) === 1;
    },

    onDiscovery: null,      // (from, xml)
    onDiscoveryDone: null,
    onBench: null,          // result of the performance test
    onStats: null           // (slot, {width, height, fps, decode_ms, mode, ...})
  };

  // ---- events from the WASM: Module.onEvent(slot, kind, json) ----

  function isActive(slot) {
    return slot >= 0 && slots[slot].running && !slots[slot].stopping;
  }

  var EVENTS = {
    'discovery-match': function (slot, data) {
      if (Player.onDiscovery) Player.onDiscovery(data.from, data.xml);
    },
    'discovery-done': function () {
      if (Player.onDiscoveryDone) Player.onDiscoveryDone();
    },
    bench: function (slot, data) {
      if (Player.onBench) Player.onBench(data);
    },
    stats: function (slot, data, kind, json) {
      console.log('[stats ' + slot + '] ' + json);
      if (!isActive(slot)) return;
      var live = data.fps > 0;
      setStatus(slot, live ? 'live' : 'connecting', live ? '' : I18n.t('player.waiting'));
      if (Player.onStats) Player.onStats(slot, data);
    },
    play: function (slot, data) {
      if (data.mode === 'native' && slots[slot].want) {
        requestKeyFrame(slots[slot].want.url);
        watchFirstFrame(slot);
      }
    },
    'first-frame': function (slot, data) {
      console.log('[first-frame ' + slot + '] ' + data.ms + ' ms after PLAY');
      clearTimeout(slots[slot].firstFrameTimer);
    }
  };

  // Logs and errors ("rtsp", "player", "fatal"...): an error shows on the tile.
  function onLogEvent(slot, data, kind) {
    if (kind === 'fatal' && slot >= 0) slots[slot].fatalKey = slots[slot].key;
    console.log('[' + kind + ' ' + slot + '] ' + (data.code ? data.code + ': ' : '') + data.text);
    if (!data.ok && slot >= 0 && !slots[slot].stopping) setStatus(slot, 'error', errorText(kind, data));
  }

  // ---- callbacks called by the WASM ----
  window.Module = {
    print: function (t) { console.log('[wasm] ' + t); },
    printErr: function (t) { console.warn('[wasm] ' + t); },
    onReady: function () {
      ready = true;
      reconcile();
      if (Player.onReady) Player.onReady();
    },
    onEvent: function (slot, kind, json) {
      var data;
      try { data = JSON.parse(json); } catch (e) { return; }  // malformed event: ignored
      (EVENTS[kind] || onLogEvent)(slot, data, kind, json);
    },
    onStopped: function (slot) {
      var s = slots[slot];
      var wasWanted = s.want && s.want.key === s.key && !s.stopping;
      var fatal = s.fatalKey !== null && s.fatalKey === s.key;
      var restartNow = s.restartNow;
      clearTimeout(s.firstFrameTimer);
      s.running = false;
      s.stopping = false;
      s.restartNow = false;
      s.key = null;
      s.mode = -1;
      if (fatal) { /* keep the error message, no reconnect */ }
      else if (restartNow) { /* the reconcile below restarts right away */ }
      else if (wasWanted) scheduleRetry(slot);  // dropped by itself: try again
      else setStatus(slot, 'idle', '');
      reconcile();
    },
    onAbort: function (what) { console.error('[wasm] abort: ' + what); }
  };

  return Player;
})();
