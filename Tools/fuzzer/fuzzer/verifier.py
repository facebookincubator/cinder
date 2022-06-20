import dis
import types
from compiler import compile, opcodes
from typing import List

from cfgutil import Block, BlockMap, BytecodeOp

CODEUNIT_SIZE = 2

class VerificationError(Exception):
    pass

class Verifier:
    @staticmethod
    def validate_code(source: types.CodeType) -> bool:
        stack = [source]
        while stack:
            module = stack.pop()
            Verifier.visit_code(module)
            for i in module.co_consts:
                if isinstance(i, types.CodeType):
                    stack.append(i)
        return True

    @staticmethod
    def visit_code(source: types.CodeType) -> None:
        bytecode = source.co_code
        Verifier.check_length_and_first_index(bytecode)
        bytecode_list = Verifier.parse_bytecode(bytecode)
        block_map = Verifier.create_blocks(bytecode_list)
        Verifier.add_successors(block_map)
        Verifier.check_stack_depth(source, block_map.idx_to_block[0])

    @staticmethod
    def check_length_and_first_index(bytecode: bytes) -> None:
        # length cannot be zero or odd, value at first index must be 0
        if len(bytecode) <= 0:
            raise VerificationError("Bytecode length cannot be zero or negative")
        if len(bytecode) % CODEUNIT_SIZE != 0:
            raise VerificationError("Bytecode length cannot be odd")
        if bytecode[1] != 0:
            raise VerificationError("Bytecode value at first index must be zero")

    @staticmethod
    def parse_bytecode(bytecode: bytes) -> List[BytecodeOp]:
        # Changing from bytecode into a more convenient data structure
        num_instrs = len(bytecode) // CODEUNIT_SIZE
        result = [None] * num_instrs
        i, idx = 0, 0
        while i < len(bytecode):
            try:
                name = dis.opname[bytecode[i]]
                if name[0] == "<":
                    # if the opcode doesn't bind to a legitimate instruction, it will just be the number inside "<>"
                    raise VerificationError("Operation does not exist")
                result[idx] = BytecodeOp(bytecode[i], bytecode[i+1], idx, name)
                if result[idx].is_branch() and (result[idx].jump_target_idx() >= num_instrs or result[idx].jump_target() % 2 != 0):
                    raise VerificationError("Can not jump out of bounds or to odd index")
                i += CODEUNIT_SIZE
                idx += 1
            except IndexError:
                # indexerror if op index does not exist in dis.opname
                raise VerificationError("Operation does not exist")
        return result

    @staticmethod
    def create_blocks(instrs: List[BytecodeOp]) -> BlockMap:
        # This function creates the CFG by determining an ordering for each block of bytecode
        # Through analyzing the order in which they can be executed (via branches, returns, raises, and fall throughs)
        # View https://bernsteinbear.com/blog/discovering-basic-blocks/ for code source and more information
        block_starts = set([0])
        num_instrs = len(instrs)
        for instr in instrs:
            if instr.is_branch():
                block_starts.add(instr.next_instr_idx())
                block_starts.add(instr.jump_target_idx())
            elif instr.is_return():
                next_instr_idx = instr.next_instr_idx()
                if next_instr_idx < num_instrs:
                    block_starts.add(next_instr_idx)
            elif instr.is_raise():
                block_starts.add(instr.next_instr_idx())
        num_blocks = len(block_starts)
        block_starts_ordered = sorted(block_starts)
        block_map = BlockMap()
        for i, start_idx in enumerate(block_starts_ordered):
            end_idx = block_starts_ordered[i + 1] if i + 1 < num_blocks else num_instrs
            block_instrs = instrs[start_idx:end_idx]
            block_map.add_block(start_idx, Block(i, block_instrs))
        return block_map

    @staticmethod
    def add_successors(block_map: BlockMap):
        # adding successors to each block prior to stack validation
        for i in block_map:
            last_instr = i.bytecode[-1]
            if last_instr.is_branch():
                i.jump_to = block_map.idx_to_block[last_instr.jump_target_idx()]
            if last_instr.next_instr_idx() in block_map.idx_to_block and not last_instr.is_uncond_transfer():
                i.fall_through = block_map.idx_to_block[last_instr.next_instr_idx()]

    @staticmethod
    def push_block(worklist: List[Block], block: Block, depth: int) -> bool:
        # push_block ensures that we only ever re-analyze a block if we visit it when the stack depth has increased
        # this way loops can be analyzed properly
        if not (block.start_depth < 0 or block.start_depth >= depth):
            return False
        if block.start_depth < depth:
            block.start_depth = depth
            worklist.append(block)
        return True

    @staticmethod
    def check_stack_depth(source: types.CodeType, start: Block) -> None:
        # makes sure the stack size never goes negative or above the limit
        max_depth = source.co_stacksize
        worklist = []
        Verifier.push_block(worklist, start, 0)
        while worklist:
            block = worklist.pop()
            depth = block.start_depth
            if depth < 0:
                raise VerificationError("Stack depth either dips below zero or goes above max size")
            for op in block.bytecode:
                if depth < 0 or depth > max_depth:
                    raise VerificationError("Stack depth either dips below zero or goes above max size")
                new_depth = depth + Verifier.get_stack_effect(op.name, op.arg, False)
                if op.is_branch():
                    target_depth = depth + Verifier.get_stack_effect(op.name, op.arg, True)
                    if target_depth < 0 or target_depth > max_depth:
                        raise VerificationError("Stack depth either dips below zero or goes above max size")
                    Verifier.push_block(worklist, block.jump_to, target_depth)
                depth = new_depth
                if depth < 0 or depth > max_depth:
                    raise VerificationError("Stack depth either dips below zero or goes above max size")
            if block.fall_through:
                Verifier.push_block(worklist, block.fall_through, depth)

    @staticmethod
    def get_stack_effect(opname: str, oparg: int, jump: bool) -> int:
        # returns the stack effect for a particular operation
        effect = opcodes.opcode.stack_effects.get(opname)
        if isinstance(effect, int):
            return effect
        else:
            return effect(oparg, jump)
