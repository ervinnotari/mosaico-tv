// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Ervin Notari Junior

/* UI texts. Each language is a JSON file in app/i18n/ (en.json, pt.json)
 * with the same keys; LANGUAGES lists them. The language comes from the TV
 * (menu language): any Portuguese variant uses "pt"; everything else uses
 * "en", which is also the fallback for a key missing in a language.
 *
 * In HTML: data-i18n="key" replaces the text; data-i18n-title,
 * data-i18n-aria-label and data-i18n-placeholder replace those attributes.
 * In JS: I18n.t('key', {name: value}) with {name} in the text and
 * I18n.plural('key', n, ...) with the variants key.one / key.other.
 * WASM module errors arrive as a code and become err.<code>. */
'use strict';

var I18n = (function () {
  var FALLBACK = 'en';

  // Available languages: file app/i18n/<code>.json -> value of <html lang>.
  var LANGUAGES = { en: 'en', pt: 'pt-BR' };

  var STRINGS = {};  // dictionaries already loaded, by language

  // Reads app/i18n/<code>.json. Synchronous: the files are inside the package
  // and every screen needs the texts before it is drawn.
  function load(code) {
    if (STRINGS[code]) return true;
    try {
      var xhr = new XMLHttpRequest();
      xhr.open('GET', 'i18n/' + code + '.json', false);
      xhr.send();
      // Packaged files answer with status 0 (file://).
      if (xhr.status !== 200 && xhr.status !== 0) return false;
      STRINGS[code] = JSON.parse(xhr.responseText);
      return true;
    } catch (e) {
      return false;  // missing or invalid file: the caller falls back to English
    }
  }

  // "pt", "pt-BR", "pt_PT"... → pt; any other language (or none) → en.
  function resolve(tag) {
    var base = String(tag || '').toLowerCase().replace('_', '-').split('-')[0];
    return LANGUAGES[base] && load(base) ? base : FALLBACK;
  }

  // The first browser language is the TV menu language.
  function detect(nav) {
    nav = nav || (typeof navigator !== 'undefined' ? navigator : {});
    return resolve((nav.languages && nav.languages[0]) || nav.language);
  }

  load(FALLBACK);
  var lang = detect();

  function format(text, params) {
    return text.replace(/\{(\w+)\}/g, function (m, name) {
      return params && params[name] !== undefined ? String(params[name]) : m;
    });
  }

  function dict(code) { return STRINGS[code] || {}; }

  function has(key) {
    return dict(lang)[key] !== undefined || dict(FALLBACK)[key] !== undefined;
  }

  function t(key, params) {
    var text = dict(lang)[key];
    if (text === undefined) text = dict(FALLBACK)[key];
    if (text === undefined) return key;
    return format(text, params);
  }

  function plural(key, n, params) {
    var p = { n: n };
    for (var k in params) p[k] = params[k];
    return t(key + (n === 1 ? '.one' : '.other'), p);
  }

  // Message for a WASM error: {code, text}. Without a known code, uses the
  // technical text as it came.
  function error(code, detail) {
    if (code && has('err.' + code)) return t('err.' + code, { detail: detail || '' });
    return detail || '';
  }

  var ATTRS = ['title', 'aria-label', 'placeholder'];

  // Applies the texts to the HTML (data-i18n*).
  function apply(root) {
    var doc = typeof document === 'undefined' ? null : document;
    root = root || doc;
    if (root === doc && doc.documentElement) {
      doc.documentElement.lang = LANGUAGES[lang];
    }
    Array.prototype.forEach.call(root.querySelectorAll('[data-i18n]'), function (e) {
      e.textContent = t(e.dataset.i18n);
    });
    ATTRS.forEach(function (attr) {
      Array.prototype.forEach.call(root.querySelectorAll('[data-i18n-' + attr + ']'), function (e) {
        e.setAttribute(attr, t(e.getAttribute('data-i18n-' + attr)));
      });
    });
  }

  var I18n = {
    STRINGS: STRINGS,
    LANGUAGES: LANGUAGES,
    FALLBACK: FALLBACK,
    resolve: resolve,
    detect: detect,
    t: t,
    plural: plural,
    has: has,
    error: error,
    apply: apply,
    onChange: null,

    language: function () { return lang; },

    // Changes the language ("pt", "en-US"...); returns true if it changed.
    setLanguage: function (tag) {
      var next = resolve(tag);
      if (next === lang) return false;
      lang = next;
      if (typeof document !== 'undefined') apply();
      if (I18n.onChange) I18n.onChange(lang);
      return true;
    },

    // Confirms with the system language (tizen.systeminfo LOCALE), which wins
    // even if the TV browser reports another one.
    refresh: function () {
      var tz = typeof tizen !== 'undefined' ? tizen : null;
      if (!tz || !tz.systeminfo) {
        I18n.setLanguage(detect());
        return;
      }
      try {
        tz.systeminfo.getPropertyValue('LOCALE', function (v) {
          I18n.setLanguage(v && v.language ? v.language : detect());
        }, function () { I18n.setLanguage(detect()); });
      } catch (e) {
        I18n.setLanguage(detect());
      }
    }
  };

  return I18n;
})();
