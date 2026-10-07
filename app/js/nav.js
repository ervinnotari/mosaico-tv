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

  // Distance from rectangle a to b moving in dir: {main, cross}, or null
  // when b is not in that direction.
  function distance(dir, a, b) {
    var dx = Math.abs((b.left + b.width / 2) - (a.left + a.width / 2));
    var dy = Math.abs((b.top + b.height / 2) - (a.top + a.height / 2));
    switch (dir) {
      case 'left': return b.right > a.left + 1 ? null : { main: a.left - b.right, cross: dy };
      case 'right': return b.left < a.right - 1 ? null : { main: b.left - a.right, cross: dy };
      case 'up': return b.bottom > a.top + 1 ? null : { main: a.top - b.bottom, cross: dx };
      default: return b.top < a.bottom - 1 ? null : { main: b.top - a.bottom, cross: dx };
    }
  }

  // Next element in the direction: favors the axis of movement and penalizes
  // misalignment on the other axis.
  function move(dir) {
    var list = candidates();
    if (!current || !list.includes(current)) { focus(list[0]); return true; }
    var a = current.getBoundingClientRect();
    var best = null, bestScore = Infinity;
    list.forEach(function (el) {
      if (el === current) return;
      var d = distance(dir, a, el.getBoundingClientRect());
      if (!d) return;
      var score = d.main + d.cross * 2;
      if (score < bestScore) { bestScore = score; best = el; }
    });
    // Entering a row of actions (.actions) from above/below starts at the
    // first one: it is the main action (Save, Connect...).
    if (best && (dir === 'up' || dir === 'down')) {
      var group = best.closest('.actions');
      if (group && !group.contains(current)) {
        var firstInGroup = list.find(function (el) { return group.contains(el); });
        if (firstInGroup) best = firstInGroup;
      }
    }
    if (best) focus(best);
    return !!best;
  }

  function startEditing(field) {
    var input = document.getElementById(field.dataset.input);
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

  var ARROWS = {};
  ARROWS[KEY.LEFT] = 'left';
  ARROWS[KEY.RIGHT] = 'right';
  ARROWS[KEY.UP] = 'up';
  ARROWS[KEY.DOWN] = 'down';
  var LEAVE_EDITING = [KEY.IME_DONE, KEY.IME_CANCEL, KEY.ENTER, KEY.BACK, KEY.ESC, KEY.UP, KEY.DOWN];

  function isEditable(el) {
    return el.classList.contains('field') && !!el.dataset.input;
  }

  // With the TV keyboard open, only keys that close it are handled; the
  // left/right arrows move the cursor in the text.
  function onEditingKey(k, ev) {
    if (!LEAVE_EDITING.includes(k)) return;
    stopEditing();
    ev.preventDefault();
    if (k === KEY.UP || k === KEY.DOWN) move(ARROWS[k]);
  }

  function onNavigationKey(k, ev) {
    if (ARROWS[k]) {
      move(ARROWS[k]);
      ev.preventDefault();
    } else if (k === KEY.ENTER && current) {
      ev.preventDefault();
      if (isEditable(current)) startEditing(current);
      else current.click();
    }
  }

  document.addEventListener('keydown', function (ev) {
    // The TV remote keys (Back 10009, CH±, IME) exist only as key codes.
    var k = ev.keyCode; // NOSONAR
    if (editing) { onEditingKey(k, ev); return; }
    if (Nav.onKey && Nav.onKey(k, current) === true) { ev.preventDefault(); return; }
    onNavigationKey(k, ev);
  });

  // A click from the remote's pointer (Smart Remote) also focuses.
  // The button handler runs before this one and may have changed the screen:
  // focus only if the element is still visible in the active area.
  document.addEventListener('click', function (ev) {
    var el = ev.target.closest && ev.target.closest('.focusable');
    if (!el || !root.contains(el) || !visible(el)) return;
    if (el !== current) focus(el);
    if (isEditable(el) && !editing) startEditing(el);
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
