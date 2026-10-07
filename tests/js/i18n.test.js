// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Ervin Notari Junior

'use strict';

const test = require('node:test');
const assert = require('node:assert');
const fs = require('node:fs');
const path = require('node:path');
const { load } = require('./harness');

const ROOT = path.join(__dirname, '..', '..');
const read = (f) => fs.readFileSync(path.join(ROOT, f), 'utf8');

test('language: any Portuguese becomes pt; the rest (or nothing) becomes en', () => {
  const { I18n } = load([]);
  for (const tag of ['pt', 'pt-BR', 'pt_PT', 'PT-br']) assert.strictEqual(I18n.resolve(tag), 'pt', tag);
  for (const tag of ['en-US', 'es-ES', 'fr', 'ko-KR', '', undefined, null]) assert.strictEqual(I18n.resolve(tag), 'en', String(tag));
  assert.strictEqual(I18n.detect({ languages: ['pt-BR', 'en-US'], language: 'en-US' }), 'pt');
  assert.strictEqual(I18n.detect({ language: 'pt-BR' }), 'pt');
  assert.strictEqual(I18n.detect({}), 'en');
});

test('starts in the TV browser language', () => {
  assert.strictEqual(load([], { language: 'pt-BR' }).I18n.language(), 'pt');
  assert.strictEqual(load([], { language: 'it-IT' }).I18n.language(), 'en');
});

test('system language (tizen LOCALE) wins and notifies the screen', () => {
  const ctx = load([], { language: 'en-US' });
  const changes = [];
  ctx.I18n.onChange = (l) => changes.push(l);
  ctx.tizen = { systeminfo: { getPropertyValue: (prop, ok) => { assert.strictEqual(prop, 'LOCALE'); ok({ language: 'pt_BR' }); } } };
  ctx.I18n.refresh();
  assert.strictEqual(ctx.I18n.language(), 'pt');
  assert.deepStrictEqual(changes, ['pt']);
  ctx.I18n.refresh();  // same language: no redraw
  assert.deepStrictEqual(changes, ['pt']);
  // LOCALE not available: stays with the browser.
  ctx.tizen.systeminfo.getPropertyValue = (prop, ok, fail) => fail(new Error('x'));
  ctx.I18n.refresh();
  assert.strictEqual(ctx.I18n.language(), 'en');
});

test('texts: parameters, plural, errors and English fallback', () => {
  const { I18n } = load([], { language: 'pt-BR' });
  assert.strictEqual(I18n.t('form.saved', { name: 'Garagem' }), '"Garagem" salva');
  assert.strictEqual(I18n.plural('channels.added', 1), '1 câmera adicionada');
  assert.strictEqual(I18n.plural('channels.added', 3), '3 câmeras adicionadas');
  assert.strictEqual(I18n.plural('channels.title', 4, { name: 'DVR' }), 'DVR: 4 canais');
  assert.strictEqual(I18n.error('dns_failed', 'cam.local'), 'Não foi possível encontrar o endereço cam.local');
  assert.strictEqual(I18n.error('nope', 'detalhe'), 'detalhe');
  assert.strictEqual(I18n.t('key.that.does.not.exist'), 'key.that.does.not.exist');
  // Key only in English: uses English.
  I18n.STRINGS.en['only.en'] = 'English';
  assert.strictEqual(I18n.t('only.en'), 'English');
});

// Each language is a JSON file in app/i18n/ with the same keys as en.json.
const I18N_DIR = path.join(ROOT, 'app', 'i18n');
const dictionaries = () => Object.fromEntries(fs.readdirSync(I18N_DIR).filter((f) => f.endsWith('.json'))
  .map((f) => [f.slice(0, -5), JSON.parse(fs.readFileSync(path.join(I18N_DIR, f), 'utf8'))]));

test('every language JSON is in LANGUAGES, and vice versa', () => {
  const { I18n } = load([]);
  assert.deepStrictEqual(Object.keys(dictionaries()).sort(), Object.keys(I18n.LANGUAGES).sort());
});

test('every language has the same keys and parameters as en.json', () => {
  const all = dictionaries();
  const en = all.en;
  const params = (s) => (s.match(/\{\w+\}/g) || []).sort().join(',');
  for (const [code, dict] of Object.entries(all)) {
    assert.deepStrictEqual(Object.keys(dict).sort(), Object.keys(en).sort(), code);
    for (const k of Object.keys(en)) {
      assert.strictEqual(typeof dict[k], 'string', code + ' ' + k);
      assert.strictEqual(params(dict[k]), params(en[k]), code + ' ' + k);
    }
  }
});

test('loads only the languages in use: en (fallback) and the TV one', () => {
  assert.deepStrictEqual(Object.keys(load([], { language: 'en-US' }).I18n.STRINGS), ['en']);
  assert.deepStrictEqual(Object.keys(load([], { language: 'pt-BR' }).I18n.STRINGS).sort(), ['en', 'pt']);
});

// Every key used in the HTML, the JS and by the WASM exists in the dictionary.
test('every key used in the app exists', () => {
  const { I18n, Store } = load(['store.js']);
  const used = new Set();
  const html = read('app/index.html');
  for (const m of html.matchAll(/data-i18n(?:-[\w-]+)?="([^"]+)"/g)) used.add(m[1]);
  const js = fs.readdirSync(path.join(ROOT, 'app/js')).filter((f) => f !== 'i18n.js')
    .map((f) => read('app/js/' + f)).join('\n');
  // Built key ('brand.' + id) is left out; the brands are added below.
  for (const m of js.matchAll(/I18n\.t\('([^']+)'(?! \+)/g)) used.add(m[1]);
  for (const m of js.matchAll(/I18n\.t\(\w+ \? '([^']+)' : '([^']+)'\)/g)) { used.add(m[1]); used.add(m[2]); }
  for (const m of js.matchAll(/I18n\.plural\('([^']+)'/g)) { used.add(m[1] + '.one'); used.add(m[1] + '.other'); }
  for (const b of Store.BRANDS) used.add('brand.' + b.id);
  used.add('err.player_error');

  // Error codes emitted by the C++.
  const cpp = ['wasm/src/rtsp_connection.cpp', 'wasm/src/player_main.cpp'].map(read).join('\n');
  const codes = new Set();
  for (const m of cpp.matchAll(/\*err = \{"(\w+)"/g)) codes.add(m[1]);
  for (const m of cpp.matchAll(/(?:LogError\(slot, "\w+"|Fatal\(slot), "(\w+)"/g)) codes.add(m[1]);
  assert.ok(codes.size >= 15, 'found the C++ codes: ' + [...codes]);
  for (const c of codes) used.add('err.' + c);

  assert.ok(used.size > 80, 'found the keys: ' + used.size);
  const missing = [...used].filter((k) => !(k in dictionaries().en));
  assert.deepStrictEqual(missing, []);
});

// The HTML carries the English text (fallback and accessibility); every
// text must come from en.json through data-i18n, so translations cover it.
test('HTML texts are the en.json defaults of their data-i18n keys', () => {
  const en = dictionaries().en;
  const unescape = (s) => s.split('&quot;').join('"').split('&lt;').join('<')
    .split('&gt;').join('>').split('&amp;').join('&');
  // Removes comments until none is left (a removal could join a new "<!--").
  let html = read('app/index.html');
  for (let prev = ''; prev !== html;) {
    prev = html;
    html = html.replace(/<!--[\s\S]*?-->/g, '');
  }
  const keyed = [...html.matchAll(/<(\w+)\b[^>]*\bdata-i18n="([^"]+)"[^>]*>([^<]*)<\/\1>/g)];
  assert.ok(keyed.length > 40, 'found the texts: ' + keyed.length);
  for (const [, , key, text] of keyed) assert.strictEqual(unescape(text), en[key], key);
  for (const [, text, key] of html.matchAll(/placeholder="([^"]*)" data-i18n-placeholder="([^"]+)"/g)) {
    assert.strictEqual(unescape(text), en[key], key);
  }
  const keyedTexts = new Set(keyed.map((m) => m[3].trim()));
  const texts = [...html.matchAll(/>([^<>]+)</g)].map((m) => m[1].trim()).filter(Boolean);
  const fixed = texts.filter((s) => /[A-Za-zÀ-ú]{2,}/.test(s) && s !== 'Mosaico' && !keyedTexts.has(s));
  assert.deepStrictEqual(fixed, []);
});

test('a language without its file falls back to English', () => {
  const { I18n } = load([], { language: 'en-US' });
  I18n.LANGUAGES.zz = 'zz';  // listed, but there is no i18n/zz.json
  assert.strictEqual(I18n.resolve('zz-ZZ'), 'en');
  assert.strictEqual(I18n.setLanguage('zz'), false);
  assert.strictEqual(I18n.language(), 'en');
});

test('apply() fills texts and attributes of the marked elements', () => {
  const { I18n } = load([], { language: 'pt-BR' });
  const el = (data) => ({ dataset: data, attrs: {}, setAttribute(k, v) { this.attrs[k] = v; },
    getAttribute(k) { return k.startsWith('data-i18n-') ? data.attr : null; } });
  const text = el({ i18n: 'exit.confirm' });
  const placeholder = el({ attr: 'form.name_ph' });
  const root = {
    querySelectorAll(sel) {
      if (sel === '[data-i18n]') return [text];
      if (sel === '[data-i18n-placeholder]') return [placeholder];
      return [];
    }
  };
  I18n.apply(root);
  assert.strictEqual(text.textContent, 'Sair');
  assert.strictEqual(placeholder.attrs.placeholder, 'Ex.: Garagem');
});

test('refresh without tizen, or when reading LOCALE throws, uses the browser language', () => {
  const ctx = load([], { language: 'pt-BR' });
  ctx.I18n.setLanguage('en');
  ctx.I18n.refresh();  // no tizen object
  assert.strictEqual(ctx.I18n.language(), 'pt');
  ctx.I18n.setLanguage('en');
  ctx.tizen = { systeminfo: { getPropertyValue: () => { throw new Error('denied'); } } };
  ctx.I18n.refresh();
  assert.strictEqual(ctx.I18n.language(), 'pt');
  // LOCALE without a language field.
  ctx.I18n.setLanguage('en');
  ctx.tizen.systeminfo.getPropertyValue = (prop, ok) => ok({});
  ctx.I18n.refresh();
  assert.strictEqual(ctx.I18n.language(), 'pt');
});

test('with a document, apply() also sets <html lang> and a language change reapplies', () => {
  const ctx = load([], { language: 'en-US' });
  const html = { lang: '' };
  const label = { dataset: { i18n: 'common.back' }, textContent: 'Back' };
  ctx.document = {
    documentElement: html,
    querySelectorAll: (sel) => (sel === '[data-i18n]' ? [label] : [])
  };
  ctx.I18n.apply();
  assert.strictEqual(html.lang, 'en');
  ctx.I18n.setLanguage('pt-PT');
  assert.strictEqual(html.lang, 'pt-BR');
  assert.strictEqual(label.textContent, 'Voltar');
});

test('an invalid language file falls back to English', () => {
  const ctx = load([], { language: 'en-US' });
  ctx.I18n.LANGUAGES.bad = 'bad';
  // The fake XHR serves files from app/i18n; this one throws while parsing.
  const XHR = ctx.XMLHttpRequest;
  ctx.XMLHttpRequest = class extends XHR {
    send() { this.status = 200; this.responseText = '{not json'; }
  };
  assert.strictEqual(ctx.I18n.resolve('bad'), 'en');
});
