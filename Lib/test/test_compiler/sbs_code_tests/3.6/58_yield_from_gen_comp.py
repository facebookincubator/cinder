((yield from abc) for x in iter)
# EXPECTED:
[
    ...,
    CODE_START('<genexpr>'),
    ...,
    LOAD_GLOBAL('abc'),
    GET_YIELD_FROM_ITER(0),
    LOAD_CONST(None),
    YIELD_FROM(0),
    YIELD_VALUE(0),
    ...
]
