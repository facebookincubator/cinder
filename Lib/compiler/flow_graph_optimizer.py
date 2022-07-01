# Portions copyright (c) Facebook, Inc. and its affiliates. (http://www.facebook.com)
# pyre-unsafe
from __future__ import annotations

from .optimizer import safe_lshift, safe_mod, safe_multiply, safe_power

TYPE_CHECKING = False
if TYPE_CHECKING:
    from typing import Callable, Dict, Optional

    from .pyassem import Block

    TOpcodeHandler = Callable[
        ["FlowGraphOptimizer", int, int, int, int, Block], Optional[int]
    ]


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

    def optimizeBlock(self, block: Block) -> None:
        instr_index = 0

        while instr_index < len(block.insts):
            instr = block.insts[instr_index]

            target: Optional[Block] = None
            if instr.is_jump():
                # Skip over empty basic blocks.
                if len(instr.target.insts) == 0:
                    instr.target = instr.target.next
                target = instr.target

            next_instr = (
                block.insts[instr_index + 1]
                if instr_index + 1 < len(block.insts)
                else None
            )

            handler = self.OP_HANDLERS.get(instr.opname)
            if handler is not None:
                instr_index = (
                    handler(self, instr_index, instr, next_instr, target, block)
                    or instr_index + 1
                )
            else:
                instr_index += 1

    def cleanBlock(self, block: Block, prev_lineno: int) -> None:
        """Remove all NOPs from a function when legal."""
        new_instrs = []
        num_instrs = len(block.insts)
        for idx in range(num_instrs):
            instr = block.insts[idx]
            try:
                if instr.opname == "NOP":
                    lineno = instr.lineno
                    # Eliminate no-op if it doesn't have a line number
                    if lineno < 0:
                        continue
                    # or, if the previous instruction had the same line number.
                    if prev_lineno == lineno:
                        continue
                    # or, if the next instruction has same line number or no line number
                    if idx < num_instrs - 1:
                        next_instr = block.insts[idx + 1]
                        next_lineno = next_instr.lineno
                        if next_lineno < 0 or next_lineno == lineno:
                            next_instr.lineno = lineno
                            continue
                    else:
                        next_block = block.next
                        while next_block and len(next_block.insts) == 0:
                            next_block = next_block.next
                        # or if last instruction in BB and next BB has same line number
                        if next_block:
                            if lineno == next_block.insts[0].lineno:
                                continue
                new_instrs.append(instr)
            finally:
                prev_lineno = instr.lineno
        block.insts = new_instrs

    OP_HANDLERS, ophandler = ophandler_registry()

    @ophandler("LOAD_CONST")
    def opt_load_const(self, instr_index, instr, next_instr, target, block):
        # Remove LOAD_CONST const; conditional jump
        const = instr.oparg
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

    @ophandler("RETURN_VALUE", "RETURN_PRIMITIVE")
    def opt_return_value(self, instr_index, instr, next_instr, target, block):
        block.insts = block.insts[: instr_index + 1]

    def extendBlock(self, block: Block) -> None:
        if len(block.insts) == 0:
            return
        last = block.insts[-1]
        if last.opname not in ("JUMP_ABSOLUTE", "JUMP_FORWARD"):
            return
        target = last.target
        if not target.is_exit:
            return
        if len(target.insts) > MAX_COPY_SIZE:
            return
        block.insts[-1] = target.insts[0]
        for instr in target.insts[1:]:
            block.insts.append(instr)

    def normalizeBlock(self, block: Block) -> None:
        """Sets the `fallthrough` and `exit` properties of a block, and ensures that the targets of
        any jumps point to non-empty blocks by following the next pointer of empty blocks."""
        for instr in block.getInstructions():
            if instr.opname in ("RETURN_VALUE", "RAISE_VARARGS", "RERAISE"):
                block.is_exit = True
                block.no_fallthrough = True
            elif instr.opname in ("JUMP_ABSOLUTE", "JUMP_FORWARD"):
                block.no_fallthrough = True
            elif instr.opname in (
                "POP_JUMP_IF_TRUE",
                "POP_JUMP_IF_FALSE",
                "JUMP_IF_TRUE_OR_POP",
                "JUMP_IF_FALSE_OR_POP",
                "FOR_ITER",
            ):
                while not instr.target.insts:
                    instr.target = instr.target.next
