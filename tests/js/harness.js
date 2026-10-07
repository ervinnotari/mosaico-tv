// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Ervin Notari Junior

/* Loads app scripts (app/js/*.js) in an isolated Node context, with test
 * doubles for what the TV browser provides: localStorage, navigator,
 * XMLHttpRequest, clock (setTimeout) and the WASM module (Module). */
'use strict';

const fs = require('fs');
const path = require('path');
const vm = require('vm');

const APP_JS = path.join(__dirname, '..', '..', 'app', 'js');

// Manual clock: timers fire only when the test advances the time.
function fakeClock() {
  let now = 0;
  let nextId = 1;
  const timers = new Map();
  return {
    now: () => now,
    setTimeout(fn, ms) {
      const id = nextId++;
      timers.set(id, { at: now + (ms || 0), fn });
      return id;
    },
    clearTimeout(id) { timers.delete(id); },
    // Advances the clock firing the timers in the right order.
    tick(ms) {
      const end = now + ms;
      for (;;) {
        let next = null;
        for (const [id, t] of timers) {
          if (t.at <= end && (!next || t.at < next.t.at)) next = { id, t };
        }
        if (!next) break;
        timers.delete(next.id);
        now = next.t.at;
        next.t.fn();
      }
      now = end;
    },
    pending: () => timers.size
  };
}

function fakeLocalStorage(initial) {
  const data = new Map(Object.entries(initial || {}));
  return {
    getItem: (k) => (data.has(k) ? data.get(k) : null),
    setItem: (k, v) => data.set(k, String(v)),
    removeItem: (k) => data.delete(k),
    clear: () => data.clear(),
    _data: data
  };
}

// Fake XHR: keeps the requests; the test decides the response. Synchronous
// reads of the texts (i18n/*.json) are answered with the packaged files, as
// on the TV, and are not recorded.
const APP = path.join(__dirname, '..', '..', 'app');
function fakeXhrClass(requests) {
  return class FakeXhr {
    constructor() { this.headers = {}; }
    open(method, url, async, user, pass) {
      Object.assign(this, { method, url, async, user, pass });
      if (!this.isText()) requests.push(this);
    }
    isText() { return this.async === false && /^i18n\/[\w-]+\.json$/.test(this.url); }
    setRequestHeader(k, v) { this.headers[k] = v; }
    send(body) {
      this.body = body;
      this.sent = true;
      if (!this.isText()) return;
      const file = path.join(APP, this.url);
      this.status = fs.existsSync(file) ? 200 : 404;
      this.responseText = this.status === 200 ? fs.readFileSync(file, 'utf8') : '';
    }
    respond(status, response) {
      this.status = status;
      this.response = response;
      this.responseText = typeof response === 'string' ? response : '';
      if (this.onload) this.onload();
    }
    fail() { if (this.onerror) this.onerror(); }
  };
}

// Fake WASM module: records the calls and allows simulating events.
function fakeModule(calls) {
  return {
    _player_start: () => 1,
    ccall(name, ret, types, args) {
      calls.push([name].concat(args));
      return this.startResult === undefined ? 1 : this.startResult;
    },
    _player_stop: (slot) => calls.push(['player_stop', slot]),
    _player_set_rect: (...a) => calls.push(['player_set_rect'].concat(a)),
    _player_clear_canvas: () => calls.push(['player_clear_canvas']),
    _player_running: () => 0
  };
}

/* load(['store.js', ...], {localStorage, hardwareConcurrency, language}) returns the
 * context with the app's global objects (Store, Capability, Player...) and the
 * test doubles: ctx.clock, ctx.requests (XHRs), ctx.wasmCalls. */
function load(files, opts) {
  opts = opts || {};
  const clock = fakeClock();
  const requests = [];
  const wasmCalls = [];
  const ctx = {
    console: opts.verbose ? console : { log() {}, warn() {}, error() {} },
    localStorage: fakeLocalStorage(opts.localStorage),
    navigator: { hardwareConcurrency: opts.hardwareConcurrency || 4, language: opts.language || 'en-US' },
    XMLHttpRequest: fakeXhrClass(requests),
    setTimeout: clock.setTimeout,
    clearTimeout: clock.clearTimeout,
    Date, JSON, Math, Uint8Array, ArrayBuffer, Int32Array, Array, Object, String,
    unescape, encodeURIComponent, decodeURIComponent,
    btoa: (s) => Buffer.from(s, 'binary').toString('base64'),
    clock, requests, wasmCalls
  };
  ctx.window = ctx;
  vm.createContext(ctx);
  // The texts (i18n.js) come before everything, as in index.html. player.js
  // defines window.Module; the tests replace it with the fake functions.
  if (files.indexOf('i18n.js') < 0) files = ['i18n.js'].concat(files);
  for (const f of files) {
    vm.runInContext(fs.readFileSync(path.join(APP_JS, f), 'utf8'), ctx, { filename: path.join(APP_JS, f) });
  }
  if (ctx.Module) Object.assign(ctx.Module, fakeModule(wasmCalls));
  return ctx;
}

// Values created in the isolated context have other prototypes (Array, Object);
// to compare with deepStrictEqual, copy them into the test context.
function plain(v) { return JSON.parse(JSON.stringify(v)); }

module.exports = { load, plain };
