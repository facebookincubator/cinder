// Copyright (c) Meta Platforms, Inc. and affiliates.
import { parse, ParseError } from "hir/parser.js";
import { Position } from "hir/tokenizer.js";
import { Block, CFG, Edge, Instr, Register } from "graph.js";

function expectParseError(src, msg, lineNum, colNum) {
  try {
    parse(src);
  } catch (err) {
    expect(err).toBeInstanceOf(ParseError);
    expect(err).toHaveProperty("msg", msg);
    if (lineNum !== undefined && colNum !== undefined) {
      expect(err).toHaveProperty("pos", new Position(lineNum, colNum));
    }
    return;
  }
  fail("No error raised");
}

test("handles incomplete programs", () => {
  let src = "fun rlcompleter:<lambda>";
  expectParseError(src, "unexpected end of input", 1, 25);
});

test("handles unknown opcodes", () => {
  let src = `
fun foo {
  bb 0 {
    v4:OptObject = Zazzle
    v5:Object = Bazzle<1> v4
    Blargle v5
  }
}
    `;
  parse(src);
});

test("parses blocks with no predecessors", () => {
  let src = `
fun foo {
  bb 0 {
  }
}
    `;
  let block = new Block("bb0");
  block.isEntryBlock = true;
  let graph = new CFG("foo", [block], []);
  expect(parse(src)).toEqual(graph);
});

test("parses blocks with predecessors", () => {
  let src = `
fun foo {
  bb 0 {
  }

  bb 1 (preds 0) {
  }

  bb 2 (preds 0) {
  }

  bb 3 (preds 1, 2) {
  }
}
    `;
  let blocks = [0, 1, 2, 3].map((i) => {
    let block = new Block("bb" + i);
    if (i == 0) {
      block.isEntryBlock = true;
    }
    return block;
  });
  let edges = [
    new Edge("bb0", "bb1"),
    new Edge("bb0", "bb2"),
    new Edge("bb1", "bb3"),
    new Edge("bb2", "bb3"),
  ];
  let graph = new CFG("foo", blocks, edges);
  expect(parse(src)).toEqual(graph);
});

test("parses instructions", () => {
  let src = `
fun __main__:CoroAsyncIOCompatTest.test_asyncio_1.<locals>.f.<lambda> {
  bb 0 {
    v4:OptObject = LoadGlobalCached<0; "print">
    CondBranch<2, 1> v4
  }

  bb 2 (preds 0) {
    v5:Object = RefineType<Object> v4
    Incref v5
    Branch<3>
  }

  bb 1 (preds 0) {
    v6:OptObject = LoadGlobal<0; "print">
    v7:Object = CheckExc<1, o:v6> v6 {
      NextInstrOffset 2
    }
    Branch<3>
  }

  bb 3 (preds 1, 2) {
    v8:Object = Phi<1, 2> v7 v5
    v9:UnicodeExact["hi"] = LoadConst<1>
    v10:OptObject = Call<1> v8 v9
    Decref v8
    v11:Object = CheckExc<1, o:v10> v10 {
      NextInstrOffset 6
      BlockStack {
        Opcode 122 HandlerOff 40 StackLevel 1
      }
    }
    Decref v11
    v12:NoneType = LoadConst<0>
    Incref v12
    Return v12
  }
}    `;
  parse(src);
});
