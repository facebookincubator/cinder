// Copyright (c) Meta Platforms, Inc. and affiliates.
import { ArrayIter, Position } from "util.js";
import { tokenize } from "hir/tokenizer.js";
import { Block, CFG, Edge, Instr, Register } from "graph.js";

class ParseError extends Error {
  constructor(message, position) {
    super(message);
    this.msg = message;
    this.pos = position;
  }
}

// Keeps track of our Position in a stream of Tokens
class TokenIter extends ArrayIter {
  constructor(tokens) {
    super(tokens);
    if (tokens.length > 0) {
      this.pos = tokens[0].pos.copy();
    } else {
      this.pos = new Position(1, 1);
    }
  }

  assertNotEmpty() {
    if (this.idx >= this.elems.length) {
      throw new ParseError("unexpected end of input", this.pos);
    }
  }

  next() {
    let token = super.next();
    if (this.isEmpty()) {
      this.pos.colNum += token.data.length;
    } else {
      this.pos = this.elems[this.idx].pos.copy();
    }
    return token;
  }

  headEquals(data) {
    let token = this.peek;
    return token.data == data;
  }

  expect(data) {
    let token = this.next();
    if (token.data != data) {
      let pos = token.pos;
      throw new ParseError(
        "expected '" + data + "', got '" + token.data + "'",
        pos
      );
    }
  }
}

function getData(token) {
  return token.data;
}

function parseFrameState(tokens) {
  // TODO(mpage) - Fill this out
  if (tokens.peek().data != "{") {
    return {};
  }
  tokens.expect("{");
  let block_depth = 1;
  tokens.dropWhile((t) => {
    if (t.data == "{") {
      block_depth++;
    } else if (t.data == "}") {
      block_depth--;
    }
    return block_depth != 0;
  });
  tokens.expect("}");
  return {};
}

function parseImmediates(tokens) {
  if (tokens.peek().data != "<") {
    return [];
  }
  tokens.expect("<");
  let immediates = tokens
    .nextWhile((t) => t.data != ">")
    .map(getData)
    .filter((t) => t != ",");
  tokens.expect(">");
  return immediates;
}

function isOperand(tokens) {
  return tokens.peek().data.charAt(0) == "v" && tokens.peek(1).data != "=";
}

function parseOperands(registers, tokens) {
  let operands = [];
  while (isOperand(tokens)) {
    let name = tokens.next().data;
    if (!registers.hasOwnProperty(name)) {
      registers[name] = new Register(name);
    }
    operands.push(registers[name]);
  }
  return operands;
}

function parseInstr(registers, tokens) {
  let outName = undefined;
  let outType = undefined;
  let opcodeToken = tokens.next();
  let opcode = opcodeToken.data;
  if (tokens.peek().data == "=") {
    let pos = opcode.indexOf(":");
    outName = opcode.substr(0, pos);
    outType = opcode.substr(pos + 1);
    tokens.next();
    opcodeToken = tokens.next();
    opcode = opcodeToken.data;
  }
  let output = undefined;
  if (outName !== undefined) {
    if (!registers.hasOwnProperty(outName)) {
      registers[outName] = new Register(outName);
    }
    output = registers[outName];
    output.type = outType;
  }
  let immediates = parseImmediates(tokens);
  let operands = parseOperands(registers, tokens);
  let frameState = parseFrameState(tokens);
  return new Instr(opcode, immediates, operands, output);
}

function parseBlock(registers, tokens) {
  tokens.expect("bb");
  let id = tokens.next().data;
  let block = new Block("bb" + id);
  block.isEntryBlock = id == "0";
  let edges = [];
  // Grab predecessor ids if we have them
  if (tokens.peek().data == "(") {
    tokens.next();
    edges = tokens
      .nextWhile((t) => t.data != ")")
      .map((t) => t.data)
      .filter((t) => t != "preds" && t != ",")
      .map((id) => new Edge("bb" + id, block.id));
    tokens.expect(")");
  }
  tokens.expect("{");
  while (tokens.peek().data != "}") {
    let instr = parseInstr(registers, tokens);
    block.instrs.push(instr);
  }
  tokens.expect("}");
  let numInstrs = block.instrs.length;
  if (numInstrs > 0) {
    block.isExitBlock = block.instrs[numInstrs - 1].opcode == "Return";
  }
  return [block, edges];
}

function parseFuncName(tokens) {
  let name = "";
  do {
    name += tokens.next().data;
    // Handle angle-bracketed elements e.g.: x.<foo>.y.<bar>
    if (tokens.peek().data == "<") {
      name += tokens
        .nextN(3)
        .map((t) => t.data)
        .join("");
      if (tokens.peek().data[0] != ".") {
        break;
      }
    } else {
      break;
    }
  } while (true);
  return name;
}

function parse(text) {
  let tokens = new TokenIter(tokenize(text));
  tokens.expect("fun");
  let funcName = parseFuncName(tokens);
  tokens.expect("{");
  let blocks = [];
  let edges = [];
  let registers = {};
  while (tokens.peek().data != "}") {
    let [block, blockEdges] = parseBlock(registers, tokens);
    blocks.push(block);
    Array.prototype.push.apply(edges, blockEdges);
  }
  tokens.expect("}");
  return new CFG(funcName, blocks, edges);
}

export { parse, ParseError };
