/* Copyright (c) 2020 Peter Solopov.

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE. */
const CODES = {
  backspace: 8,
  tab: 9,
  enter: 13,
  shift: 16,
  control: 17,
  alt: 18,
  pause: 19,
  capslock: 20,
  escape: 27,
  " ": 32,
  pageup: 33,
  pagedown: 34,
  end: 35,
  home: 36,
  arrowleft: 37,
  arrowup: 38,
  arrowright: 39,
  arrowdown: 40,
  insert: 45,
  delete: 46,
  meta: 91,
  numlock: 144,
  scrolllock: 145,
  ";": 186,
  "=": 187,
  ",": 188,
  "-": 189,
  ".": 190,
  "/": 191,
  "`": 192,
  "[": 219,
  "\\": 220,
  "]": 221,
  "'": 222,

  // aliases
  add: 187,
};

const IS_MAC =
  typeof window != "undefined" &&
  /Mac|iPod|iPhone|iPad/.test(window.navigator.platform);

const MODIFIERS = {
  alt: "altKey",
  control: "ctrlKey",
  meta: "metaKey",
  shift: "shiftKey",
  "ctrl/cmd": IS_MAC ? "metaKey" : "ctrlKey",
};

function toKeyCode(name) {
  return CODES[name] || name.toUpperCase().charCodeAt(0);
}

function isKey(string, event) {
  const keys = string.split("+").reduce(
    (acc, key) => {
      if (MODIFIERS[key]) {
        acc.modifiers[MODIFIERS[key]] = true;
        return acc;
      }

      return {
        ...acc,
        keyCode: toKeyCode(key),
      };
    },
    {
      modifiers: {
        altKey: false,
        ctrlKey: false,
        metaKey: false,
        shiftKey: false,
      },
      keyCode: null,
    }
  );

  const hasModifiers = Object.keys(keys.modifiers).every((key) => {
    const value = keys.modifiers[key];
    return value ? event[key] : !event[key];
  });

  const hasKey = keys.keyCode ? event.which === keys.keyCode : true;

  return hasModifiers && hasKey;
}

export default isKey;
