c = (a, b)
fun(a, b, *c)
# EXPECTED:
[
    ...,
    BUILD_TUPLE_UNPACK_WITH_CALL(2),
    CALL_FUNCTION_EX(0),
    ...,
]
