# Portions copyright (c) Meta Platforms, Inc. and affiliates. (http://www.meta.com)

from __future__ import annotations

from .optimizer import safe_lshift, safe_mod, safe_multiply, safe_power

TYPE_CHECKING = False
if TYPE_CHECKING:
    from typing import Dict, Optional, Tuple

    from .pyassem import Block, Instruction, PyFlowGraph


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


class FlowGraphOptimizer:
    def __init__(
        self,
        graph: PyFlowGraph,
    ) -> None:
        self.graph = graph

    def optimize_basic_block(self, block: Block) -> None:
        instr_index = 0

        while instr_index < len(block.insts):
            instr = block.insts[instr_index]

            target_instr: Instruction | None = None
            if instr.is_jump(self.graph.opcode):
                target = instr.target
                assert target is not None
                # Skip over empty basic blocks.
                while len(target.insts) == 0:
                    instr.target = target.next
                    target = instr.target
                    assert target is not None
                target_instr = target.insts[0]

            next_instr = (
                block.insts[instr_index + 1]
                if instr_index + 1 < len(block.insts)
                else None
            )

            new_index = self.dispatch_instr(
                instr_index, instr, next_instr, target_instr, block
            )
            instr_index = instr_index + 1 if new_index is None else new_index

    def dispatch_instr(
        self,
        instr_index: int,
        instr: Instruction,
        next_instr: Instruction | None,
        target: Instruction | None,
        block: Block,
    ) -> Optional[int]:
        if instr.opname == "JUMP_IF_FALSE_OR_POP":
            return self.opt_jump_if_false_or_pop(
                instr_index, instr, next_instr, target, block
            )
        elif instr.opname == "JUMP_IF_TRUE_OR_POP":
            return self.opt_jump_if_true_or_pop(
                instr_index, instr, next_instr, target, block
            )
        elif instr.opname in ("POP_JUMP_IF_TRUE", "POP_JUMP_IF_FALSE"):
            return self.opt_pop_jump_if(instr_index, instr, next_instr, target, block)
        elif instr.opname in ("JUMP_ABSOLUTE", "JUMP_FORWARD"):
            return self.opt_jump(instr_index, instr, next_instr, target, block)
        elif instr.opname == "FOR_ITER":
            return self.opt_for_iter(instr_index, instr, next_instr, target, block)
        elif instr.opname == "ROT_N":
            return self.opt_rot_n(instr_index, instr, next_instr, target, block)
        elif instr.opname == "LOAD_CONST":
            return self.opt_load_const(instr_index, instr, next_instr, target, block)
        elif instr.opname == "BUILD_TUPLE":
            return self.opt_build_tuple(instr_index, instr, next_instr, target, block)
        elif instr.opname == "RETURN_VALUE":
            return self.opt_return_value(instr_index, instr, next_instr, target, block)

    def clean_basic_block(self, block: Block, prev_lineno: int) -> None:
        """Remove all NOPs from a function when legal."""
        new_instrs = []
        num_instrs = len(block.insts)
        for idx in range(num_instrs):
            instr = block.insts[idx]
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
            prev_lineno = instr.lineno
        block.insts = new_instrs

    def jump_thread(self, instr: Instruction, target: Instruction, opname: str) -> int:
        """Attempt to eliminate jumps to jumps by updating inst to jump to
        target->i_target using the provided opcode. Return 0 if successful, 1 if
        not; this makes it easier for our callers to revisit the same
        instruction again only if we changed it."""
        assert instr.is_jump(self.graph.opcode)
        assert target.is_jump(self.graph.opcode)
        if instr.lineno == target.lineno and instr.target != target.target:
            instr.target = target.target
            instr.opname = opname
            return 0
        return 1

    def opt_jump_if_false_or_pop(
        self,
        instr_index: int,
        instr: Instruction,
        next_instr: Instruction | None,
        target: Instruction | None,
        block: Block,
    ) -> Optional[int]:
        assert target is not None
        if target.opname == "POP_JUMP_IF_FALSE":
            return instr_index + self.jump_thread(instr, target, "POP_JUMP_IF_FALSE")
        elif target.opname in ("JUMP_ABSOLUTE", "JUMP_FORWARD", "JUMP_IF_FALSE_OR_POP"):
            return instr_index + self.jump_thread(instr, target, "JUMP_IF_FALSE_OR_POP")
        elif target.opname in ("JUMP_IF_TRUE_OR_POP", "POP_JUMP_IF_TRUE"):
            if instr.lineno == target.lineno:
                target_block = instr.target
                assert target_block and target_block != target_block.next
                instr.opname = "POP_JUMP_IF_FALSE"
                instr.target = target_block.next
                return instr_index
            return instr_index + 1

    def opt_jump_if_true_or_pop(
        self,
        instr_index: int,
        instr: Instruction,
        next_instr: Instruction | None,
        target: Instruction | None,
        block: Block,
    ) -> Optional[int]:
        assert target is not None
        if target.opname == "POP_JUMP_IF_TRUE":
            return instr_index + self.jump_thread(instr, target, "POP_JUMP_IF_TRUE")
        elif target.opname in ("JUMP_ABSOLUTE", "JUMP_FORWARD", "JUMP_IF_TRUE_OR_POP"):
            return instr_index + self.jump_thread(instr, target, "JUMP_IF_TRUE_OR_POP")
        elif target.opname in ("JUMP_IF_FALSE_OR_POP", "POP_JUMP_IF_FALSE"):
            if instr.lineno == target.lineno:
                target_block = instr.target
                assert target_block and target_block != target_block.next
                instr.opname = "POP_JUMP_IF_TRUE"
                instr.target = target_block.next
                return instr_index
            return instr_index + 1

    def opt_pop_jump_if(
        self,
        instr_index: int,
        instr: Instruction,
        next_instr: Instruction | None,
        target: Instruction | None,
        block: Block,
    ) -> Optional[int]:
        assert target is not None
        if target.opname in ("JUMP_ABSOLUTE", "JUMP_FORWARD"):
            return instr_index + self.jump_thread(instr, target, instr.opname)

    def opt_jump(
        self,
        instr_index: int,
        instr: Instruction,
        next_instr: Instruction | None,
        target: Instruction | None,
        block: Block,
    ) -> Optional[int]:
        assert target is not None
        if target.opname in ("JUMP_ABSOLUTE", "JUMP_FORWARD"):
            return instr_index + self.jump_thread(instr, target, "JUMP_ABSOLUTE")

    def opt_for_iter(
        self,
        instr_index: int,
        instr: Instruction,
        next_instr: Instruction | None,
        target: Instruction | None,
        block: Block,
    ) -> Optional[int]:
        assert target is not None
        if target.opname == "JUMP_FORWARD":
            return instr_index + self.jump_thread(instr, target, "FOR_ITER")

    def opt_rot_n(
        self,
        instr_index: int,
        instr: Instruction,
        next_instr: Instruction | None,
        target: Instruction | None,
        block: Block,
    ) -> Optional[int]:
        if instr.ioparg < 2:
            pass
            instr.opname = "NOP"
            return
        elif instr.ioparg == 2:
            instr.opname = "ROT_TWO"
        elif instr.ioparg == 3:
            instr.opname = "ROT_THREE"
        elif instr.ioparg == 4:
            instr.opname = "ROT_FOUR"
        if instr_index >= instr.ioparg - 1:
            self.fold_rotations(
                block.insts[instr_index - instr.ioparg + 1 : instr_index + 1],
                instr.ioparg,
            )

    def fold_rotations(self, instrs: list[Instruction], n: int) -> None:
        for instr in instrs:
            if instr.opname == "ROT_N":
                rot = instr.ioparg
            elif instr.opname == "ROT_FOUR":
                rot = 4
            elif instr.opname == "ROT_THREE":
                rot = 3
            elif instr.opname == "ROT_TWO":
                rot = 2
            else:
                return
            if rot != n:
                return
        for instr in instrs:
            instr.opname = "NOP"

    def opt_load_const(
        self,
        instr_index: int,
        instr: Instruction,
        next_instr: Instruction | None,
        target: Instruction | None,
        block: Block,
    ) -> Optional[int]:
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
                block.no_fallthrough = True
            else:
                next_instr.opname = "NOP"
                next_instr.target = None
        elif next_instr.opname in ("JUMP_IF_FALSE_OR_POP", "JUMP_IF_TRUE_OR_POP"):
            is_true = bool(const)
            jump_if_true = next_instr.opname == "JUMP_IF_TRUE_OR_POP"
            if is_true == jump_if_true:
                next_instr.opname = "JUMP_ABSOLUTE"
                block.no_fallthrough = True
            else:
                instr.opname = "NOP"
                next_instr.opname = "NOP"
                next_instr.target = None

    def opt_build_tuple(
        self,
        instr_index: int,
        instr: Instruction,
        next_instr: Instruction | None,
        target: Instruction | None,
        block: Block,
    ) -> Optional[int]:
        if (
            next_instr
            and next_instr.opname == "UNPACK_SEQUENCE"
            and instr.ioparg == next_instr.ioparg
        ):
            if instr.ioparg == 1:
                instr.opname = "NOP"
                next_instr.opname = "NOP"
            elif instr.ioparg == 2:
                instr.opname = "ROT_TWO"
                next_instr.opname = "NOP"
            elif instr.ioparg == 3:
                instr.opname = "ROT_THREE"
                next_instr.opname = "ROT_TWO"
            return
        if instr_index >= instr.ioparg:
            self.fold_tuple_on_constants(instr_index, instr, block)

    def fold_tuple_on_constants(
        self, instr_index: int, instr: Instruction, block: Block
    ) -> None:
        load_const_instrs = []
        for i in range(instr_index - instr.ioparg, instr_index):
            maybe_load_const = block.insts[i]
            if maybe_load_const.opname != "LOAD_CONST":
                return
            load_const_instrs.append(maybe_load_const)
        newconst = tuple(lc.oparg for lc in load_const_instrs)
        for lc in load_const_instrs:
            lc.opname = "NOP"
        instr.opname = "LOAD_CONST"
        instr.oparg = newconst
        instr.ioparg = self.graph.convertArg("LOAD_CONST", newconst)

    def opt_return_value(
        self,
        instr_index: int,
        instr: Instruction,
        next_instr: Instruction | None,
        target: Instruction | None,
        block: Block,
    ) -> Optional[int]:
        block.insts = block.insts[: instr_index + 1]
