// Copyright (c) Meta Platforms, Inc. and affiliates.
class Position {
  constructor(lineNum, colNum) {
    this.lineNum = lineNum;
    this.colNum = colNum;
  }

  copy() {
    return new Position(this.lineNum, this.colNum);
  }

  toString() {
    return "line " + this.lineNum + ", col " + this.colNum;
  }
}

class ArrayIter {
  constructor(elems) {
    this.elems = elems;
    this.idx = 0;
  }

  assertNotEmpty(off = 0) {
    if (this.idx + off >= this.elems.length) {
      throw new Error("iterator exhausted");
    }
  }

  peek(off = 0) {
    this.assertNotEmpty(off);
    return this.elems[this.idx + off];
  }

  next() {
    this.assertNotEmpty();
    return this.elems[this.idx++];
  }

  nextN(numItems) {
    let items = [];
    for (let i = 0; i < numItems; i++) {
      items.push(this.next());
    }
    return items;
  }

  nextWhile(fn) {
    let items = [];
    while (!this.isEmpty() && fn(this.peek())) {
      items.push(this.next());
    }
    return items;
  }

  isEmpty() {
    return this.idx == this.elems.length;
  }

  dropWhile(fn) {
    while (!this.isEmpty() && fn(this.peek())) {
      this.next();
    }
  }

  dropN(numItems) {
    for (let i = 0; i < numItems; i++) {
      this.next();
    }
  }

  expect(item) {
    let got = this.next();
    if (got != item) {
      throw new Error("expected " + item + ", got " + got);
    }
    return got;
  }
}

export { ArrayIter, Position };
