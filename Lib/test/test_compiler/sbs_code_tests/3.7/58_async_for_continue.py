async def test3():
    async for i in x:
        if i > 20:
           continue
        else:
           c
    d
# EXPECTED:
[
    ...,
    CODE_START('test3'),
    SETUP_LOOP(Block(10)),
    ...,
    GET_AITER(0),
    SETUP_EXCEPT(Block(2)),
    GET_ANEXT(0),
    LOAD_CONST(None),
    YIELD_FROM(0),
    ...,
    POP_BLOCK(0),
    JUMP_FORWARD(Block(3)),
    DUP_TOP(0),
    LOAD_GLOBAL('StopAsyncIteration'),
    COMPARE_OP('exception match'),
    POP_JUMP_IF_TRUE(Block(8)),
    END_FINALLY(0),
    ...,
    POP_JUMP_IF_FALSE(Block(6)),
    JUMP_ABSOLUTE(Block(1)),
    JUMP_FORWARD(Block(7)),
    ...,
    JUMP_ABSOLUTE(Block(1)),
    POP_TOP(0),
    POP_TOP(0),
    POP_TOP(0),
    POP_EXCEPT(0),
    POP_TOP(0),
    POP_BLOCK(0),
    ...,
]
