// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Ervin Notari Junior

/* Cameras and preferences, saved in the TV localStorage.
 * The URLs include user and password: they stay only in the app's local storage. */
'use strict';

var Store = (function () {
  var KEY = 'rtsp-player-v1';

  // Models with a known RTSP path (name on screen: I18n brand.<id>). {ch} = channel, {stream} = 01/02 etc.
  var BRANDS = [
    { id: 'hikvision',
      main: '/Streaming/Channels/{ch}01', sub: '/Streaming/Channels/{ch}02' },
    { id: 'dahua',
      main: '/cam/realmonitor?channel={ch}&subtype=0', sub: '/cam/realmonitor?channel={ch}&subtype=1' },
    { id: 'custom', main: '', sub: '' }
  ];

  var state = { cameras: [], layout: 4, page: 0 };

  function load() {
    try {
      var saved = JSON.parse(localStorage.getItem(KEY) || 'null');
      if (saved && Array.isArray(saved.cameras)) state = saved;
    } catch (e) { /* storage not available: start empty */ }
    if (![1, 4, 8, 16].includes(state.layout)) state.layout = 4;
  }

  function save() {
    try { localStorage.setItem(KEY, JSON.stringify(state)); } catch (e) { /* no space */ }
  }

  function newId() {
    return 'c' + Date.now().toString(36) + crypto.getRandomValues(new Uint32Array(1))[0].toString(36);
  }

  function brand(id) {
    var custom = BRANDS[BRANDS.length - 1];  // "Other (enter the path)"
    return BRANDS.find(function (b) { return b.id === id; }) || custom;
  }

  function path(p) {
    p = String(p || '').trim();
    if (!p) return '';
    return p.charAt(0) === '/' ? p : '/' + p;
  }

  // Builds the URLs from the manual setup fields.
  function urlsFromForm(f) {
    var b = brand(f.brand);
    var auth = f.user ? encodeURIComponent(f.user) + ':' + encodeURIComponent(f.pass || '') + '@' : '';
    var base = 'rtsp://' + auth + f.host.trim() + ':' + (Number.parseInt(f.port, 10) || 554);
    var ch = Number.parseInt(f.channel, 10) || 1;
    var main = b.id === 'custom' ? path(f.mainPath) : b.main.replace('{ch}', ch);
    var sub = b.id === 'custom' ? path(f.subPath) : b.sub.replace('{ch}', ch);
    return { main: base + main, sub: sub ? base + sub : base + main };
  }

  function sanitize(url) {
    return String(url).replace(/(:\/\/[^:/@\s]*:)[^@/\s]*@/, '$1***@');
  }

  load();

  return {
    BRANDS: BRANDS,
    brand: brand,
    urlsFromForm: urlsFromForm,
    sanitize: sanitize,

    cameras: function () { return state.cameras; },
    camera: function (id) {
      return state.cameras.find(function (c) { return c.id === id; }) || null;
    },
    // cam: {name, main, sub, form?, source}
    add: function (cam) {
      cam.id = newId();
      state.cameras.push(cam);
      save();
      return cam;
    },
    update: function (id, fields) {
      var cam = this.camera(id);
      if (!cam) return;
      for (var k in fields) cam[k] = fields[k];
      save();
    },
    remove: function (id) {
      state.cameras = state.cameras.filter(function (c) { return c.id !== id; });
      save();
    },
    move: function (id, delta) {
      var i = state.cameras.findIndex(function (c) { return c.id === id; });
      var j = i + delta;
      if (i < 0 || j < 0 || j >= state.cameras.length) return;
      var tmp = state.cameras[i];
      state.cameras[i] = state.cameras[j];
      state.cameras[j] = tmp;
      save();
    },
    has: function (url) {
      return state.cameras.some(function (c) { return c.main === url; });
    },

    layout: function () { return state.layout; },
    setLayout: function (n) { state.layout = n; save(); },
    page: function () { return state.page; },
    setPage: function (p) { state.page = p; save(); }
  };
})();
