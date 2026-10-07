// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Ervin Notari Junior

'use strict';

const test = require('node:test');
const assert = require('node:assert');
const fs = require('fs');
const path = require('path');
const { load } = require('./harness');

const { Markdown } = load(['markdown.js']);
const ROOT = path.join(__dirname, '..', '..');

test('headings, paragraphs and inline formatting', () => {
  const html = Markdown.render('# Title\n\nLine one\ncontinues **strong** and *emphasis* and `a<b`.');
  assert.strictEqual(html,
    '<h1>Title</h1>\n<p>Line one continues <strong>strong</strong> and <em>emphasis</em> and <code>a&lt;b</code>.</p>');
});

test('links show the text and the address, without navigation', () => {
  assert.strictEqual(Markdown.inline('[OpenH264](https://github.com/cisco/openh264)'),
    '<span class="md-link">OpenH264</span> <span class="md-url">https://github.com/cisco/openh264</span>');
  // Text equal to the address: not repeated.
  assert.strictEqual(Markdown.inline('[x](x)'), '<span class="md-link">x</span>');
});

test('table with header', () => {
  const html = Markdown.render('| A | B |\n|---|:---:|\n| 1 | **2** |\n| 3 | 4 |');
  assert.strictEqual(html,
    '<table><thead><tr><th>A</th><th>B</th></tr></thead><tbody>' +
    '<tr><td>1</td><td><strong>2</strong></td></tr><tr><td>3</td><td>4</td></tr></tbody></table>');
});

test('lists, with indented continuation, and numbered lists', () => {
  assert.strictEqual(Markdown.render('- one\n- two\n  continues\n- three'),
    '<ul><li>one</li><li>two continues</li><li>three</li></ul>');
  assert.strictEqual(Markdown.render('1. a\n2. b'), '<ol><li>a</li><li>b</li></ol>');
});

test('code block keeps the text and does not format it', () => {
  const html = Markdown.render('```\nTHE SOFTWARE IS PROVIDED "AS IS" **x** <b>\n  indent\n```');
  assert.strictEqual(html,
    '<pre class="md-code"><code>THE SOFTWARE IS PROVIDED &quot;AS IS&quot; **x** &lt;b&gt;\n  indent</code></pre>');
});

test('quote and horizontal rule', () => {
  assert.strictEqual(Markdown.render('> citado\n> aqui\n\n---'),
    '<blockquote><p>citado aqui</p></blockquote>\n<hr>');
});

test('HTML embedded in Markdown is escaped (safe result)', () => {
  const html = Markdown.render('<script>alert(1)</script>\n\n[x](javascript:alert(1)) <img src=x onerror=y>');
  assert.ok(!/<script|<img/.test(html), html);
  assert.ok(html.includes('&lt;script&gt;'));
});

test('italic does not break words with underscores or loose asterisks', () => {
  assert.strictEqual(Markdown.inline('sprop_parameter_sets e 2 * 3 * 4'), 'sprop_parameter_sets e 2 * 3 * 4');
});

test('the real project files render with the expected structure', () => {
  const credits = Markdown.render(fs.readFileSync(path.join(ROOT, 'CREDITS.md'), 'utf8'));
  assert.ok(credits.startsWith('<h1>Credits</h1>'));
  assert.strictEqual((credits.match(/<table>/g) || []).length, 1);
  assert.ok(credits.includes('<td>Cisco Systems</td>'));
  assert.ok(credits.includes('Ervin Notari Junior'));

  const notices = Markdown.render(fs.readFileSync(path.join(ROOT, 'THIRD_PARTY_NOTICES.md'), 'utf8'));
  // Each license stays in a code block, with no accidental formatting.
  assert.ok((notices.match(/<pre class="md-code">/g) || []).length >= 4);
  assert.ok(notices.includes('Copyright (c) 2013, Cisco Systems'));
  for (const block of notices.match(/<pre class="md-code">[\s\S]*?<\/pre>/g)) {
    assert.ok(!/<(em|strong)>/.test(block), 'formatting inside a license text');
  }
});
