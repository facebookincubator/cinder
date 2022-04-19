c = {a: 1, b: 2}
fun(a, b, **c)
# EXPECTED:
[
    ...,
    LOAD_NAME('fun'),
    ...,
    BUILD_TUPLE(2),
    ...,
    CALL_FUNCTION_EX(1),
    ...,
]
