// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Ervin Notari Junior

/* App screens: mosaic (1, 4, 8, 16), camera list, manual setup and ONVIF
 * search. */
'use strict';

(function () {
  var W = 1920, H = 1080, GAP = 2;

  function $(id) { return document.getElementById(id); }

  // ------------------------------------------------------------ utilities

  var toastTimer = null;
  function toast(text) {
    var el = $('toast');
    el.textContent = text;
    el.classList.add('show');
    clearTimeout(toastTimer);
    toastTimer = setTimeout(function () { el.classList.remove('show'); }, 3500);
  }

  function el(tag, cls, text) {
    var e = document.createElement(tag);
    if (cls) e.className = cls;
    if (text !== undefined) e.textContent = text;
    return e;
  }

  var screens = ['mosaic', 'cameras', 'form', 'search', 'about'];
  var screen = null;
  function show(name, initialFocus) {
    screens.forEach(function (s) { $('screen-' + s).classList.toggle('active', s === name); });
    screen = name;
    document.body.classList.toggle('on-mosaic', name === 'mosaic');
    if (name !== 'mosaic') Player.stopAll();
    Nav.setRoot($('screen-' + name), initialFocus);
  }

  // TV subnet for the unicast ONVIF search ("192.168.1.").
  var subnetPrefix = '';
  function detectSubnet() {
    if (!window.tizen || !tizen.systeminfo) return;
    ['ETHERNET_NETWORK', 'WIFI_NETWORK'].forEach(function (prop) {
      try {
        tizen.systeminfo.getPropertyValue(prop, function (v) {
          if (v && v.ipAddress && /^\d+\.\d+\.\d+\.\d+$/.test(v.ipAddress) && !subnetPrefix) {
            subnetPrefix = v.ipAddress.split('.').slice(0, 3).join('.') + '.';
          }
        }, function () {});
      } catch (e) { /* property not available */ }
    });
  }

  // --------------------------------------------------------------- mosaic

  var zoomed = -1;  // index of the camera opened in full screen from the mosaic

  function inset(r) {
    return { x: r.x + GAP, y: r.y + GAP, w: r.w - 2 * GAP, h: r.h - 2 * GAP };
  }

  function layoutRects(n) {
    var rects = [];
    if (n === 1) return [{ x: 0, y: 0, w: W, h: H }];
    if (n === 8) {
      var cw = W / 4, ch = H / 4;
      rects.push(inset({ x: 0, y: 0, w: cw * 3, h: ch * 3 }));
      [[3, 0], [3, 1], [3, 2], [0, 3], [1, 3], [2, 3], [3, 3]].forEach(function (p) {
        rects.push(inset({ x: p[0] * cw, y: p[1] * ch, w: cw, h: ch }));
      });
      return rects;
    }
    var cols = n === 4 ? 2 : 4;
    var w = W / cols, h = H / cols;
    for (var i = 0; i < n; i++) {
      rects.push(inset({ x: (i % cols) * w, y: Math.floor(i / cols) * h, w: w, h: h }));
    }
    return rects;
  }

  function pageCount() {
    var n = Store.cameras().length;
    var size = Store.layout();
    return Math.max(1, Math.ceil(n / size));
  }

  // Cameras visible now: [{cam, rect, native}]
  function visibleTiles() {
    var cams = Store.cameras();
    if (zoomed >= 0 && cams[zoomed]) {
      return [{ cam: cams[zoomed], index: zoomed, rect: { x: 0, y: 0, w: W, h: H }, native: true }];
    }
    var size = Store.layout();
    var page = Math.min(Store.page(), pageCount() - 1);
    var rects = layoutRects(size);
    var out = [];
    for (var i = 0; i < size; i++) {
      var idx = page * size + i;
      // Single view and the large tile of 1:8 use the native player (hardware,
      // main stream); the rest is decoded in software (substream).
      var native = size === 1 || (size === 8 && i === 0);
      out.push({ cam: cams[idx] || null, index: idx, rect: rects[i], native: native });
    }
    return out;
  }

  // ------------------------------------------------------- TV capability

  function layoutCheck(n) {
    return Capability.check(n, Store.cameras());
  }

  function unavailableReason(n) {
    var c = layoutCheck(n);
    return I18n.t('layout.unavailable', { n: n, cost: c.cost.toFixed(0), budget: c.budget.toFixed(0) });
  }

  // If the saved layout no longer fits (new or heavier cameras), fall back to
  // the largest one that fits.
  function ensureLayoutFits() {
    var current = Store.layout();
    if (!Capability.ready() || layoutCheck(current).ok) return;
    var fallback = [16, 8, 4, 1].filter(function (n) { return n < current && layoutCheck(n).ok; })[0];
    var firstVisible = Store.page() * current;
    Store.setLayout(fallback);
    Store.setPage(Math.floor(firstVisible / fallback));
    toast(I18n.t('layout.fallback', { from: current, to: fallback }));
  }

  function refreshLayoutButtons() {
    Array.prototype.forEach.call(document.querySelectorAll('.tb-layout'), function (b) {
      var n = +b.getAttribute('data-layout');
      b.classList.toggle('selected', n === Store.layout() && zoomed < 0);
      b.classList.toggle('unavailable', Capability.ready() && !layoutCheck(n).ok);
    });
  }

  function renderMosaic(clearCanvas) {
    ensureLayoutFits();
    resetLoadGuard();
    var tiles = visibleTiles();
    var box = $('tiles');
    var focusedIndex = Nav.current() && Nav.current().getAttribute('data-index');
    box.innerHTML = '';
    var wants = [];
    var video = $('video');
    video.style.display = 'none';

    tiles.forEach(function (t, slot) {
      var d = el('div', 'tile focusable' + (t.cam ? '' : ' empty-tile'));
      d.style.left = t.rect.x + 'px';
      d.style.top = t.rect.y + 'px';
      d.style.width = t.rect.w + 'px';
      d.style.height = t.rect.h + 'px';
      d.setAttribute('data-slot', slot);
      d.setAttribute('data-index', t.index);
      if (t.cam) {
        d.appendChild(el('div', 'tile-name', t.cam.name));
        d.appendChild(el('div', 'tile-status', ''));
        wants[slot] = {
          // No position: changing the layout only moves the tile, no reconnect.
          key: t.cam.id + (t.native ? ':main' : ':sub'),
          url: t.native ? t.cam.main : (t.cam.sub || t.cam.main),
          mode: t.native ? 0 : 1,
          rect: t.rect
        };
        if (t.native) {
          video.style.display = 'block';
          video.style.left = t.rect.x + 'px';
          video.style.top = t.rect.y + 'px';
          video.style.width = t.rect.w + 'px';
          video.style.height = t.rect.h + 'px';
        }
      } else {
        d.appendChild(el('div', 'tile-empty', '+'));
      }
      d.addEventListener('click', function () { onTileOk(d); });
      box.appendChild(d);
    });

    var hasCams = Store.cameras().length > 0;
    $('empty').style.display = hasCams ? 'none' : 'flex';
    box.style.display = hasCams ? 'block' : 'none';
    $('tb-page').textContent = (Math.min(Store.page(), pageCount() - 1) + 1) + '/' + pageCount();
    refreshLayoutButtons();
    document.body.classList.toggle('zoomed', zoomed >= 0);

    // Play only once we know what the TV can handle.
    Player.show(hasCams && Capability.ready() ? wants : [], clearCanvas);

    if (screen === 'mosaic' && !exitDialogOpen()) {
      var again = focusedIndex && box.querySelector('[data-index="' + focusedIndex + '"]');
      Nav.setRoot($('screen-mosaic'), hasCams ? (again || box.firstChild) : $('empty-search'));
    }
  }

  function setLayout(n) {
    if (!layoutCheck(n).ok) {
      toast(unavailableReason(n));
      return;
    }
    zoomed = -1;
    var firstVisible = Store.page() * Store.layout();
    Store.setLayout(n);
    Store.setPage(Math.floor(firstVisible / n));
    renderMosaic(true);
  }

  function changePage(delta) {
    var pages = pageCount();
    if (zoomed >= 0) {
      var n = Store.cameras().length;
      zoomed = (zoomed + delta + n) % n;
    } else {
      Store.setPage((Math.min(Store.page(), pages - 1) + delta + pages) % pages);
    }
    renderMosaic(true);
  }

  function onTileOk(tile) {
    var index = +tile.getAttribute('data-index');
    var cam = Store.cameras()[index];
    if (!cam) { openForm(null); return; }
    if (zoomed >= 0 || Store.layout() === 1) return;
    zoomed = index;
    renderMosaic(true);
  }

  Player.onStatus = function (slot, status, text) {
    var tile = document.querySelector('.tile[data-slot="' + slot + '"]');
    if (!tile) return;
    tile.setAttribute('data-status', status);
    var st = tile.querySelector('.tile-status');
    if (st) st.textContent = status === 'live' ? '' : text;
  };

  $('tb-prev').onclick = function () { changePage(-1); };
  $('tb-next').onclick = function () { changePage(1); };
  $('tb-cameras').onclick = function () { openCameras(); };
  $('empty-search').onclick = function () { openSearch(); };
  $('empty-manual').onclick = function () { openForm(null); };
  Array.prototype.forEach.call(document.querySelectorAll('.tb-layout'), function (b) {
    b.onclick = function () { setLayout(+b.getAttribute('data-layout')); };
  });

  // The toolbar shows up when the focus enters it.
  Nav.onFocus = function (focused) {
    var inToolbar = !!focused.closest('#toolbar');
    $('toolbar').classList.toggle('show', inToolbar || Store.cameras().length === 0);
    $('tb-info').textContent = focused.classList.contains('unavailable')
      ? unavailableReason(+focused.getAttribute('data-layout'))
      : focused.id === 'tb-cameras' ? I18n.t('toolbar.settings_info') : '';
  };

  // Keeps the real substream resolution of each camera (the layout cost
  // then uses the measured value) and watches the decoder load.
  var lastStats = {};
  var overloadedTicks = 0;
  var overloadWarned = false;

  function resetLoadGuard() {
    lastStats = {};
    overloadedTicks = 0;
    overloadWarned = false;
  }

  Player.onStats = function (slot, s) {
    if (s.mode !== 'software' || !(s.width > 0)) return;
    var tile = document.querySelector('.tile[data-slot="' + slot + '"]');
    var cam = tile && Store.cameras()[+tile.getAttribute('data-index')];
    if (!cam) return;

    var fps = Math.round(Math.min(30, s.fps));
    var info = cam.subInfo;
    if (fps >= 5 && (!info || info.w !== s.width || info.h !== s.height || Math.abs(info.fps - fps) > 3)) {
      Store.update(cam.id, { subInfo: { w: s.width, h: s.height, fps: fps } });
      refreshLayoutButtons();
    }

    lastStats[slot] = s;
    var all = Object.keys(lastStats).map(function (k) { return lastStats[k]; });
    // Evaluate once per stats cycle (when the first software slot reports).
    if (slot !== +Object.keys(lastStats)[0]) return;
    var load = Capability.load(all);
    overloadedTicks = load > 0.9 ? overloadedTicks + 1 : 0;
    if (overloadedTicks >= 3 && !overloadWarned) {
      overloadWarned = true;
      toast(I18n.t('mosaic.overload'));
    }
  };

  // ----------------------------------------------------------------- list

  var confirmRemove = null;

  function openCameras(focusId) {
    var list = $('cam-list');
    list.innerHTML = '';
    var cams = Store.cameras();
    if (!cams.length) list.appendChild(el('p', 'note', I18n.t('cameras.none')));
    var initial = null;
    cams.forEach(function (c, i) {
      var row = el('div', 'item');
      var info = el('div', 'info');
      info.appendChild(el('div', 'title', (i + 1) + '. ' + c.name));
      info.appendChild(el('div', 'sub', Store.sanitize(c.main)));
      row.appendChild(info);
      var actions = el('div', 'actions');
      function btn(label, fn) {
        var b = el('button', 'focusable', label);
        b.onclick = fn;
        actions.appendChild(b);
        return b;
      }
      var edit = btn(I18n.t('cameras.edit'), function () { openForm(c.id); });
      btn('▲', function () { Store.move(c.id, -1); openCameras(c.id); });
      btn('▼', function () { Store.move(c.id, 1); openCameras(c.id); });
      var rm = btn(I18n.t('cameras.remove'), function () {
        if (confirmRemove !== c.id) {
          confirmRemove = c.id;
          rm.textContent = I18n.t('cameras.confirm');
          rm.classList.add('danger');
          return;
        }
        confirmRemove = null;
        Store.remove(c.id);
        toast(I18n.t('cameras.removed', { name: c.name }));
        openCameras();
      });
      if (c.id === focusId) initial = edit;
      row.appendChild(actions);
      list.appendChild(row);
    });
    confirmRemove = null;
    refreshPerformance();
    show('cameras', initial || $('cam-search'));
  }

  function refreshPerformance() {
    var p = Capability.profile();
    var text;
    if (!p) {
      text = I18n.t('perf.measuring');
    } else {
      var layouts = [1, 4, 8, 16].filter(function (n) { return layoutCheck(n).ok; });
      text = I18n.t('perf.summary', { mpx: p.capacityMpx.toFixed(0), cores: p.cores }) +
        (p.fallback ? I18n.t('perf.estimated') : '') +
        I18n.t('perf.layouts', { list: layouts.join(', ') });
    }
    $('cam-perf').textContent = text;
  }

  $('cam-remeasure').onclick = function () {
    $('cam-perf').textContent = I18n.t('perf.measuring');
    Capability.remeasure(function () { refreshPerformance(); });
  };

  $('cam-search').onclick = function () { openSearch(); };
  $('cam-manual').onclick = function () { openForm(null); };
  $('cam-back').onclick = function () { backToMosaic(); };

  // ---------------------------------------------------------------- about

  // Documents copied to app/licenses/ by scripts/package.bat. The .md files
  // are rendered; the license texts stay as they are (preformatted).
  var ABOUT_DOCS = {
    credits: [{ file: 'licenses/CREDITS.md', md: true }],
    third: [{ file: 'licenses/THIRD_PARTY_NOTICES.md', md: true }],
    license: [{ file: 'licenses/NOTICE' }, { file: 'licenses/LICENSE' }]
  };
  var aboutCache = {};

  function fetchText(file, done) {
    var xhr = new XMLHttpRequest();
    xhr.open('GET', file);
    xhr.onload = xhr.onerror = function () {
      done(xhr.status === 200 || xhr.status === 0 ? xhr.responseText : '');
    };
    xhr.send();
  }

  function showAboutDoc(name) {
    Array.prototype.forEach.call(document.querySelectorAll('.about-tab'), function (b) {
      b.classList.toggle('selected', b.getAttribute('data-doc') === name);
    });
    var box = $('about-doc');
    box.scrollTop = 0;
    if (aboutCache[name]) { box.innerHTML = aboutCache[name]; return; }
    var parts = ABOUT_DOCS[name];
    var html = [];
    var pending = parts.length;
    box.innerHTML = '<p class="note">' + Markdown.escape(I18n.t('about.loading')) + '</p>';
    parts.forEach(function (part, i) {
      fetchText(part.file, function (text) {
        // Markdown.render and Markdown.escape escape all the text.
        html[i] = !text ? '' : part.md ? Markdown.render(text)
          : '<pre class="md-code">' + Markdown.escape(text) + '</pre>';
        if (--pending === 0) {
          aboutCache[name] = html.join('<hr>') || '<p>' + Markdown.escape(I18n.t('about.not_found')) + '</p>';
          box.innerHTML = aboutCache[name];
        }
      });
    });
  }

  function openAbout() {
    var version = '';
    try { version = tizen.application.getAppInfo().version; } catch (e) { /* outside the TV */ }
    $('about-version').textContent = version ? '· Mosaico ' + version : '· Mosaico';
    showAboutDoc('credits');
    show('about', document.querySelector('.about-tab[data-doc="credits"]'));
  }

  Array.prototype.forEach.call(document.querySelectorAll('.about-tab'), function (b) {
    b.onclick = function () { showAboutDoc(b.getAttribute('data-doc')); };
  });
  $('cam-about').onclick = openAbout;
  $('about-back').onclick = function () { openCameras(); };

  function backToMosaic() {
    show('mosaic');
    renderMosaic(true);
  }

  // ----------------------------------------------------------------- form

  var editingId = null;
  var brandIndex = 0;

  function formValues() {
    return {
      brand: Store.BRANDS[brandIndex].id,
      host: $('f-host').value.trim(),
      port: $('f-port').value.trim(),
      channel: $('f-channel').value.trim(),
      user: $('f-user').value.trim(),
      pass: $('f-pass').value,
      mainPath: $('f-main').value.trim(),
      subPath: $('f-sub').value.trim()
    };
  }

  function refreshForm() {
    var b = Store.BRANDS[brandIndex];
    $('f-brand').querySelector('.value').textContent = I18n.t('brand.' + b.id);
    var custom = b.id === 'custom';
    Array.prototype.forEach.call(document.querySelectorAll('.custom-only'), function (e) {
      e.classList.toggle('hidden', !custom);
    });
    Array.prototype.forEach.call(document.querySelectorAll('.brand-only'), function (e) {
      e.classList.toggle('hidden', custom);
    });
    var v = formValues();
    $('f-preview').textContent = v.host ? Store.sanitize(Store.urlsFromForm(v).main) : '';
  }

  function openForm(id) {
    editingId = id;
    var cam = id ? Store.camera(id) : null;
    var f = (cam && cam.form) || {};
    $('form-title').textContent = I18n.t(cam ? 'form.edit' : 'form.new');
    $('f-name').value = cam ? cam.name : '';
    brandIndex = Math.max(0, Store.BRANDS.findIndex(function (b) { return b.id === (f.brand || 'hikvision'); }));
    $('f-host').value = f.host || '';
    $('f-port').value = f.port || '554';
    $('f-channel').value = f.channel || '1';
    $('f-user').value = f.user !== undefined ? f.user : 'admin';
    $('f-pass').value = f.pass || '';
    $('f-main').value = f.mainPath || '';
    $('f-sub').value = f.subPath || '';
    // Camera from ONVIF: edit the URLs directly.
    if (cam && !cam.form) {
      brandIndex = Store.BRANDS.length - 1;
      var m = cam.main.match(/^rtsp:\/\/(?:([^:@\/]*)(?::([^@\/]*))?@)?([^:\/]+)(?::(\d+))?(\/.*)?$/);
      if (m) {
        $('f-user').value = decodeURIComponent(m[1] || '');
        $('f-pass').value = decodeURIComponent(m[2] || '');
        $('f-host').value = m[3];
        $('f-port').value = m[4] || '554';
        $('f-main').value = m[5] || '/';
        var s = cam.sub && cam.sub.match(/^rtsp:\/\/[^\/]+(\/.*)?$/);
        $('f-sub').value = s && cam.sub !== cam.main ? (s[1] || '') : '';
      }
    }
    $('f-error').textContent = '';
    refreshForm();
    show('form', document.querySelector('[data-input="f-name"]'));
  }

  Nav.onEdited = function () {
    if (screen === 'form') refreshForm();
  };

  $('f-brand').onclick = function () {
    brandIndex = (brandIndex + 1) % Store.BRANDS.length;
    refreshForm();
  };

  $('f-save').onclick = function () {
    var v = formValues();
    var err = '';
    // IP or name (e.g. DDNS); the TV resolves the name when connecting.
    if (!/^[A-Za-z0-9]([A-Za-z0-9.-]*[A-Za-z0-9])?$/.test(v.host)) {
      err = I18n.t('form.err_host');
    }
    else if (Store.BRANDS[brandIndex].id === 'custom' && !v.mainPath) err = I18n.t('form.err_main');
    if (err) { $('f-error').textContent = err; return; }
    var urls = Store.urlsFromForm(v);
    var name = $('f-name').value.trim() || (I18n.t('form.default_name', { host: v.host }) + (Store.BRANDS[brandIndex].id !== 'custom' ? ' · ' + (v.channel || 1) : ''));
    var fields = { name: name, main: urls.main, sub: urls.sub, form: v, source: 'manual' };
    if (editingId) Store.update(editingId, fields);
    else Store.add(fields);
    toast(I18n.t('form.saved', { name: name }));
    if (editingId) openCameras(editingId);
    else backToMosaic();
  };
  $('f-cancel').onclick = function () {
    if (editingId) openCameras(editingId);
    else backToMosaic();
  };

  // --------------------------------------------------------------- search

  var found = [];
  var searching = false;
  var selectedDevice = null;
  var channels = [];

  function openSearch() {
    show('search', $('s-back'));
    startSearch();
  }

  function startSearch() {
    if (searching) return;  // the previous search is still running
    if (!Player.isReady()) {
      $('s-status').textContent = I18n.t('search.loading');
      setTimeout(function () { if (screen === 'search') startSearch(); }, 1000);
      return;
    }
    found = [];
    $('s-list').innerHTML = '';
    $('s-status').textContent = I18n.t('search.searching');
    $('s-status').classList.add('busy');
    searching = Player.discover(4000, subnetPrefix);
    if (!searching) {
      $('s-status').textContent = I18n.t('search.start_failed');
      $('s-status').classList.remove('busy');
    }
  }

  Player.onDiscovery = function (from, xml) {
    var dev;
    try { dev = Onvif.parseProbeMatch(from, xml); } catch (e) { return; }
    if (!dev.xaddr || found.some(function (d) { return d.ip === dev.ip; })) return;
    found.push(dev);
    var b = el('button', 'item focusable');
    var info = el('div', 'info');
    info.appendChild(el('div', 'title', dev.name));
    info.appendChild(el('div', 'sub', [dev.hardware, dev.ip].filter(Boolean).join(' · ')));
    b.appendChild(info);
    var already = Store.cameras().some(function (c) { return c.main.indexOf('@' + dev.ip + ':') > 0 || c.main.indexOf('//' + dev.ip) > 0; });
    if (already) b.appendChild(el('span', 'badge', I18n.t('search.already')));
    b.onclick = function () { openLogin(dev); };
    $('s-list').appendChild(b);
    if (Nav.current() === $('s-back') || !Nav.current()) Nav.focus(b);
  };

  Player.onDiscoveryDone = function () {
    searching = false;
    $('s-status').classList.remove('busy');
    $('s-status').textContent = found.length
      ? I18n.plural('search.found', found.length)
      : I18n.t('search.none');
  };

  function openDialog(id, initial) {
    $(id).classList.add('show');
    Nav.setRoot($(id), initial);
  }
  function closeDialog(id, focusEl) {
    $(id).classList.remove('show');
    Nav.setRoot($('screen-search'), focusEl);
  }

  function openLogin(dev) {
    selectedDevice = dev;
    $('s-login-title').textContent = I18n.t('login.title', { name: dev.name });
    $('s-login-error').textContent = '';
    openDialog('s-login', document.querySelector('[data-input="s-pass"]'));
  }

  $('s-connect').onclick = function () {
    var user = $('s-user').value.trim(), pass = $('s-pass').value;
    $('s-login-error').textContent = I18n.t('login.connecting');
    Onvif.getChannels(selectedDevice, user, pass, function (err, res) {
      if (err) {
        $('s-login-error').textContent = err.auth
          ? I18n.t('login.auth_failed')
          : I18n.t('login.read_failed', { detail: err.message });
        return;
      }
      $('s-login').classList.remove('show');
      openChannels(res);
    });
  };
  $('s-login-cancel').onclick = function () { closeDialog('s-login'); };

  function openChannels(res) {
    channels = res.channels.map(function (c) {
      return { info: c, checked: c.supported && !Store.has(c.main) };
    });
    $('s-channels-title').textContent = I18n.plural('channels.title', channels.length, { name: res.name });
    var list = $('s-channel-list');
    list.innerHTML = '';
    channels.forEach(function (ch) {
      var b = el('button', 'item focusable check');
      function paint() {
        b.classList.toggle('checked', ch.checked);
      }
      b.appendChild(el('span', 'box', ''));
      var info = el('div', 'info');
      info.appendChild(el('div', 'title', ch.info.name));
      var detail = ch.info.width ? ch.info.width + 'x' + ch.info.height + ' ' + ch.info.codec : ch.info.codec;
      if (!ch.info.supported) detail += ' · ' + I18n.t('channels.unsupported');
      else if (Store.has(ch.info.main)) detail += ' · ' + I18n.t('search.already');
      info.appendChild(el('div', 'sub', detail));
      b.appendChild(info);
      b.onclick = function () {
        if (!ch.info.supported) return;
        ch.checked = !ch.checked;
        paint();
      };
      paint();
      list.appendChild(b);
    });
    openDialog('s-channels', $('s-add'));
  }

  $('s-add').onclick = function () {
    var added = 0;
    channels.forEach(function (ch) {
      if (!ch.checked || Store.has(ch.info.main)) return;
      Store.add({ name: ch.info.name, main: ch.info.main, sub: ch.info.sub, source: 'onvif' });
      added++;
    });
    $('s-channels').classList.remove('show');
    toast(added ? I18n.plural('channels.added', added) : I18n.t('channels.none_new'));
    backToMosaic();
  };
  $('s-channels-cancel').onclick = function () { closeDialog('s-channels'); };
  $('s-again').onclick = startSearch;
  $('s-back').onclick = function () {
    if (Store.cameras().length) openCameras();
    else backToMosaic();
  };

  // ----------------------------------------------------------------- keys

  function exitApp() {
    Player.stopAll();
    if (window.tizen) tizen.application.getCurrentApplication().exit();
  }

  // Back on the mosaic asks before exiting; focus starts on Cancel so that
  // an accidental press does not close the app.
  var focusBeforeExit = null;

  function exitDialogOpen() { return $('exit-dialog').classList.contains('show'); }

  function openExitDialog() {
    focusBeforeExit = Nav.current();
    $('exit-dialog').classList.add('show');
    Nav.setRoot($('exit-dialog'), $('exit-cancel'));
  }

  function closeExitDialog() {
    $('exit-dialog').classList.remove('show');
    var back = focusBeforeExit && $('screen-mosaic').contains(focusBeforeExit) ? focusBeforeExit : null;
    Nav.setRoot($('screen-mosaic'), back || document.querySelector('.tile') || $('empty-search'));
  }

  $('exit-confirm').onclick = exitApp;
  $('exit-cancel').onclick = closeExitDialog;

  Nav.onKey = function (k, current) {
    var K = Nav.KEY;
    var back = k === K.BACK || k === K.ESC;

    if (screen === 'mosaic' && exitDialogOpen()) {
      if (back) { closeExitDialog(); return true; }
      // Only arrows and OK inside the box; CH▲▼ and the like are blocked.
      return [K.LEFT, K.RIGHT, K.UP, K.DOWN, K.ENTER].indexOf(k) < 0;
    }

    if (screen === 'mosaic') {
      var onTile = current && current.classList.contains('tile');
      var single = zoomed >= 0 || Store.layout() === 1;
      if (k === K.CH_UP) { changePage(1); return true; }
      if (k === K.CH_DOWN) { changePage(-1); return true; }
      if (single && onTile && (k === K.LEFT || k === K.RIGHT)) {
        changePage(k === K.LEFT ? -1 : 1);
        return true;
      }
      if (back) {
        if (current && current.closest('#toolbar') && Store.cameras().length) {
          Nav.focus(document.querySelector('.tile'));
        } else if (zoomed >= 0) {
          var from = zoomed;
          zoomed = -1;
          renderMosaic(true);
          var t = document.querySelector('.tile[data-index="' + from + '"]');
          if (t) Nav.focus(t);
        } else {
          openExitDialog();
        }
        return true;
      }
      return false;
    }

    if (screen === 'form' && current === $('f-brand') && (k === K.LEFT || k === K.RIGHT)) {
      var n = Store.BRANDS.length;
      brandIndex = (brandIndex + (k === K.LEFT ? n - 1 : 1)) % n;
      refreshForm();
      return true;
    }

    // License text: ▲▼ scroll; at the top, ▲ goes back to the buttons.
    if (screen === 'about' && current === $('about-doc') && (k === K.UP || k === K.DOWN)) {
      var box = $('about-doc');
      if (k === K.UP && box.scrollTop === 0) return false;
      box.scrollTop += k === K.DOWN ? 320 : -320;
      return true;
    }

    if (back) {
      if (screen === 'about') {
        openCameras();
        return true;
      }
      if (screen === 'search') {
        if ($('s-login').classList.contains('show')) closeDialog('s-login');
        else if ($('s-channels').classList.contains('show')) closeDialog('s-channels');
        else $('s-back').click();
      } else if (screen === 'form') {
        $('f-cancel').click();
      } else {
        backToMosaic();
      }
      return true;
    }
    return false;
  };

  // ---------------------------------------------------------------- start

  if (window.tizen && tizen.tvinputdevice) {
    ['ChannelUp', 'ChannelDown'].forEach(function (key) {
      try { tizen.tvinputdevice.registerKey(key); } catch (e) { /* key not available */ }
    });
  }
  // TV language: fixed HTML texts now; the system language confirms it
  // right after (and again when returning to the app) and redraws the screen.
  I18n.apply();
  I18n.onChange = function () {
    if (screen === 'mosaic') renderMosaic(false);
    else if (screen === 'cameras') openCameras();
    else if (screen === 'about') openAbout();
    else if (screen === 'form') {
      $('form-title').textContent = I18n.t(editingId ? 'form.edit' : 'form.new');
      refreshForm();
    }
  };
  I18n.refresh();
  detectSubnet();
  show('mosaic');
  renderMosaic(true);
  setTimeout(function () { $('hint').classList.add('fade'); }, 6000);

  // Off screen (Home, another source, another app): disconnect everything and
  // free the TV player, which is unique. On return, reconnect what was shown.
  document.addEventListener('visibilitychange', function () {
    if (document.hidden) {
      console.log('[app] in background: cameras paused');
      Player.stopAll();
    } else {
      I18n.refresh();  // the TV language may have changed
      if (screen === 'mosaic') {
        console.log('[app] back: reconnecting');
        renderMosaic(true);
      }
    }
  });

  // The performance test needs the WASM module; it runs only the first time
  // (or after a firmware update).
  Player.onReady = function () {
    Capability.ensure(function () {
      $('analyzing').classList.add('show');
    }, function () {
      $('analyzing').classList.remove('show');
      if (screen === 'mosaic') renderMosaic(true);
      else if (screen === 'cameras') refreshPerformance();
    });
  };
})();
