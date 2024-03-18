with a as b:
    pass
# EXPECTED:
[
    LOAD_NAME("a"),
    SETUP_WITH(Block(2)),
    STORE_NAME("b"),
    POP_BLOCK(0),
    LOAD_CONST(None),
    DUP_TOP(0),
    DUP_TOP(0),
    CALL_FUNCTION(3),
    POP_TOP(0),
    ...,
    WITH_EXCEPT_START(0),
    POP_JUMP_IF_TRUE(Block(4)),
    RERAISE(1),
    ...,
]
