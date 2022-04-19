c = (a, b)
d = {e: 1, f: 2}
fun(a, b, *c, **d)
# EXPECTED:
[
    ...,
    BUILD_TUPLE(2),
    ...,
    BUILD_TUPLE_UNPACK_WITH_CALL(2),
    ...,
    CALL_FUNCTION_EX(1),
    ...,
]
