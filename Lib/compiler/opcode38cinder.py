from .opcode38 import opcode as opcode38
from .opcodebase import Opcode

opcode: Opcode = opcode38.copy()
opcode.def_op("LOAD_METHOD_SUPER", 198)
opcode.hasconst.add(198)
opcode.def_op("LOAD_ATTR_SUPER", 199)
opcode.hasconst.add(199)

opcode.stack_effects["LOAD_METHOD_SUPER"] = -1
opcode.stack_effects["LOAD_ATTR_SUPER"] = -2
