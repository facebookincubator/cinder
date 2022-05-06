# Portions copyright (c) Facebook, Inc. and its affiliates. (http://www.facebook.com)
# pyre-unsafe
from __future__ import annotations

from .optimizer import safe_power, safe_multiply, safe_mod, safe_lshift

TYPE_CHECKING = False
if TYPE_CHECKING:
    from typing import Callable, Dict, Optional

    from .pyassem import Block

    TOpcodeHandler = Callable[["FlowGraphOptimizer", int, int, int, int], Optional[int]]


PyCmp_LT = 0
PyCmp_LE = 1
PyCmp_EQ = 2
PyCmp_NE = 3
PyCmp_GT = 4
PyCmp_GE = 5
PyCmp_IN = 6
PyCmp_NOT_IN = 7
PyCmp_IS = 8
PyCmp_IS_NOT = 9
PyCmp_EXC_MATCH = 10

MAX_COPY_SIZE = 4
UNARY_OPS: Dict[str, object] = {
    "UNARY_INVERT": lambda v: ~v,
    "UNARY_NEGATIVE": lambda v: -v,
    "UNARY_POSITIVE": lambda v: +v,
}


BINARY_OPS: Dict[str, object] = {
    "BINARY_POWER": safe_power,
    "BINARY_MULTIPLY": safe_multiply,
    "BINARY_TRUE_DIVIDE": lambda left, right: left / right,
    "BINARY_FLOOR_DIVIDE": lambda left, right: left // right,
    "BINARY_MODULO": safe_mod,
    "BINARY_ADD": lambda left, right: left + right,
    "BINARY_SUBTRACT": lambda left, right: left - right,
    "BINARY_SUBSCR": lambda left, right: left[right],
    "BINARY_LSHIFT": safe_lshift,
    "BINARY_RSHIFT": lambda left, right: left >> right,
    "BINARY_AND": lambda left, right: left & right,
    "BINARY_XOR": lambda left, right: left ^ right,
    "BINARY_OR": lambda left, right: left | right,
}


def ophandler_registry(existing: Optional[Dict[str, TOpcodeHandler]] = None):
    registry = {} if existing is None else dict(existing)

    def register(*opcodes):
        def handler(f: TOpcodeHandler):
            for opcode in opcodes:
                registry[opcode] = f
            return f

        return handler

    return registry, register


class FlowGraphOptimizer:
    def __init__(
        self,
        consts,
    ) -> None:
        self.consts = consts
        self.const_stack = []
        self.in_consts = False

    def push_const(self, const) -> None:
        self.const_stack.append(const)
        self.in_consts = True

    def optimizeBlock(self, block: Block) -> None:
        instr_index = 0
        num_operations = len(block.insts)

        while instr_index < num_operations:
            instr = block.insts[instr_index]

            target: Optional[Block] = None
            if instr.is_jump():
                # Skip over empty basic blocks.
                if len(instr.target.insts) == 0:
                    instr.target = instr.target.next
                target = instr.target

            next_instr = (
                block.insts[instr_index + 1]
                if instr_index + 1 < num_operations
                else None
            )

            if not self.in_consts:
                del self.const_stack[:]
            self.in_consts = False

            handler = self.OP_HANDLERS.get(instr.opname)
            if handler is not None:
                instr_index = (
                    handler(self, instr_index, instr, next_instr, target)
                    or instr_index + 1
                )
            else:
                instr_index += 1

    def cleanBlock(self, block: Block) -> None:
        """Remove all NOPs from a function when legal."""
        prev_instr = None
        new_instrs = []
        for instr in block.insts:
            if instr.opname != "NOP":
                new_instrs.append(instr)
        block.insts = new_instrs

    OP_HANDLERS, ophandler = ophandler_registry()

    @ophandler("LOAD_CONST")
    def opt_load_const(self, instr_index, instr, next_instr, target):
        # Skip over LOAD_CONST trueconst
        # POP_JUMP_IF_FALSE xx.  This improves
        # "while 1" performance.
        # The above comment is from CPython.  This optimization is now performed
        # at the AST level and is also applied to if statements.  But it does
        # not get applied to conditionals, e.g. 1 if 2 else 3
        const = instr.oparg
        self.push_const(const)
        if next_instr is None:
            return
        if next_instr.opname in (
            "POP_JUMP_IF_FALSE",
            "POP_JUMP_IF_TRUE",
        ):
            is_true = bool(const)
            instr.opname = "NOP"
            jump_if_true = next_instr.opname == "POP_JUMP_IF_TRUE"
            if is_true == jump_if_true:
                next_instr.opname = "JUMP_ABSOLUTE"
            else:
                next_instr.opname = "NOP"
        elif next_instr.opname in ("JUMP_IF_FALSE_OR_POP", "JUMP_IF_TRUE_OR_POP"):
            is_true = bool(const)
            jump_if_true = next_instr.opname == "JUMP_IF_TRUE_OR_POP"
            if is_true == jump_if_true:
                next_instr.opname = "JUMP_ABSOLUTE"
            else:
                instr.opname = "NOP"
                next_instr.opname = "NOP"

    def extendBlock(self, block: Block) -> None:
        if len(block.insts) == 0:
            return
        last = block.insts[-1]
        if last.opname not in ("JUMP_ABSOLUTE", "JUMP_FORWARD"):
            return
        target = last.target
        if not target.is_exit_block():
            return
        if len(target.insts) > MAX_COPY_SIZE:
            return
        block.insts[-1] = target.insts[0]
        for instr in target.insts[1:]:
            block.insts.append(instr)
