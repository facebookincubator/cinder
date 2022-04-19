fun(a, *b, c)
# EXPECTED:
[
    ...,
    BUILD_TUPLE(1),
    ...,
    BUILD_TUPLE(1),
    BUILD_TUPLE_UNPACK_WITH_CALL(3),
    CALL_FUNCTION_EX(0),
    ...,
]
