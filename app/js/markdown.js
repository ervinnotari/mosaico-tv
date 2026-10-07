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
      s = s.replace(/\[([^\]]+)\]\(([^)\s]+)\)/g, function (m, label, url) {
        return '<span class="md-link">' + label + '</span>' +
          (label === url ? '' : ' <span class="md-url">' + url + '</span>');
      });
      s = s.replace(/\*\*([^*]+)\*\*/g, '<strong>$1</strong>');
      s = s.replace(/(^|[^*\w])\*([^*\s][^*]*)\*(?!\w)/g, '$1<em>$2</em>');
      s = s.replace(/(^|[^\w])_([^_\s][^_]*)_(?!\w)/g, '$1<em>$2</em>');
      return s;
    }).join('');
  }

  var RE = {
    fence: /^\s*```/,
    heading: /^(#{1,6})\s+(.*?)\s*#*\s*$/,
    hr: /^\s*(-{3,}|\*{3,}|_{3,})\s*$/,
    item: /^(\s*)([-*+]|\d+[.)])\s+(.*)$/,
    quote: /^\s*>\s?(.*)$/,
    tableSep: /^\s*\|?\s*:?-{3,}:?\s*(\|\s*:?-{3,}:?\s*)*\|?\s*$/
  };

  function cells(row) {
    return row.trim().replace(/^\|/, '').replace(/\|$/, '').split('|').map(function (c) { return c.trim(); });
  }

  function isBlockStart(lines, i) {
    var l = lines[i];
    return RE.fence.test(l) || RE.heading.test(l) || RE.hr.test(l) || RE.item.test(l) ||
      RE.quote.test(l) || (l.indexOf('|') >= 0 && i + 1 < lines.length && RE.tableSep.test(lines[i + 1]));
  }

  function render(md) {
    var lines = String(md).replace(/\r\n?/g, '\n').split('\n');
    var out = [];
    var i = 0;
    while (i < lines.length) {
      var line = lines[i];
      var m;

      if (!line.trim()) { i++; continue; }

      if (RE.fence.test(line)) {
        var code = [];
        for (i++; i < lines.length && !RE.fence.test(lines[i]); i++) code.push(lines[i]);
        i++;  // closes the block
        out.push('<pre class="md-code"><code>' + escape(code.join('\n')) + '</code></pre>');
        continue;
      }

      if ((m = line.match(RE.heading))) {
        var level = m[1].length;
        out.push('<h' + level + '>' + inline(m[2]) + '</h' + level + '>');
        i++;
        continue;
      }

      if (RE.hr.test(line)) { out.push('<hr>'); i++; continue; }

      if (line.indexOf('|') >= 0 && i + 1 < lines.length && RE.tableSep.test(lines[i + 1])) {
        var head = cells(line);
        var rows = [];
        for (i += 2; i < lines.length && lines[i].indexOf('|') >= 0 && lines[i].trim(); i++) rows.push(cells(lines[i]));
        out.push('<table><thead><tr>' + head.map(function (c) { return '<th>' + inline(c) + '</th>'; }).join('') +
          '</tr></thead><tbody>' + rows.map(function (r) {
            return '<tr>' + r.map(function (c) { return '<td>' + inline(c) + '</td>'; }).join('') + '</tr>';
          }).join('') + '</tbody></table>');
        continue;
      }

      if (RE.quote.test(line)) {
        var quoted = [];
        for (; i < lines.length && (m = lines[i].match(RE.quote)); i++) quoted.push(m[1]);
        out.push('<blockquote>' + render(quoted.join('\n')) + '</blockquote>');
        continue;
      }

      if ((m = line.match(RE.item))) {
        var ordered = /\d/.test(m[2]);
        var items = [];
        while (i < lines.length && (m = lines[i].match(RE.item))) {
          var text = m[3];
          // Following indented lines continue the same item.
          for (i++; i < lines.length && /^\s{2,}\S/.test(lines[i]) && !RE.item.test(lines[i]); i++) {
            text += ' ' + lines[i].trim();
          }
          items.push('<li>' + inline(text) + '</li>');
        }
        out.push((ordered ? '<ol>' : '<ul>') + items.join('') + (ordered ? '</ol>' : '</ul>'));
        continue;
      }

      var para = [];
      for (; i < lines.length && lines[i].trim() && (para.length === 0 || !isBlockStart(lines, i)); i++) {
        para.push(lines[i].trim());
      }
      out.push('<p>' + inline(para.join(' ')) + '</p>');
    }
    return out.join('\n');
  }

  return { render: render, inline: inline, escape: escape };
})();
