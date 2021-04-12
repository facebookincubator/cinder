fun(a, *c, kw=1, **d)
# EXPECTED:
[
    ...,
    BUILD_TUPLE(1),
    ...,
    BUILD_TUPLE_UNPACK_WITH_CALL(2),
    ...,
    BUILD_MAP(1),
    ...,
    BUILD_MAP_UNPACK_WITH_CALL(2),
    CALL_FUNCTION_EX(1),
    ...,
]
