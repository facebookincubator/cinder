def f():
    try:
        a
    finally:
        return 42
# EXPECTED:
[
    ...,
    CODE_START('f'),
    SETUP_FINALLY(Block(2)),
    ...,
    POP_BLOCK(0),
    LOAD_CONST(None),
    LOAD_CONST(42),
    RETURN_VALUE(0),
    END_FINALLY(0),
]
