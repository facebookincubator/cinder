from .opcodebase import Opcode
from .opcodes import opcode as base_opcode


opcode: Opcode = base_opcode.copy()
opcode.def_op("LOAD_METHOD_SUPER", 198)
opcode.hasconst.add(198)
opcode.def_op("LOAD_ATTR_SUPER", 199)
opcode.hasconst.add(199)


opcode.stack_effects.update(
    LOAD_METHOD_SUPER=-1,
    LOAD_ATTR_SUPER=-2,
)
