# Copyright (c) Meta Platforms, Inc. and affiliates. (http://www.meta.com)

from .opcode_cinder import opcode as opcode_cinder
from .opcodebase import Opcode

opcode: Opcode = opcode_cinder.copy()
opcode.def_op("INVOKE_METHOD", 158)
opcode.hasconst.add(158)
opcode.def_op("LOAD_FIELD", 159)
opcode.hasconst.add(159)
opcode.def_op("STORE_FIELD", 166)
opcode.hasconst.add(166)
opcode.def_op("SEQUENCE_REPEAT", 167)
opcode.def_op("BUILD_CHECKED_LIST", 168)
opcode.hasconst.add(168)
opcode.def_op("LOAD_TYPE", 169)
opcode.hasconst.add(169)
opcode.def_op("CAST", 170)
opcode.hasconst.add(170)
opcode.def_op("LOAD_LOCAL", 171)
opcode.hasconst.add(171)
opcode.def_op("STORE_LOCAL", 172)
opcode.hasconst.add(172)
opcode.def_op("PRIMITIVE_BOX", 174)
opcode.jabs_op("POP_JUMP_IF_ZERO", 175)
opcode.jabs_op("POP_JUMP_IF_NONZERO", 176)
opcode.def_op("PRIMITIVE_UNBOX", 177)
opcode.def_op("PRIMITIVE_BINARY_OP", 178)
opcode.def_op("PRIMITIVE_UNARY_OP", 179)
opcode.def_op("PRIMITIVE_COMPARE_OP", 180)
opcode.def_op("LOAD_ITERABLE_ARG", 181)
opcode.def_op("LOAD_MAPPING_ARG", 182)
opcode.def_op("INVOKE_FUNCTION", 183)
opcode.hasconst.add(183)
opcode.jabs_op("JUMP_IF_ZERO_OR_POP", 184)
opcode.jabs_op("JUMP_IF_NONZERO_OR_POP", 185)
opcode.def_op("FAST_LEN", 186)
opcode.def_op("CONVERT_PRIMITIVE", 187)
opcode.def_op("CHECK_ARGS", 188)
opcode.hasconst.add(188)
opcode.def_op("INVOKE_NATIVE", 189)
opcode.hasconst.add(189)
opcode.def_op("LOAD_CLASS", 190)
opcode.hasconst.add(190)
opcode.def_op("BUILD_CHECKED_MAP", 191)
opcode.hasconst.add(191)
opcode.def_op("SEQUENCE_GET", 192)
opcode.def_op("SEQUENCE_SET", 193)
opcode.def_op("LIST_DEL", 194)
opcode.def_op("REFINE_TYPE", 195)
opcode.hasconst.add(195)
opcode.def_op("PRIMITIVE_LOAD_CONST", 196)
opcode.hasconst.add(196)
opcode.def_op("RETURN_PRIMITIVE", 197)
opcode.def_op("TP_ALLOC", 200)
opcode.hasconst.add(200)


def _load_mapping_arg_effect(oparg: int, _jmp: int = 0) -> int:
    if oparg == 2:
        return -1
    elif oparg == 3:
        return -2
    return 1


opcode.stack_effects.update(
    INVOKE_METHOD=lambda oparg, jmp: -oparg[1],
    LOAD_FIELD=0,
    STORE_FIELD=-2,
    SEQUENCE_REPEAT=-1,
    CAST=0,
    LOAD_LOCAL=1,
    STORE_LOCAL=-1,
    PRIMITIVE_BOX=0,
    POP_JUMP_IF_ZERO=-1,
    POP_JUMP_IF_NONZERO=-1,
    PRIMITIVE_UNBOX=0,
    PRIMITIVE_BINARY_OP=lambda oparg, jmp: -1,
    PRIMITIVE_UNARY_OP=lambda oparg, jmp: 0,
    PRIMITIVE_COMPARE_OP=lambda oparg, jmp: -1,
    LOAD_ITERABLE_ARG=1,
    LOAD_MAPPING_ARG=_load_mapping_arg_effect,
    INVOKE_FUNCTION=lambda oparg, jmp=0: (-oparg[1]) + 1,
    INVOKE_NATIVE=lambda oparg, jmp=0: (-len(oparg[1])) + 2,
    JUMP_IF_ZERO_OR_POP=lambda oparg, jmp=0: 0 if jmp else -1,
    JUMP_IF_NONZERO_OR_POP=lambda oparg, jmp=0: 0 if jmp else -1,
    FAST_LEN=0,
    CONVERT_PRIMITIVE=0,
    CHECK_ARGS=0,
    LOAD_CLASS=1,
    BUILD_CHECKED_MAP=lambda oparg, jmp: 1 - 2 * oparg[1],
    SEQUENCE_GET=-1,
    SEQUENCE_SET=-3,
    LIST_DEL=-2,
    REFINE_TYPE=0,
    PRIMITIVE_LOAD_CONST=1,
    RETURN_PRIMITIVE=-1,
    TP_ALLOC=1,
    BUILD_CHECKED_LIST=lambda oparg, jmp: 1 - oparg[1],
    LOAD_TYPE=0,
)
