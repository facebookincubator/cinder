def f(x: int):
    pass

# EXPECTED:
[
    LOAD_NAME('int'),
    LOAD_CONST(('x',)),
    BUILD_CONST_KEY_MAP(1),
    ...,
    MAKE_FUNCTION(4),
    ...,
    CODE_START('f'),
    ...,
]
