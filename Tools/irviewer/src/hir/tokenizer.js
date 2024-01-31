// Copyright (c) Meta Platforms, Inc. and affiliates.
import { ArrayIter, Position } from "util.js";

function isWhitespace(c) {
  return c == " " || c == "\t" || c == "\n";
}

function isSingleCharToken(c) {
  return (
    c == "=" ||
    c == "<" ||
    c == ">" ||
    c == "," ||
    c == "{" ||
    c == "}" ||
    c == "(" ||
    c == ")" ||
    c == ";"
  );
}

function getStr(iter) {
  let str = "";
  let prev = undefined;
  while (!iter.isEmpty()) {
    let c = iter.next();
    str += c;
    if (c == '"' && prev !== "\\") {
      return str;
    }
    prev = c;
  }
  throw new Error("End of input in string literal '" + str + "'");
}

// Keeps track of our Position in a document
class CharArrayIter extends ArrayIter {
  constructor(chars) {
    super(chars);
    this.pos = new Position(1, 1);
  }

  next() {
    let c = super.next();
    if (c == "\n") {
      this.pos.lineNum++;
      this.pos.colNum = 1;
    } else {
      this.pos.colNum++;
    }
    return c;
  }
}

class Token {
  constructor(data, pos) {
    this.data = data;
    this.pos = pos;
  }
}

function tokenize(text) {
  let tokens = [];
  let iter = new CharArrayIter(text.split(""));
  while (!iter.isEmpty()) {
    iter.dropWhile(isWhitespace);
    if (iter.isEmpty()) {
      break;
    }
    let pos = iter.pos.copy();
    let c = iter.next();
    if (isSingleCharToken(c)) {
      tokens.push(new Token(c, pos));
      continue;
    }
    let token = new Token("", pos);
    while (true) {
      token.data += c;
      if (c == '"') {
        token.data += getStr(iter);
      }
      if (
        iter.isEmpty() ||
        isWhitespace(iter.peek()) ||
        isSingleCharToken(iter.peek())
      ) {
        break;
      }
      c = iter.next();
    }
    tokens.push(token);
  }
  return tokens;
}

export { Position, tokenize, Token };
