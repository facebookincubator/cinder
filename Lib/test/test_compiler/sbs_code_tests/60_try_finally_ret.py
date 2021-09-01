def f():
    try:
        a
    finally:
        return 42


# EXPECTED:
[
    ...,
    CODE_START("f"),
    LOAD_CONST(None),
    SETUP_FINALLY(Block(1)),
    ...,
    POP_BLOCK(0),
    BEGIN_FINALLY(0),
    POP_FINALLY(0),
    POP_TOP(0),
    LOAD_CONST(42),
    RETURN_VALUE(0),
    END_FINALLY(0),
    POP_TOP(0),
]
