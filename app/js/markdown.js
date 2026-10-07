// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Ervin Notari Junior

/* Minimal Markdown converter for the "About" screen (credits and licenses).
 * Covers what those files use: headings, paragraphs, lists, tables,
 * quotes, code blocks, horizontal rule, **bold**, *italic*,
 * `code` and [links](url). All text is escaped: the result is safe HTML.
 * Links do not navigate (it is a TV): they show the text and the address. */
'use strict';

var Markdown = (function () {
  function escape(s) {
    return String(s).replace(/&/g, '&amp;').replace(/</g, '&lt;').replace(/>/g, '&gt;')
      .replace(/"/g, '&quot;');
  }

  // Inline formatting. Spans `between backticks` are not formatted.
  function inline(text) {
    return String(text).split(/(`[^`]*`)/).map(function (part) {
      if (/^`[^`]*`$/.test(part)) return '<code>' + escape(part.slice(1, -1)) + '</code>';
      var s = escape(part);
      s = s.replace(/\[([^[\]]+)\]\(([^()\s]+)\)/g, function (m, label, url) {
        return '<span class="md-link">' + label + '</span>' +
          (label === url ? '' : ' <span class="md-url">' + url + '</span>');
      });
      s = s.replace(/\*\*([^*]+)\*\*/g, '<strong>$1</strong>');
      s = s.replace(/(^|[^*\w])\*([^*\s][^*]*)\*(?!\w)/g, '$1<em>$2</em>');
      s = s.replace(/(^|[^\w])_([^_\s][^_]*)_(?!\w)/g, '$1<em>$2</em>');
      return s;
    }).join('');
  }

  // Block patterns, written to match in linear time (no nested repetition).
  var RE = {
    fence: /^\s*```/,
    heading: /^(#{1,6})\s(.*)$/,
    hr: /^\s*(-{3,}|\*{3,}|_{3,})\s*$/,
    item: /^\s*([-*+]|\d{1,9}[.)])\s(.*)$/,
    quote: /^\s*>\s?(.*)$/,
    sepCell: /^:?-{3,}:?$/
  };

  function cells(row) {
    var r = row.trim();
    if (r.startsWith('|')) r = r.slice(1);
    if (r.endsWith('|')) r = r.slice(0, -1);
    return r.split('|').map(function (cell) { return cell.trim(); });
  }

  // "| --- | :---: |": the line under a table header.
  function isTableSep(line) {
    return line.includes('-') && cells(line).every(function (cell) { return RE.sepCell.test(cell); });
  }

  function isTableStart(lines, i) {
    return lines[i].includes('|') && i + 1 < lines.length && isTableSep(lines[i + 1]);
  }

  function isBlockStart(lines, i) {
    var l = lines[i];
    return RE.fence.test(l) || RE.heading.test(l) || RE.hr.test(l) || RE.item.test(l) ||
      RE.quote.test(l) || isTableStart(lines, i);
  }

  // Each block parser takes the lines and the current index; if the block
  // starts there it appends its HTML to out and returns the next index,
  // otherwise it returns -1.
  function fence(lines, i, out) {
    if (!RE.fence.test(lines[i])) return -1;
    var code = [];
    for (i++; i < lines.length && !RE.fence.test(lines[i]); i++) code.push(lines[i]);
    out.push('<pre class="md-code"><code>' + escape(code.join('\n')) + '</code></pre>');
    return i + 1;  // skips the closing fence
  }

  function heading(lines, i, out) {
    var m = RE.heading.exec(lines[i]);
    if (!m) return -1;
    var text = m[2].trim();
    while (text.endsWith('#')) text = text.slice(0, -1);  // optional closing #s
    var level = m[1].length;
    out.push('<h' + level + '>' + inline(text.trim()) + '</h' + level + '>');
    return i + 1;
  }

  function rule(lines, i, out) {
    if (!RE.hr.test(lines[i])) return -1;
    out.push('<hr>');
    return i + 1;
  }

  function table(lines, i, out) {
    if (!isTableStart(lines, i)) return -1;
    var head = cells(lines[i]);
    var rows = [];
    for (i += 2; i < lines.length && lines[i].includes('|') && lines[i].trim(); i++) rows.push(cells(lines[i]));
    out.push('<table><thead><tr>' + head.map(function (h) { return '<th>' + inline(h) + '</th>'; }).join('') +
      '</tr></thead><tbody>' + rows.map(function (r) {
        return '<tr>' + r.map(function (d) { return '<td>' + inline(d) + '</td>'; }).join('') + '</tr>';
      }).join('') + '</tbody></table>');
    return i;
  }

  function quote(lines, i, out) {
    var quoted = [];
    for (var m = RE.quote.exec(lines[i]); m; m = i < lines.length ? RE.quote.exec(lines[i]) : null) {
      quoted.push(m[1]);
      i++;
    }
    if (!quoted.length) return -1;
    out.push('<blockquote>' + render(quoted.join('\n')) + '</blockquote>');
    return i;
  }

  function list(lines, i, out) {
    var m = RE.item.exec(lines[i]);
    if (!m) return -1;
    var ordered = /\d/.test(m[1]);
    var items = [];
    for (; m; m = i < lines.length ? RE.item.exec(lines[i]) : null) {
      var text = m[2].trim();
      // Following indented lines continue the same item.
      for (i++; i < lines.length && /^\s{2,}\S/.test(lines[i]) && !RE.item.test(lines[i]); i++) {
        text += ' ' + lines[i].trim();
      }
      items.push('<li>' + inline(text) + '</li>');
    }
    out.push((ordered ? '<ol>' : '<ul>') + items.join('') + (ordered ? '</ol>' : '</ul>'));
    return i;
  }

  function paragraph(lines, i, out) {
    var para = [];
    for (; i < lines.length && lines[i].trim() && (para.length === 0 || !isBlockStart(lines, i)); i++) {
      para.push(lines[i].trim());
    }
    out.push('<p>' + inline(para.join(' ')) + '</p>');
    return i;
  }

  var BLOCKS = [fence, heading, rule, table, quote, list, paragraph];

  // The first parser that recognizes the block renders it; paragraph always does.
  function block(lines, i, out) {
    for (var b = 0; b < BLOCKS.length; b++) {
      var next = BLOCKS[b](lines, i, out);
      if (next >= 0) return next;
    }
    return i + 1;
  }

  function render(md) {
    var lines = String(md).replace(/\r\n?/g, '\n').split('\n');
    var out = [];
    var i = 0;
    while (i < lines.length) {
      if (!lines[i].trim()) { i++; continue; }
      i = block(lines, i, out);
    }
    return out.join('\n');
  }

  return { render: render, inline: inline, escape: escape };
})();
