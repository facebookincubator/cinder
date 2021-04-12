from .opcode37 import opcode as opcode37
from .opcodebase import Opcode

opcode: Opcode = opcode37.copy()
opcode.remove_op("BREAK_LOOP")
opcode.remove_op("CONTINUE_LOOP")
opcode.remove_op("SETUP_LOOP")
opcode.def_op("ROT_FOUR", 6)
opcode.def_op("END_ASYNC_FOR", 54)
opcode.def_op("BEGIN_FINALLY", 53)
opcode.jrel_op("CALL_FINALLY", 162)
opcode.def_op("POP_FINALLY", 163)

opcode.stack_effects.update(
    # New opcodes
    ROT_FOUR=0,
    END_ASYNC_FOR=-7,
    POP_FINALLY=-6,
    CALL_FINALLY=lambda oparg, jmp: 1 if jmp else 0,
    BEGIN_FINALLY=6,
)
