fun(a, kw=1, *c)
# EXPECTED:
[
    ...,
    BUILD_TUPLE(1),
    ...,
    BUILD_TUPLE_UNPACK_WITH_CALL(2),
    ...,
    BUILD_MAP(1),
    CALL_FUNCTION_EX(1),
    ...,
]
