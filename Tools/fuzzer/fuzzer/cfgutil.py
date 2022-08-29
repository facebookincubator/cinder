import dis
from compiler import opcode_static as opcodes
from typing import Dict, Iterator, List, Optional

CODEUNIT_SIZE = 2


class BytecodeOp:
    def __init__(self, op: int, arg: int, idx: int, name: str) -> None:
        self.op = op
        self.arg = arg
        self.idx = idx
        self.name = name

    def __repr__(self) -> None:
        return f"{self.name} : {self.arg}"

    def is_branch(self) -> bool:
        return self.op in {
            opcodes.opcode.FOR_ITER,
            opcodes.opcode.JUMP_ABSOLUTE,
            opcodes.opcode.JUMP_FORWARD,
            opcodes.opcode.JUMP_IF_FALSE_OR_POP,
            opcodes.opcode.JUMP_IF_TRUE_OR_POP,
            opcodes.opcode.POP_JUMP_IF_FALSE,
            opcodes.opcode.POP_JUMP_IF_TRUE,
        }

    def is_uncond_transfer(self) -> bool:
        return self.op in {
            opcodes.opcode.RETURN_VALUE,
            opcodes.opcode.RAISE_VARARGS,
            opcodes.opcode.RERAISE,
            opcodes.opcode.JUMP_ABSOLUTE,
            opcodes.opcode.JUMP_FORWARD,
        }

    def is_relative_branch(self) -> bool:
        return self.op in opcodes.opcode.hasjrel

    def is_return(self) -> bool:
        return self.op == opcodes.opcode.RETURN_VALUE

    def is_raise(self) -> bool:
        return (
            self.op == opcodes.opcode.RAISE_VARARGS or self.op == opcodes.opcode.RERAISE
        )

    def next_instr_idx(self) -> int:
        return self.idx + 1

    def next_instr_offset(self) -> int:
        return self.next_instr_idx() * CODEUNIT_SIZE

    def jump_target(self) -> int:
        return self.jump_target_idx() * CODEUNIT_SIZE

    def jump_target_idx(self) -> int:
        if self.is_relative_branch():
            return self.next_instr_idx() + self.arg
        return self.arg


class Block:
    def __init__(self, id: int, bytecodes: List[BytecodeOp]) -> None:
        self.id: int = id
        self.bytecode: List[BytecodeOp] = bytecodes
        self.start_depth: int = -1
        self.jump_to: Block = None
        self.fall_through: Block = None

    def __str__(self) -> str:
        return f"ID: {self.id}, Slice: {self.bytecode}, startDepth: {self.start_depth}"


class BlockMap:
    def __init__(self) -> None:
        self.idx_to_block: Dict[int, Block] = {}

    def add_block(self, idx: int, block: Block) -> None:
        self.idx_to_block[idx] = block

    def __str__(self) -> str:
        result = []
        for block in self.idx_to_block.values():
            result.append(f"bb{block.id}:")
            for instr in block.bytecode:
                if instr.is_branch():
                    target_idx = instr.jump_target_idx()
                    target = self.idx_to_block[target_idx]
                    result.append(f"  {(instr.name)} bb{target.id}")
                else:
                    result.append(f"  {instr}")
        return "\n".join(result)

    def size(self) -> int:
        return len(self.idx_to_block.keys())

    def __iter__(self) -> Iterator[Block]:
        return iter([i for i in self.idx_to_block.values()])
