((yield 42) for i in gen())
# EXPECTED:
[
    ...,
    LOAD_CONST(42),
    YIELD_VALUE(0),
    YIELD_VALUE(0),
    ...
]
