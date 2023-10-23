def f(x: int):
    pass

# EXPECTED:
[
    ...,
    BUILD_TUPLE(2),
    ...,
    MAKE_FUNCTION(4),
    ...,
    CODE_START('f'),
    ...,
]
