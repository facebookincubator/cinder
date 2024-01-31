// Copyright (c) Meta Platforms, Inc. and affiliates.
// This file defines the data model used by the fronted. It's currently tied
// somewhat closely to HIR, however, with minor changes we should be able to
// represent other IRs (ideally LIR) easily.
class Register {
  constructor(name, type) {
    this.name = name;
    this.type = type;
  }
}

// An instruction consists of
// - An opcode (string)
// - Immediates (list of strings)
// - Operands (list of registers)
// - An optional output (a register)
class Instr {
  constructor(opcode, immediates, operands, output) {
    this.opcode = opcode;
    this.immediates = immediates;
    this.operands = operands;
    this.output = output;
  }
}

// A basic block contains a list of Instrs
class Block {
  constructor(id) {
    this.id = id;
    this.instrs = [];
    this.isEntryBlock = false;
    this.isExitBlock = false;
  }
}

// An edge connects one block to another
class Edge {
  constructor(srcBlockId, dstBlockId) {
    this.srcBlockId = srcBlockId;
    this.dstBlockId = dstBlockId;
  }
}

// A CFG consists of a list of basic blocks and the edges that connect them
class CFG {
  constructor(funcName, blocks, edges) {
    this.funcName = funcName;
    this.blocks = blocks;
    this.edges = edges;
  }
}

export { Block, CFG, Edge, Instr, Register };
