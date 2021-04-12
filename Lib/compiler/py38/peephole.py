# pyre-unsafe
from __future__ import annotations

from ..peephole import Optimizer, ophandler_registry, UNARY_OPS, BINARY_OPS

TYPE_CHECKING = False
if TYPE_CHECKING:
    from typing import Optional


class Optimizer38(Optimizer):
    OP_HANDLERS, ophandler = ophandler_registry(Optimizer.OP_HANDLERS)
    del OP_HANDLERS["FOR_ITER"]

    # pyre-ignore[56]: decorator factory type is unknown
    @ophandler(*UNARY_OPS)
    def op_unary_constants(
        self, instr_index: int, opcode: int, op_start: int, nextop: int, nexti: int
    ) -> Optional[int]:
        return

    # pyre-ignore[56]: decorator factory type is unknown
    @ophandler(*BINARY_OPS)
    def op_binary_constants(
        self, instr_index: int, opcode: int, op_start: int, nextop: int, nexti: int
    ) -> Optional[int]:
        return

    # pyre-ignore[56]: decorator factory type is unknown
    @ophandler("RETURN_VALUE")
    def opt_return_value(
        self, instr_index: int, opcode: int, op_start: int, nextop: int, nexti: int
    ) -> Optional[int]:
        block_end = instr_index + 1
        block_id = self.blocks[instr_index]
        while (
            # checks that we are still in the same basic block
            block_end < len(self.codestr) // self.CODEUNIT_SIZE
            and self.blocks[block_end] == block_id
            and self.codestr[block_end * self.CODEUNIT_SIZE] != self.opcode.END_FINALLY
        ):
            if (
                self.codestr[block_end * self.CODEUNIT_SIZE]
                == self.opcode.SETUP_FINALLY
            ):
                while (
                    block_end > instr_index + 1
                    and self.codestr[(block_end - 1) * self.CODEUNIT_SIZE]
                    == self.EXTENDED_ARG
                ):
                    block_end -= 1
                break
            block_end += 1
        if block_end > instr_index + 1:
            self.fill_nops(instr_index + 1, block_end)

    op_fold_jumps_to_uncond_jumps = ophandler(
        "POP_JUMP_IF_FALSE",
        "POP_JUMP_IF_TRUE",
        "JUMP_FORWARD",
        "JUMP_ABSOLUTE",
    )(Optimizer.op_fold_jumps_to_uncond_jumps)
