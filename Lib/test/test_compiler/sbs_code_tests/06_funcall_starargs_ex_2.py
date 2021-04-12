fun(a, *b, c, *d)
# EXPECTED:
[
    ...,
    BUILD_TUPLE(1),
    ...,
    BUILD_TUPLE(1),
    ...,
    BUILD_TUPLE_UNPACK_WITH_CALL(4),
    CALL_FUNCTION_EX(0),
    ...,
]
