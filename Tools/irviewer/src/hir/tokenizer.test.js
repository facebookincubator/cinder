// Copyright (c) Meta Platforms, Inc. and affiliates.
import { Position, tokenize, Token } from "hir/tokenizer.js";

function expectToken(tokens, idx, data, lineNum, colNum) {
  let token = new Token(data, new Position(lineNum, colNum));
  expect(tokens[idx]).toEqual(token);
}

function expectData(tokens, idx, data) {
  expect(tokens[idx]).toEqual(data);
}

function getData(token) {
  return token.data;
}

test("ignores whitespace", () => {
  let tokens = tokenize("foo bar    baz\n\n\t jazz");
  let t = expectToken.bind(null, tokens);
  t(0, "foo", 1, 1);
  t(1, "bar", 1, 5);
  t(2, "baz", 1, 12);
  t(3, "jazz", 3, 3);
});

test("handles single character tokens", () => {
  let data = tokenize("foo;<lambda> 0 { bb 0 (preds 1, 2) }").map(getData);
  let expected = [
    "foo",
    ";",
    "<",
    "lambda",
    ">",
    "0",
    "{",
    "bb",
    "0",
    "(",
    "preds",
    "1",
    ",",
    "2",
    ")",
    "}",
  ];
  expect(data).toEqual(expected);
});

test("handles quoted strings", () => {
  let data = tokenize('LoadMethod<1; "set_completer">').map(getData);
  expect(data).toEqual(["LoadMethod", "<", "1", ";", '"set_completer"', ">"]);

  data = tokenize(' "foo"bar""').map(getData);
  expect(data).toEqual(['"foo"bar""']);
});
