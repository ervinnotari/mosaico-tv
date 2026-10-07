// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Ervin Notari Junior

/* Test double for the browser's DOMParser: enough XML DOM for onvif.js
 * (getElementsByTagNameNS with '*', getAttribute and textContent). It reads
 * well-formed SOAP/WS-Discovery replies; it is not a general XML parser. */
'use strict';

function decode(s) {
  return s.replace(/&(lt|gt|quot|apos|amp);/g, (m, e) => ({ lt: '<', gt: '>', quot: '"', apos: "'", amp: '&' })[e]);
}

class Node {
  constructor(name, attrs) {
    this.localName = name.includes(':') ? name.split(':')[1] : name;
    this.attrs = attrs || {};
    this.children = [];
  }
  get textContent() {
    return this.children.map((c) => (typeof c === 'string' ? c : c.textContent)).join('');
  }
  getAttribute(name) { return name in this.attrs ? this.attrs[name] : null; }
  getElementsByTagNameNS(ns, name) {
    const out = [];
    const walk = (n) => n.children.forEach((c) => {
      if (typeof c === 'string') return;
      if (c.localName === name) out.push(c);
      walk(c);
    });
    walk(this);
    return out;
  }
}

function parse(xml) {
  const doc = new Node('#document');
  const stack = [doc];
  const tag = /<(\/?)([\w:.-]+)((?:\s+[\w:.-]+\s*=\s*"[^"]*")*)\s*(\/?)>|<\?[^>]*\?>|<!--[\s\S]*?-->|([^<]+)/g;
  for (const m of xml.matchAll(tag)) {
    const top = stack.at(-1);
    if (m[5] !== undefined) { top.children.push(decode(m[5])); continue; }
    if (!m[2]) continue;  // declaration or comment
    if (m[1]) { stack.pop(); continue; }
    const attrs = {};
    for (const a of (m[3] || '').matchAll(/([\w:.-]+)\s*=\s*"([^"]*)"/g)) attrs[a[1]] = decode(a[2]);
    const node = new Node(m[2], attrs);
    top.children.push(node);
    if (!m[4]) stack.push(node);
  }
  return doc;
}

class DOMParser {
  parseFromString(xml) { return parse(String(xml)); }
}

module.exports = { DOMParser };
