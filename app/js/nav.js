// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Ervin Notari Junior

/* Remote-control navigation: the arrows move to the nearest .focusable
 * element in that direction, inside the active screen (or dialog). Text
 * fields are navigated through their .field container; OK opens the TV keyboard. */
'use strict';

var Nav = (function () {
  var KEY = {
    LEFT: 37, UP: 38, RIGHT: 39, DOWN: 40, ENTER: 13, BACK: 10009, ESC: 27,
    IME_DONE: 65376, IME_CANCEL: 65385, CH_UP: 427, CH_DOWN: 428
  };

  var root = document.body;
  var current = null;
  var editing = null;  // <input> with the keyboard open

  function visible(el) {
    if (!el.offsetParent && getComputedStyle(el).position !== 'fixed') return false;
    var r = el.getBoundingClientRect();
    return r.width > 0 && r.height > 0;
  }

  function candidates() {
    return Array.prototype.filter.call(root.querySelectorAll('.focusable'), function (el) {
      return visible(el) && !el.disabled && !el.classList.contains('hidden');
    });
  }

  function focus(el) {
    if (!el) return;
    if (current) current.classList.remove('focused');
    current = el;
    el.classList.add('focused');
    if (el.tagName === 'BUTTON') el.focus();
    else if (document.activeElement && document.activeElement !== document.body) {
      document.activeElement.blur();
    }
    if (el.scrollIntoView) {
      var r = el.getBoundingClientRect();
      if (r.top < 0 || r.bottom > 1080) el.scrollIntoView({ block: 'nearest' });
    }
    if (Nav.onFocus) Nav.onFocus(el);
  }

  // Next element in the direction: favors the axis of movement and penalizes
  // misalignment on the other axis.
  function move(dir) {
    var list = candidates();
    if (!current || list.indexOf(current) < 0) { focus(list[0]); return true; }
    var a = current.getBoundingClientRect();
    var ax = a.left + a.width / 2, ay = a.top + a.height / 2;
    var best = null, bestScore = Infinity;
    list.forEach(function (el) {
      if (el === current) return;
      var b = el.getBoundingClientRect();
      var bx = b.left + b.width / 2, by = b.top + b.height / 2;
      var main, cross;
      if (dir === 'left') { if (b.right > a.left + 1) return; main = a.left - b.right; cross = Math.abs(by - ay); }
      else if (dir === 'right') { if (b.left < a.right - 1) return; main = b.left - a.right; cross = Math.abs(by - ay); }
      else if (dir === 'up') { if (b.bottom > a.top + 1) return; main = a.top - b.bottom; cross = Math.abs(bx - ax); }
      else { if (b.top < a.bottom - 1) return; main = b.top - a.bottom; cross = Math.abs(bx - ax); }
      var score = main + cross * 2;
      if (score < bestScore) { bestScore = score; best = el; }
    });
    // Entering a row of actions (.actions) from above/below starts at the
    // first one: it is the main action (Save, Connect...).
    if (best && (dir === 'up' || dir === 'down')) {
      var group = best.closest('.actions');
      if (group && !group.contains(current)) {
        var firstInGroup = list.filter(function (el) { return group.contains(el); })[0];
        if (firstInGroup) best = firstInGroup;
      }
    }
    if (best) focus(best);
    return !!best;
  }

  function startEditing(field) {
    var input = document.getElementById(field.getAttribute('data-input'));
    if (!input) return;
    editing = input;
    field.classList.add('editing');
    input.focus();
    // Puts the cursor at the end of the text.
    try { input.setSelectionRange(input.value.length, input.value.length); } catch (e) { /* password */ }
  }

  function stopEditing() {
    if (!editing) return;
    var field = editing.closest('.field');
    editing.blur();
    editing = null;
    if (field) field.classList.remove('editing');
    if (Nav.onEdited) Nav.onEdited(field);
  }

  document.addEventListener('keydown', function (ev) {
    var k = ev.keyCode;

    if (editing) {
      if (k === KEY.IME_DONE || k === KEY.IME_CANCEL || k === KEY.ENTER ||
          k === KEY.BACK || k === KEY.ESC || k === KEY.UP || k === KEY.DOWN) {
        stopEditing();
        ev.preventDefault();
        if (k === KEY.UP) move('up');
        if (k === KEY.DOWN) move('down');
      }
      return;  // left/right arrows move the cursor in the text
    }

    if (Nav.onKey && Nav.onKey(k, current) === true) { ev.preventDefault(); return; }

    switch (k) {
      case KEY.LEFT: move('left'); ev.preventDefault(); break;
      case KEY.RIGHT: move('right'); ev.preventDefault(); break;
      case KEY.UP: move('up'); ev.preventDefault(); break;
      case KEY.DOWN: move('down'); ev.preventDefault(); break;
      case KEY.ENTER:
        if (!current) break;
        ev.preventDefault();
        if (current.classList.contains('field') && current.getAttribute('data-input')) {
          startEditing(current);
        } else {
          current.click();
        }
        break;
    }
  });

  // A click from the remote's pointer (Smart Remote) also focuses.
  // The button handler runs before this one and may have changed the screen:
  // focus only if the element is still visible in the active area.
  document.addEventListener('click', function (ev) {
    var el = ev.target.closest && ev.target.closest('.focusable');
    if (!el || !root.contains(el) || !visible(el)) return;
    if (el !== current) focus(el);
    if (el.classList.contains('field') && el.getAttribute('data-input') && !editing) startEditing(el);
  });

  return {
    KEY: KEY,
    focus: focus,
    move: move,
    current: function () { return current; },
    // Sets the navigable area (screen or dialog) and the initial focus.
    setRoot: function (el, initial) {
      stopEditing();
      root = el;
      var target = initial || candidates()[0];
      if (target) focus(target);
      else if (current) { current.classList.remove('focused'); current = null; }
    },
    root: function () { return root; },
    isEditing: function () { return !!editing; },
    onKey: null,
    onFocus: null,
    onEdited: null
  };
})();
