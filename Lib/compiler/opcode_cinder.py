from typing import Tuple

from .opcodebase import Opcode
from .opcodes import opcode as base_opcode


opcode: Opcode = base_opcode.copy()
opcode.def_op("FUNC_CREDENTIAL", 153)
opcode.hasconst.add(153)
opcode.def_op("READONLY_OPERATION", 158)
opcode.hasconst.add(158)
opcode.readonly_op("MAKE_FUNCTION", 0)
opcode.readonly_op("CHECK_FUNCTION", 1)
opcode.readonly_op("BINARY_ADD", 2)
opcode.readonly_op("BINARY_SUBTRACT", 3)
opcode.readonly_op("BINARY_MULTIPLY", 4)
opcode.readonly_op("BINARY_MATRIX_MULTIPLY", 5)
opcode.readonly_op("BINARY_TRUE_DIVIDE", 6)
opcode.readonly_op("BINARY_FLOOR_DIVIDE", 7)
opcode.readonly_op("BINARY_MODULO", 8)
opcode.readonly_op("BINARY_POWER", 9)
opcode.readonly_op("BINARY_LSHIFT", 10)
opcode.readonly_op("BINARY_RSHIFT", 11)
opcode.readonly_op("BINARY_OR", 12)
opcode.readonly_op("BINARY_XOR", 13)
opcode.readonly_op("BINARY_AND", 14)
opcode.readonly_op("UNARY_INVERT", 15)
opcode.readonly_op("UNARY_NEGATIVE", 16)
opcode.readonly_op("UNARY_POSITIVE", 17)
opcode.readonly_op("UNARY_NOT", 18)
opcode.readonly_op("COMPARE_OP", 19)
opcode.readonly_op("CHECK_LOAD_ATTR", 20)
opcode.readonly_op("CHECK_STORE_ATTR", 21)
opcode.readonly_op("GET_ITER", 22)
opcode.readonly_op("FOR_ITER", 23)
opcode.readonly_op("POP_JUMP_IF_TRUE", 24)
opcode.readonly_op("POP_JUMP_IF_FALSE", 25)
opcode.def_op("LOAD_METHOD_SUPER", 198)
opcode.hasconst.add(198)
opcode.def_op("LOAD_ATTR_SUPER", 199)
opcode.hasconst.add(199)


def _calculate_readonly_op_stack_effect(oparg: Tuple[int], jmp: int = 0) -> int:
    op = oparg[0]
    if op >= opcode.readonlyop["BINARY_ADD"] and op <= opcode.readonlyop["BINARY_AND"]:
        return -1
    if op == opcode.readonlyop["COMPARE_OP"]:
        return -1
    if op == opcode.readonlyop["FOR_ITER"]:
        return -1 if jmp > 0 else 1
    if (
        op == opcode.readonlyop["POP_JUMP_IF_TRUE"]
        or op == opcode.readonlyop["POP_JUMP_IF_FALSE"]
    ):
        return -1
    return 0


opcode.stack_effects.update(
    FUNC_CREDENTIAL=1,
    READONLY_OPERATION=_calculate_readonly_op_stack_effect,
    LOAD_METHOD_SUPER=-1,
    LOAD_ATTR_SUPER=-2,
)
