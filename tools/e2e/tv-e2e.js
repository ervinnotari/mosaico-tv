#!/usr/bin/env node
// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Ervin Notari Junior

/* End-to-end test on the TV, without a real camera: starts two RTSP test
 * servers on the PC (H.264 and H.265), opens the app in debug mode and drives
 * everything through DevTools (CDP), like a user with the remote control.
 *
 * Scenarios: live H.264 mosaic; H.265 in the mosaic (error without reconnect);
 * H.265 full screen in the native player; back to the mosaic; background and
 * return; language. The cameras saved on the TV are kept before and restored
 * at the end.
 *
 * Usage: node tools/e2e/tv-e2e.js [tv-ip=192.168.1.17] [--onvif]
 *   --onvif  also runs the ONVIF search (needs an ONVIF device on the network)
 * Requirements: app installed (scripts\install.bat), Developer Mode, ffmpeg.
 */
'use strict';

const { spawn, execFileSync } = require('child_process');
const fs = require('fs');
const os = require('os');
const path = require('path');

if (typeof fetch !== 'function' || typeof WebSocket !== 'function') {
  console.error(`needs Node 22 or newer (fetch and WebSocket); this is ${process.version}`);
  process.exit(2);
}

const ROOT = path.join(__dirname, '..', '..');
const TV = process.argv.slice(2).find((a) => !a.startsWith('--')) || '192.168.1.17';
const WITH_ONVIF = process.argv.includes('--onvif');
const SERIAL = `${TV}:26101`;
// The app id is defined only in app/config.xml.
const APP_ID = (fs.readFileSync(path.join(ROOT, 'app', 'config.xml'), 'utf8')
  .match(/<tizen:application id="([^"]+)"/) || [])[1];
const SDB = process.env.SDB || path.join(os.homedir(), '.tizen-extension-platform', 'server', 'sdktools', 'data', 'tools', 'sdb.exe');
const MEDIA = path.join(ROOT, 'build', 'e2e');
const PORT_H264 = 8555;
const PORT_H265 = 8554;

const sleep = (ms) => new Promise((r) => setTimeout(r, ms));
const results = [];

function sdb(...args) {
  return execFileSync(SDB, ['-s', SERIAL, ...args], { encoding: 'utf8', timeout: 30000 });
}

// ---------------------------------------------------------------- media

function ensureClip(file, codec) {
  if (fs.existsSync(file)) return;
  fs.mkdirSync(MEDIA, { recursive: true });
  const enc = codec === 'h265'
    ? ['-c:v', 'libx265', '-x265-params', 'keyint=25:min-keyint=25:repeat-headers=1:aud=1:log-level=error', '-f', 'hevc']
    : ['-c:v', 'libx264', '-x264-params', 'keyint=25:min-keyint=25:aud=1', '-f', 'h264'];
  // 352x240: the H.264 becomes a "substream" and fits the budget of any layout.
  const size = codec === 'h265' ? '1280x720' : '352x240';
  execFileSync('ffmpeg', ['-hide_banner', '-loglevel', 'error', '-y', '-f', 'lavfi',
    '-i', `testsrc2=size=${size}:rate=25`, '-t', '20', ...enc, '-pix_fmt', 'yuv420p', file]);
}

// RTSP test server; counts the connections (PLAY) to know whether the app
// reconnected.
function startServer(file, port) {
  const proc = spawn(process.execPath, [path.join(ROOT, 'tools', 'rtsp-test-server', 'server.js'), file, String(port)]);
  // methods: RTSP commands in the order they arrived (to check OPTIONS).
  const server = { proc, plays: 0, teardowns: 0, closed: 0, methods: [] };
  proc.stdout.on('data', (d) => {
    for (const line of d.toString().split('\n')) {
      const m = line.match(/\] ([A-Z_]+) rtsp:/);
      if (m) server.methods.push(m[1]);
      if (/ PLAY /.test(line)) server.plays++;
      if (/ closed$/.test(line.trim())) server.closed++;
    }
  });
  return server;
}

function localIp() {
  // PC IP on the same subnet as the TV.
  const prefix = TV.split('.').slice(0, 3).join('.') + '.';
  const ip = Object.values(os.networkInterfaces()).flat()
    .find((i) => i.family === 'IPv4' && i.address.startsWith(prefix));
  if (!ip) throw new Error(`no PC network interface on the subnet ${prefix}*`);
  return ip.address;
}

// ------------------------------------------------------------------ CDP

// Opening in debug mode sometimes fails on the TV: try up to 3 times.
async function launchDebug() {
  let lastError = '';
  for (let attempt = 1; attempt <= 3; attempt++) {
    try { sdb('shell', '0', 'was_kill', APP_ID); } catch (e) { /* was not open */ }
    await sleep(1000);
    const out = sdb('shell', '0', 'debug', APP_ID);
    const port = (out.match(/port: (\d+)/) || [])[1];
    if (!port) { lastError = 'did not open in debug mode: ' + out.trim(); continue; }
    sdb('forward', `tcp:${port}`, `tcp:${port}`);
    for (let i = 0; i < 20; i++) {
      try {
        const targets = await (await fetch(`http://127.0.0.1:${port}/json`)).json();
        if (targets[0]) return targets[0].webSocketDebuggerUrl;
      } catch (e) { /* still starting */ }
      await sleep(500);
    }
    lastError = 'DevTools did not answer';
  }
  throw new Error(lastError);
}

async function connect(wsUrl) {
  const ws = new WebSocket(wsUrl);
  await new Promise((resolve, reject) => { ws.onopen = resolve; ws.onerror = reject; });
  let id = 0;
  const pending = new Map();
  ws.onmessage = (ev) => {
    const msg = JSON.parse(ev.data);
    if (msg.id && pending.has(msg.id)) { pending.get(msg.id)(msg); pending.delete(msg.id); }
  };
  const send = (method, params = {}) => new Promise((resolve) => {
    const msgId = ++id;
    pending.set(msgId, resolve);
    ws.send(JSON.stringify({ id: msgId, method, params }));
  });
  const KEYS = { Left: 37, Up: 38, Right: 39, Down: 40, Enter: 13, Back: 10009 };
  return {
    // awaitPromise: waits for the Promise returned by the expression.
    async js(expr, awaitPromise) {
      const r = await send('Runtime.evaluate', { expression: expr, returnByValue: true, awaitPromise: !!awaitPromise });
      if (r.result && r.result.exceptionDetails) {
        throw new Error('JS: ' + (r.result.exceptionDetails.exception || {}).description);
      }
      return r.result && r.result.result ? r.result.result.value : undefined;
    },
    async key(name) {
      const code = KEYS[name];
      await send('Input.dispatchKeyEvent', { type: 'rawKeyDown', windowsVirtualKeyCode: code, nativeVirtualKeyCode: code });
      await send('Input.dispatchKeyEvent', { type: 'keyUp', windowsVirtualKeyCode: code, nativeVirtualKeyCode: code });
      await sleep(250);
    },
    // Waits for the expression to become true; returns the last value.
    async until(expr, ms) {
      const end = Date.now() + ms;
      let v;
      while (Date.now() < end) {
        v = await this.js(expr).catch(() => undefined);
        if (v) return v;
        await sleep(300);
      }
      return v;
    },
    close: () => ws.close()
  };
}

// --------------------------------------------------------------- report

async function step(name, fn) {
  const t0 = Date.now();
  try {
    await fn();
    results.push({ name, ok: true, ms: Date.now() - t0 });
    console.log(`  ok    ${name} (${Date.now() - t0} ms)`);
  } catch (e) {
    results.push({ name, ok: false, error: e.message });
    console.log(`  FAIL  ${name}: ${e.message}`);
  }
}

function check(cond, message) { if (!cond) throw new Error(message); }

const TILE = (i) => `document.querySelector('.tile[data-index="${i}"]')`;
const STATUS = (i) => `(${TILE(i)} || {getAttribute(){return null}}).getAttribute('data-status')`;
const RERENDER = "document.getElementById('tb-cameras').click(); document.getElementById('cam-back').click();";

// ------------------------------------------------------------- scenario

async function main() {
  const h264 = path.join(MEDIA, 'e2e_352x240.h264');
  const h265 = path.join(MEDIA, 'e2e_1280x720.hevc');
  ensureClip(h264, 'h264');
  ensureClip(h265, 'h265');
  const pc = localIp();
  const srv264 = startServer(h264, PORT_H264);
  const srv265 = startServer(h265, PORT_H265);
  // The servers stop on any exit, including on error.
  process.on('exit', () => { srv264.proc.kill(); srv265.proc.kill(); });
  await sleep(1500);

  console.log(`TV ${TV}, test servers on ${pc}`);
  try { execFileSync(SDB, ['connect', TV], { timeout: 15000 }); } catch (e) { /* already connected */ }
  const app = await connect(await launchDebug());
  let saved = null;

  try {
    await step('app loads and measures the performance', async () => {
      check(await app.until('Player.isReady() && Capability.ready()', 30000), 'module or profile did not become ready');
    });

    // Saves what the user had and sets up the test scenario.
    saved = await app.js("JSON.stringify({cams: localStorage.getItem('rtsp-player-v1')})");
    await app.js(`(function(){
      Store.cameras().slice().forEach(function(c){ Store.remove(c.id); });
      Store.add({name:'E2E H.264', main:'rtsp://${pc}:${PORT_H264}/test', sub:'rtsp://${pc}:${PORT_H264}/test', source:'test'});
      Store.add({name:'E2E H.265', main:'rtsp://${pc}:${PORT_H265}/test', sub:'rtsp://${pc}:${PORT_H265}/test', source:'test'});
      Store.setLayout(4); Store.setPage(0); ${RERENDER}
    })()`);

    await step('mosaic: software H.264 goes live', async () => {
      check(await app.until(`${STATUS(0)} === 'live'`, 15000), `status ${await app.js(STATUS(0))}`);
    });

    await step('RTSP: OPTIONS before DESCRIBE, then SETUP and PLAY', async () => {
      const seq = srv264.methods.slice(0, 4).join(' > ');
      check(seq === 'OPTIONS > DESCRIBE > SETUP > PLAY', `sequence: ${seq}`);
    });

    await step('mosaic: H.265 shows the notice and does not keep reconnecting', async () => {
      const text = await app.until(`${STATUS(1)} === 'error' && ${TILE(1)}.querySelector('.tile-status').textContent`, 15000);
      check(text && /H\.265/.test(text), `notice: ${text}`);
      // The app only does DESCRIBE on H.265 (never reaches PLAY); a new
      // connection would show up as one more "connected" on the server.
      const closed = srv265.closed;
      await sleep(12000);
      check(srv265.closed === closed, `reconnected ${srv265.closed - closed} time(s)`);
    });

    await step('full screen: H.265 on the TV native player', async () => {
      await app.js(`Nav.focus(${TILE(1)})`);
      await app.key('Enter');
      const ok = await app.until(`(function(v){ return v.readyState === 4 && v.videoWidth === 1280 && v.currentTime > 2; })(document.getElementById('video'))`, 20000);
      check(ok, 'video: ' + JSON.stringify(await app.js(`(function(v){return {rs:v.readyState,w:v.videoWidth,t:v.currentTime}})(document.getElementById('video'))`)));
    });

    await step('Back returns to the live mosaic', async () => {
      await app.key('Back');
      check(await app.until(`!document.body.classList.contains('zoomed') && ${STATUS(0)} === 'live'`, 15000), 'mosaic did not come back');
    });

    await step('Back on the mosaic asks before exiting (focus on Cancel)', async () => {
      await app.js(`Nav.focus(${TILE(0)})`);
      await app.key('Back');
      const ok = await app.until(`document.getElementById('exit-dialog').classList.contains('show') && Nav.current().id === 'exit-cancel' && !document.hidden && Module._player_running(0) === 1`, 5000);
      check(ok, 'box did not open, wrong focus or the app exited');
    });

    await step('Back with the box open stays in the app', async () => {
      await app.key('Back');
      const ok = await app.until(`!document.getElementById('exit-dialog').classList.contains('show') && Nav.current().classList.contains('tile') && ${STATUS(0)} === 'live'`, 5000);
      check(ok, 'box did not close or the focus did not return to the tile');
    });

    await step('Cancel closes the box', async () => {
      await app.key('Back');
      await app.until(`Nav.current().id === 'exit-cancel'`, 3000);
      await app.key('Enter');
      check(await app.until(`!document.getElementById('exit-dialog').classList.contains('show')`, 3000), 'box stayed open');
    });

    await step('About: credits, third-party and project licenses rendered', async () => {
      await app.js("document.getElementById('tb-cameras').click(); document.getElementById('cam-about').click()");
      const credits = await app.until(`document.querySelector('#about-doc table') && document.querySelector('#about-doc h1').textContent`, 5000);
      check(credits === 'Credits', 'credits without table or title: ' + credits);
      check(await app.js(`/Ervin Notari Junior/.test(document.getElementById('about-doc').textContent)`), 'author missing');
      await app.key('Right');  // Third-party licenses tab
      await app.key('Enter');
      const third = await app.until(`document.querySelectorAll('#about-doc pre.md-code').length`, 5000);
      check(third >= 4, `third-party licenses: ${third} blocks`);
      await app.key('Right');  // Project license tab
      await app.key('Enter');
      check(await app.until(`/Apache License/.test(document.getElementById('about-doc').textContent) && /Ervin Notari Junior/.test(document.getElementById('about-doc').textContent)`, 5000), 'project license');
      await app.key('Down');  // enters the text and scrolls
      await app.key('Down');
      check(await app.js(`document.getElementById('about-doc').scrollTop > 0`), 'text did not scroll');
      await app.key('Back');
      await app.key('Back');
      check(await app.until(`document.querySelector('.screen.active').id === 'screen-mosaic' && ${STATUS(0)} === 'live'`, 15000), 'did not return to the live mosaic');
    });

    await step('background disconnects everything', async () => {
      sdb('shell', '0', 'execute', 'org.tizen.browser');
      const ok = await app.until('document.hidden && [0,1,2,3].every(function(i){ return !Module._player_running(i); })', 15000);
      check(ok, 'cameras stayed connected');
    });

    await step('on return, reconnects by itself', async () => {
      sdb('shell', '0', 'execute', APP_ID);
      check(await app.until(`!document.hidden && ${STATUS(0)} === 'live'`, 20000), `status ${await app.js(STATUS(0))}`);
    });

    await step('language: follows the TV and switches the texts without reconnecting', async () => {
      const locale = await app.js(`new Promise(function (ok) { tizen.systeminfo.getPropertyValue('LOCALE', function (v) { ok(v.language); }, function () { ok(navigator.language); }); })`, true);
      const expected = /^pt/i.test(locale) ? 'pt' : 'en';
      check(await app.js('I18n.language()') === expected, `LOCALE ${locale}, app in ${await app.js('I18n.language()')}`);
      const texts = async () => app.js(`[document.documentElement.lang, document.getElementById('tb-cameras').title, document.querySelector('#empty h1').textContent]`);
      await app.js(`I18n.setLanguage('en')`);
      const en = await texts();
      await app.js(`I18n.setLanguage('pt-BR')`);
      const pt = await texts();
      await app.js(`I18n.setLanguage('${expected}')`);
      check(JSON.stringify(en) === JSON.stringify(['en', 'Settings', 'No cameras yet']), 'en: ' + JSON.stringify(en));
      check(JSON.stringify(pt) === JSON.stringify(['pt-BR', 'Configurações', 'Nenhuma câmera cadastrada']), 'pt: ' + JSON.stringify(pt));
      check(await app.until(`${STATUS(0)} === 'live'`, 10000), 'mosaic stopped when switching the language');
    });

    // Last: confirm the exit. The Tizen exit function is replaced by a
    // marker so the app does not close in the middle of the test.
    await step('Exit confirms: stops the cameras and closes the app', async () => {
      await app.js("window.__exited = false; tizen.application.getCurrentApplication = function () { return { exit: function () { window.__exited = true; } }; }");
      await app.js(`Nav.focus(${TILE(0)})`);
      await app.key('Back');
      await app.until(`Nav.current().id === 'exit-cancel'`, 3000);
      await app.key('Left');
      check(await app.js(`Nav.current().id`) === 'exit-confirm', 'arrow did not move to Exit');
      await app.key('Enter');
      const ok = await app.until('window.__exited && [0,1,2,3].every(function(i){ return !Module._player_running(i); })', 5000);
      check(ok, 'did not call exit or left cameras connected');
    });

    if (WITH_ONVIF) {
      await step('ONVIF search finds a device', async () => {
        await app.js("window.__e2eFound = []; var prev = Player.onDiscovery; Player.onDiscovery = function(f, x){ window.__e2eFound.push(f); if (prev) prev(f, x); }; Player.discover(4000, '')");
        const found = await app.until('window.__e2eFound.length', 8000);
        check(found, 'no answer');
      });
    }
  } finally {
    // Restores the user's cameras and the normal app.
    if (saved) {
      const { cams } = JSON.parse(saved);
      await app.js(`(function(){ ${cams === null ? "localStorage.removeItem('rtsp-player-v1')" : `localStorage.setItem('rtsp-player-v1', ${JSON.stringify(cams)})`}; location.reload(); })()`).catch(() => {});
    }
    app.close();
    srv264.proc.kill();
    srv265.proc.kill();
  }

  const failed = results.filter((r) => !r.ok);
  console.log(`\n${results.length - failed.length}/${results.length} scenarios passed`);
  process.exit(failed.length ? 1 : 0);
}

main().catch((e) => { console.error('error: ' + e.message); process.exit(2); });
