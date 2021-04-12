from .opcode36 import opcode as opcode36
from .opcodebase import Opcode

opcode: Opcode = opcode36.copy()
opcode.remove_op("STORE_ANNOTATION")
opcode.name_op("LOAD_METHOD", 160)
opcode.def_op("CALL_METHOD", 161)

opcode.stack_effects.update(
    # Updated stack effects
    SETUP_WITH=lambda oparg, jmp=0: 6 if jmp else 1,
    WITH_CLEANUP_START=2,  # or 1, depending on TOS
    WITH_CLEANUP_FINISH=-3,
    POP_EXCEPT=-3,
    END_FINALLY=-6,
    FOR_ITER=lambda oparg, jmp=0: -1 if jmp > 0 else 1,
    JUMP_IF_TRUE_OR_POP=lambda oparg, jmp=0: 0 if jmp else -1,
    JUMP_IF_FALSE_OR_POP=lambda oparg, jmp=0: 0 if jmp else -1,
    SETUP_EXCEPT=lambda oparg, jmp: 6 if jmp else 0,
    SETUP_FINALLY=lambda oparg, jmp: 6 if jmp else 0,
    SETUP_ASYNC_WITH=lambda oparg, jmp: (-1 + 6) if jmp else 0,
    # New opcodes
    CALL_METHOD=lambda oparg, jmp: -oparg - 1,
    LOAD_METHOD=1,
)
