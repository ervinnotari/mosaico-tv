// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Ervin Notari Junior

/* Minimal ONVIF client: finds the channels of a device and the RTSP URLs
 * of each one (main and sub stream). SOAP over XHR with
 * WS-Security UsernameToken (PasswordDigest). */
'use strict';

var Onvif = (function () {
  // ---- SHA-1 (only for PasswordDigest) ----
  // "x | 0" below is 32-bit modular arithmetic (as in the SHA-1 spec), not
  // truncation: Math.trunc would give wrong hashes.
  function sha1(bytes) {
    var h0 = 0x67452301, h1 = 0xefcdab89, h2 = 0x98badcfe, h3 = 0x10325476, h4 = 0xc3d2e1f0;
    var len = bytes.length;
    var withPad = ((len + 9 + 63) >> 6) << 6;
    var msg = new Uint8Array(withPad);
    msg.set(bytes);
    msg[len] = 0x80;
    var bits = len * 8;
    msg[withPad - 4] = (bits >>> 24) & 0xff;
    msg[withPad - 3] = (bits >>> 16) & 0xff;
    msg[withPad - 2] = (bits >>> 8) & 0xff;
    msg[withPad - 1] = bits & 0xff;
    var w = new Int32Array(80);
    for (var off = 0; off < withPad; off += 64) {
      for (var i = 0; i < 16; i++) {
        w[i] = (msg[off + i * 4] << 24) | (msg[off + i * 4 + 1] << 16) |
               (msg[off + i * 4 + 2] << 8) | msg[off + i * 4 + 3];
      }
      for (var j = 16; j < 80; j++) {
        var x = w[j - 3] ^ w[j - 8] ^ w[j - 14] ^ w[j - 16];
        w[j] = (x << 1) | (x >>> 31);
      }
      var a = h0, b = h1, c = h2, d = h3, e = h4;
      for (var r = 0; r < 80; r++) {
        var f, k;
        if (r < 20) { f = (b & c) | (~b & d); k = 0x5a827999; }
        else if (r < 40) { f = b ^ c ^ d; k = 0x6ed9eba1; }
        else if (r < 60) { f = (b & c) | (b & d) | (c & d); k = 0x8f1bbcdc; }
        else { f = b ^ c ^ d; k = 0xca62c1d6; }
        var t = (((a << 5) | (a >>> 27)) + f + e + k + w[r]) | 0; // NOSONAR: 32-bit wrap
        e = d; d = c; c = (b << 30) | (b >>> 2); b = a; a = t;
      }
      h0 = (h0 + a) | 0; h1 = (h1 + b) | 0; h2 = (h2 + c) | 0; // NOSONAR: 32-bit wrap
      h3 = (h3 + d) | 0; h4 = (h4 + e) | 0; // NOSONAR: 32-bit wrap
    }
    var out = new Uint8Array(20);
    [h0, h1, h2, h3, h4].forEach(function (v, j) {
      out[j * 4] = (v >>> 24) & 0xff; out[j * 4 + 1] = (v >>> 16) & 0xff;
      out[j * 4 + 2] = (v >>> 8) & 0xff; out[j * 4 + 3] = v & 0xff;
    });
    return out;
  }

  function utf8(str) {
    return new TextEncoder().encode(str);
  }

  function concat(a, b, c) {
    var out = new Uint8Array(a.length + b.length + c.length);
    out.set(a); out.set(b, a.length); out.set(c, a.length + b.length);
    return out;
  }

  function base64(bytes) {
    var s = '';
    bytes.forEach(function (byte) { s += String.fromCodePoint(byte); });
    return btoa(s);
  }

  function xmlEscape(s) {
    return String(s).replace(/&/g, '&amp;').replace(/</g, '&lt;').replace(/>/g, '&gt;')
      .replace(/"/g, '&quot;');
  }

  // ---- SOAP ----
  function security(user, pass, clockOffsetMs) {
    if (!user) return '';
    // The nonce protects the digest against replay: cryptographic randomness.
    var nonce = crypto.getRandomValues(new Uint8Array(16));
    var created = new Date(Date.now() + clockOffsetMs).toISOString().replace(/\.\d+Z$/, 'Z');
    var digest = base64(sha1(concat(nonce, utf8(created), utf8(pass))));
    return '<s:Header><Security s:mustUnderstand="1" xmlns="http://docs.oasis-open.org/wss/2004/01/oasis-200401-wss-wssecurity-secext-1.0.xsd">' +
      '<UsernameToken><Username>' + xmlEscape(user) + '</Username>' +
      '<Password Type="http://docs.oasis-open.org/wss/2004/01/oasis-200401-wss-username-token-profile-1.0#PasswordDigest">' + digest + '</Password>' +
      '<Nonce EncodingType="http://docs.oasis-open.org/wss/2004/01/oasis-200401-wss-soap-message-security-1.0#Base64Binary">' + base64(nonce) + '</Nonce>' +
      '<Created xmlns="http://docs.oasis-open.org/wss/2004/01/oasis-200401-wss-wssecurity-utility-1.0.xsd">' + created + '</Created>' +
      '</UsernameToken></Security></s:Header>';
  }

  function soap(url, body, auth, callback) {
    var xml = '<?xml version="1.0" encoding="UTF-8"?>' +
      '<s:Envelope xmlns:s="http://www.w3.org/2003/05/soap-envelope">' +
      (auth ? security(auth.user, auth.pass, auth.clockOffsetMs || 0) : '') +
      '<s:Body>' + body + '</s:Body></s:Envelope>';
    var xhr = new XMLHttpRequest();
    xhr.open('POST', url, true);
    xhr.timeout = 8000;
    xhr.setRequestHeader('Content-Type', 'application/soap+xml; charset=utf-8');
    xhr.onload = function () {
      var doc = new DOMParser().parseFromString(xhr.responseText, 'text/xml');
      var fault = first(doc, 'Fault');
      if (xhr.status !== 200 || fault) {
        var reason = fault ? text(fault, 'Text') || text(fault, 'Reason') : '';
        var authFail = xhr.status === 401 || /not ?authori|sender ?not|password|credential/i.test(reason + xhr.responseText);
        callback({ status: xhr.status, auth: authFail, message: reason || ('HTTP ' + xhr.status) });
        return;
      }
      callback(null, doc);
    };
    xhr.onerror = function () { callback({ message: I18n.t('onvif.no_response', { url: url }) }); };
    xhr.ontimeout = function () { callback({ message: I18n.t('onvif.timeout', { url: url }) }); };
    xhr.send(xml);
  }

  function all(node, name) {
    return Array.prototype.slice.call(node.getElementsByTagNameNS('*', name));
  }
  function first(node, name) { return all(node, name)[0] || null; }
  function text(node, name) {
    var n = name ? first(node, name) : node;
    return n ? n.textContent.trim() : '';
  }

  // ---- Discovery (WS-Discovery response coming from the WASM) ----
  function parseProbeMatch(from, xml) {
    var doc = new DOMParser().parseFromString(xml, 'text/xml');
    var xaddrs = text(doc, 'XAddrs').split(/\s+/).filter(Boolean);
    var scopes = text(doc, 'Scopes').split(/\s+/);
    function scope(kind) {
      var re = new RegExp(String.raw`^onvif://www\.onvif\.org/` + kind + '/(.+)$');
      var m = scopes.map(function (s) { return re.exec(s); }).find(Boolean);
      return m ? decodeURIComponent(m[1]).replace(/_/g, ' ') : '';
    }
    // Prefers the address on the same IP that answered (some devices list several).
    var xaddr = xaddrs.find(function (a) { return a.indexOf('//' + from) > 0; }) || xaddrs[0];
    return {
      ip: from,
      xaddr: xaddr,
      name: scope('name') || from,
      hardware: scope('hardware'),
      id: text(doc, 'Address') || from
    };
  }

  // ---- Channels and RTSP URLs ----
  function withCredentials(uri, user, pass) {
    if (!user) return uri;
    return uri.replace(/^rtsp:\/\/([^@/]*@)?/, 'rtsp://' + encodeURIComponent(user) + ':' +
      encodeURIComponent(pass) + '@');
  }

  // callback(err, {name, channels:[{name, main, sub, width, height}]})
  function getChannels(device, user, pass, callback) {
    var auth = { user: user, pass: pass, clockOffsetMs: 0 };

    // 1. Device clock (no login): the digest depends on the time.
    soap(device.xaddr, '<GetSystemDateAndTime xmlns="http://www.onvif.org/ver10/device/wsdl"/>', null,
      function (err, doc) {
        if (!err) {
          var utc = first(doc, 'UTCDateTime');
          if (utc) {
            var d = Date.UTC(+text(utc, 'Year'), +text(utc, 'Month') - 1, +text(utc, 'Day'),
              +text(utc, 'Hour'), +text(utc, 'Minute'), +text(utc, 'Second'));
            if (!Number.isNaN(d)) auth.clockOffsetMs = d - Date.now();
          }
        }
        getMediaUrl();
      });

    // 2. Address of the media service.
    function getMediaUrl() {
      soap(device.xaddr, '<GetCapabilities xmlns="http://www.onvif.org/ver10/device/wsdl"><Category>Media</Category></GetCapabilities>',
        auth, function (err, doc) {
          if (err) { callback(err); return; }
          var media = first(doc, 'Media');
          var url = media ? text(media, 'XAddr') : '';
          getProfiles(url || device.xaddr);
        });
    }

    // 3. Profiles: each video source is a channel; it may have several encoders.
    function getProfiles(mediaUrl) {
      soap(mediaUrl, '<GetProfiles xmlns="http://www.onvif.org/ver10/media/wsdl"/>', auth, function (err, doc) {
        if (err) { callback(err); return; }
        var profiles = all(doc, 'Profiles').map(function (p) {
          var vsc = first(p, 'VideoSourceConfiguration');
          var vec = first(p, 'VideoEncoderConfiguration');
          return {
            token: p.getAttribute('token'),
            name: text(p, 'Name'),
            source: vsc ? text(vsc, 'SourceToken') : '',
            encoding: vec ? text(vec, 'Encoding') : '',
            width: vec ? +text(first(vec, 'Resolution'), 'Width') : 0,
            height: vec ? +text(first(vec, 'Resolution'), 'Height') : 0
          };
        }).filter(function (p) { return p.source; });
        if (!profiles.length) { callback({ message: I18n.t('onvif.no_profiles') }); return; }
        getUris(mediaUrl, profiles);
      });
    }

    // 4. RTSP URL of each profile.
    function getUris(mediaUrl, profiles) {
      var pending = profiles.length;
      profiles.forEach(function (p) {
        soap(mediaUrl,
          '<GetStreamUri xmlns="http://www.onvif.org/ver10/media/wsdl"><StreamSetup>' +
          '<Stream xmlns="http://www.onvif.org/ver10/schema">RTP-Unicast</Stream>' +
          '<Transport xmlns="http://www.onvif.org/ver10/schema"><Protocol>RTSP</Protocol></Transport>' +
          '</StreamSetup><ProfileToken>' + xmlEscape(p.token) + '</ProfileToken></GetStreamUri>',
          auth, function (err, doc) {
            if (!err) p.uri = text(doc, 'Uri');
            if (--pending === 0) group(profiles);
          });
      });
    }

    // 5. Group by source: main = highest resolution, sub = lowest.
    function group(profiles) {
      var bySource = {};
      var order = [];
      profiles.forEach(function (p) {
        if (!p.uri) return;
        if (!bySource[p.source]) { bySource[p.source] = []; order.push(p.source); }
        bySource[p.source].push(p);
      });
      var channels = order.map(function (src, i) {
        var list = bySource[src].sort(function (a, b) { return b.width * b.height - a.width * a.height; });
        var h264 = list.filter(function (p) { return /h264|avc/i.test(p.encoding); });
        var main = h264[0] || list[0];
        var sub = h264.length > 1 ? h264[h264.length - 1] : main;
        return {
          name: order.length > 1 ? device.name + ' · ' + I18n.t('onvif.channel', { n: i + 1 }) : device.name,
          main: withCredentials(main.uri, user, pass),
          sub: withCredentials(sub.uri, user, pass),
          width: main.width,
          height: main.height,
          codec: main.encoding,
          supported: h264.length > 0
        };
      });
      if (!channels.length) { callback({ message: I18n.t('onvif.no_urls') }); return; }
      callback(null, { name: device.name, channels: channels });
    }
  }

  return {
    parseProbeMatch: parseProbeMatch,
    getChannels: getChannels,
    // Internals exposed for the unit tests.
    _sha1: sha1,
    _security: security,
    _withCredentials: withCredentials
  };
})();
