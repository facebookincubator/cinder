import dis
import types
from compiler import compile, opcode_static as opcodes
from math import inf
from typing import List, Tuple, Union

from cfgutil import Block, BlockMap, BytecodeOp

CODEUNIT_SIZE = 2


class VerificationError(Exception):
    def __init__(self, reason: str, bytecode_op: BytecodeOp = None):
        super().__init__(reason, bytecode_op)
        self.reason = reason
        self.bytecode_op = bytecode_op

    def __str__(self):
        if self.bytecode_op is not None:
            return f"{self.reason} for operation {self.bytecode_op.name} @ offset {self.bytecode_op.idx * CODEUNIT_SIZE}"
        else:
            return f"{self.reason}"


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
        Verifier.check_length(bytecode)
        bytecode_list = Verifier.parse_bytecode(bytecode)
        Verifier.check_opargs(source, bytecode_list)
        block_map = Verifier.create_blocks(bytecode_list)
        Verifier.add_successors(block_map)
        Verifier.check_stack_depth(source, block_map.idx_to_block[0])

    @staticmethod
    def check_length(bytecode: bytes) -> None:
        # length cannot be zero or odd
        if len(bytecode) <= 0:
            raise VerificationError("Bytecode length cannot be zero or negative")
        if len(bytecode) % CODEUNIT_SIZE != 0:
            raise VerificationError("Bytecode length cannot be odd")

    @staticmethod
    def parse_bytecode(bytecode: bytes) -> List[BytecodeOp]:
        # Changing from bytecode into a more convenient data structure
        num_instrs = len(bytecode) // CODEUNIT_SIZE
        result = [None] * num_instrs
        i, idx = 0, 0
        while i < len(bytecode):
            op = bytecode[i]
            try:
                name = dis.opname[op]
            except IndexError:
                raise VerificationError(f"Operation {op} at offset {i} out of bounds")
            if name[0] == "<":
                # if the opcode doesn't bind to a legitimate instruction, it will just be the number inside "<>"
                raise VerificationError(f"Operation {op} at offset {i} does not exist")
            result[idx] = BytecodeOp(op, bytecode[i + 1], idx, name)
            if result[idx].is_branch():
                if result[idx].jump_target_idx() >= num_instrs:
                    raise VerificationError(
                        f"Operation {name} can not jump out of bounds"
                    )
            i += CODEUNIT_SIZE
            idx += 1
        return result

    @staticmethod
    def create_blocks(instrs: List[BytecodeOp]) -> BlockMap:
        # This function creates the CFG by determining an ordering for each block of bytecode
        # Through analyzing the order in which they can be executed (via branches, returns, raises, and fall throughs)
        # View https://bernsteinbear.com/blog/discovering-basic-blocks/ for code source and more information
        # Note that the blog post uses 3.8 semantics while this code uses 3.10
        block_starts = set([0])
        num_instrs = len(instrs)
        for instr in instrs:
            if instr.is_branch():
                block_starts.add(instr.next_instr_idx())
                block_starts.add(instr.jump_target_idx())
            elif instr.is_return() or instr.is_raise():
                next_instr_idx = instr.next_instr_idx()
                if next_instr_idx < num_instrs:
                    block_starts.add(next_instr_idx)
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
            if (
                last_instr.next_instr_idx() in block_map.idx_to_block
                and not last_instr.is_uncond_transfer()
            ):
                i.fall_through = block_map.idx_to_block[last_instr.next_instr_idx()]

    @staticmethod
    def assert_depth_within_bounds(
        depth: int, min_: int = 0, max_: int = inf, op: BytecodeOp = None
    ):
        if not min_ <= depth:
            raise VerificationError(
                f"Stack depth {depth} dips below minimum of {min_}", op
            )
        if not max_ >= depth:
            raise VerificationError(
                f"Stack depth {depth} exceeds maximum of {max_}", op
            )

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
            for op in block.bytecode:
                Verifier.assert_depth_within_bounds(depth, 0, max_depth, op)
                new_depth = depth + Verifier.get_stack_effect(source, op, False)
                if op.is_branch():
                    target_depth = depth + Verifier.get_stack_effect(source, op, True)
                    Verifier.assert_depth_within_bounds(target_depth, 0, max_depth, op)
                    Verifier.push_block(worklist, block.jump_to, target_depth)
                depth = new_depth
                Verifier.assert_depth_within_bounds(depth, 0, max_depth, op)
            if block.fall_through:
                Verifier.push_block(worklist, block.fall_through, depth)

    @staticmethod
    def get_stack_effect(source: types.CodeType, op: BytecodeOp, jump: bool) -> int:
        # returns the stack effect for a particular operation
        effect = opcodes.opcode.stack_effects.get(op.name)
        if isinstance(effect, int):
            return effect
        else:
            # if real oparg is stored in one of the code object's tuples, use that instead
            oparg_location = Verifier.resolve_oparg_location(source, op.op)
            if oparg_location is not None:
                return effect(oparg_location[op.arg], jump)
            return effect(op.arg, jump)

    @staticmethod
    def check_opargs(source: types.CodeType, ops: List[BytecodeOp]) -> None:
        for op in ops:
            oparg_location = Verifier.resolve_oparg_location(source, op.op)
            if oparg_location is not None:
                Verifier.check_oparg_location(oparg_location, op)

    @staticmethod
    def resolve_oparg_location(source: types.CodeType, op: int) -> Union[List, Tuple]:
        if op in Verifier.INSTRS_WITH_OPARG_IN_CONSTS:
            return source.co_consts
        elif op in Verifier.INSTRS_WITH_OPARG_IN_VARNAMES:
            return source.co_varnames
        elif op in Verifier.INSTRS_WITH_OPARG_IN_NAMES:
            return source.co_names
        elif op in Verifier.DEREF_INSTRS:
            return [source.co_freevars, source.co_cellvars + source.co_freevars]
        elif op == opcodes.opcode.LOAD_CLOSURE:
            return source.co_cellvars + source.co_freevars
        elif op == opcodes.opcode.COMPARE_OP:
            return opcodes.opcode.CMP_OP
        return None

    @staticmethod
    def resolve_expected_oparg_type(op: int) -> type:
        if op == opcodes.opcode.LOAD_CONST:
            return object
        elif op == opcodes.opcode.PRIMITIVE_LOAD_CONST:
            return int
        elif op in Verifier.INSTRS_WITH_OPARG_TYPE_STRING:
            return str
        elif op in Verifier.INSTRS_WITH_OPARG_TYPE_TUPLE:
            return tuple
        return None

    @staticmethod
    def check_oparg_location(
        oparg_location: Union[List, Tuple], op: BytecodeOp
    ) -> None:
        if type(oparg_location) == tuple:
            Verifier.check_oparg_index_and_type(oparg_location, op)
        else:  # deref case which has to check both freevars and closure (cellvars + freevars)
            Verifier.check_oparg_index_and_type_deref_case(oparg_location, op)

    @staticmethod
    def check_oparg_index_and_type(oparg_location: tuple, op: BytecodeOp) -> None:
        expected_type = Verifier.resolve_expected_oparg_type(op.op)
        if not 0 <= op.arg < len(oparg_location):
            raise VerificationError(
                f"Argument index {op.arg} out of bounds for size {len(oparg_location)}",
                op,
            )
        if not isinstance(oparg_location[op.arg], expected_type):
            raise VerificationError(
                f"Incorrect oparg type of {type(oparg_location[op.arg]).__name__}, expected {expected_type.__name__}",
                op,
            )

    @staticmethod
    def check_oparg_index_and_type_deref_case(
        oparg_locations: list, op: BytecodeOp
    ) -> None:
        expected_type = Verifier.resolve_expected_oparg_type(op.op)
        freevars = oparg_locations[0]
        closure = oparg_locations[1]
        if not 0 <= op.arg < len(freevars) and not 0 <= op.arg < len(closure):
            raise VerificationError(
                f"Argument index {op.arg} out of bounds for size {len(closure)}", op
            )
        if not (
            0 <= op.arg < len(freevars) and isinstance(freevars[op.arg], expected_type)
        ) and not (
            0 <= op.arg < len(closure) and isinstance(closure[op.arg], expected_type)
        ):
            raise VerificationError(
                f"Incorrect oparg type, expected {expected_type.__name__}", op
            )

    INSTRS_WITH_OPARG_IN_CONSTS = {
        opcodes.opcode.LOAD_CONST,
        opcodes.opcode.LOAD_CLASS,
        opcodes.opcode.INVOKE_FUNCTION,
        opcodes.opcode.INVOKE_METHOD,
        opcodes.opcode.LOAD_FIELD,
        opcodes.opcode.STORE_FIELD,
        opcodes.opcode.CAST,
        opcodes.opcode.PRIMITIVE_BOX,
        opcodes.opcode.PRIMITIVE_UNBOX,
        opcodes.opcode.TP_ALLOC,
        opcodes.opcode.CHECK_ARGS,
        opcodes.opcode.BUILD_CHECKED_MAP,
        opcodes.opcode.BUILD_CHECKED_LIST,
        opcodes.opcode.PRIMITIVE_LOAD_CONST,
        opcodes.opcode.LOAD_LOCAL,
        opcodes.opcode.STORE_LOCAL,
        opcodes.opcode.REFINE_TYPE,
        opcodes.opcode.LOAD_METHOD_SUPER,
        opcodes.opcode.LOAD_ATTR_SUPER,
    }

    INSTRS_WITH_OPARG_IN_VARNAMES = {
        opcodes.opcode.LOAD_FAST,
        opcodes.opcode.STORE_FAST,
        opcodes.opcode.DELETE_FAST,
    }

    INSTRS_WITH_OPARG_IN_NAMES = {
        opcodes.opcode.LOAD_NAME,
        opcodes.opcode.LOAD_GLOBAL,
        opcodes.opcode.STORE_GLOBAL,
        opcodes.opcode.DELETE_GLOBAL,
        opcodes.opcode.STORE_NAME,
        opcodes.opcode.DELETE_NAME,
        opcodes.opcode.IMPORT_NAME,
        opcodes.opcode.IMPORT_FROM,
        opcodes.opcode.STORE_ATTR,
        opcodes.opcode.LOAD_ATTR,
        opcodes.opcode.DELETE_ATTR,
        opcodes.opcode.LOAD_METHOD,
    }

    DEREF_INSTRS = {
        opcodes.opcode.LOAD_DEREF,
        opcodes.opcode.STORE_DEREF,
        opcodes.opcode.DELETE_DEREF,
        opcodes.opcode.LOAD_CLASSDEREF,
    }

    INSTRS_WITH_OPARG_TYPE_STRING = {
        opcodes.opcode.LOAD_FAST,
        opcodes.opcode.STORE_FAST,
        opcodes.opcode.DELETE_FAST,
        opcodes.opcode.LOAD_NAME,
        opcodes.opcode.LOAD_CLOSURE,
        opcodes.opcode.COMPARE_OP,
        opcodes.opcode.LOAD_GLOBAL,
        opcodes.opcode.STORE_GLOBAL,
        opcodes.opcode.DELETE_GLOBAL,
        opcodes.opcode.STORE_NAME,
        opcodes.opcode.DELETE_NAME,
        opcodes.opcode.IMPORT_NAME,
        opcodes.opcode.IMPORT_FROM,
        opcodes.opcode.STORE_ATTR,
        opcodes.opcode.LOAD_ATTR,
        opcodes.opcode.DELETE_ATTR,
        opcodes.opcode.LOAD_METHOD,
        opcodes.opcode.LOAD_DEREF,
        opcodes.opcode.STORE_DEREF,
        opcodes.opcode.DELETE_DEREF,
        opcodes.opcode.LOAD_CLASSDEREF,
    }

    INSTRS_WITH_OPARG_TYPE_TUPLE = {
        opcodes.opcode.INVOKE_FUNCTION,
        opcodes.opcode.INVOKE_METHOD,
        opcodes.opcode.BUILD_CHECKED_MAP,
        opcodes.opcode.BUILD_CHECKED_LIST,
        opcodes.opcode.LOAD_LOCAL,
        opcodes.opcode.STORE_LOCAL,
        opcodes.opcode.LOAD_METHOD_SUPER,
        opcodes.opcode.LOAD_ATTR_SUPER,
        opcodes.opcode.TP_ALLOC,
        opcodes.opcode.CHECK_ARGS,
        opcodes.opcode.PRIMITIVE_BOX,
        opcodes.opcode.PRIMITIVE_UNBOX,
        opcodes.opcode.LOAD_CLASS,
        opcodes.opcode.LOAD_FIELD,
        opcodes.opcode.STORE_FIELD,
        opcodes.opcode.CAST,
        opcodes.opcode.REFINE_TYPE,
    }
