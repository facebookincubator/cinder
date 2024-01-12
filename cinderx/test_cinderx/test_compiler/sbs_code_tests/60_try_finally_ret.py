def f():
    try:
        a
    finally:
        return 42


# EXPECTED:
[
    ...,
    CODE_START("f"),
    SETUP_FINALLY(Block(3)),
    LOAD_GLOBAL("a"),
    POP_TOP(0),
    POP_BLOCK(0),
    LOAD_CONST(42),
    RETURN_VALUE(0),
    POP_TOP(0),
    POP_TOP(0),
    POP_TOP(0),
    POP_EXCEPT(0),
    LOAD_CONST(42),
    RETURN_VALUE(0),
    ...,
]
