c = (a, b)
fun(a, b, *c)
# EXPECTED:
[
    LOAD_NAME("a"),
    LOAD_NAME("b"),
    BUILD_TUPLE(2),
    STORE_NAME("c"),
    LOAD_NAME("fun"),
    LOAD_NAME("a"),
    LOAD_NAME("b"),
    BUILD_LIST(2),
    LOAD_NAME("c"),
    LIST_EXTEND(1),
    LIST_TO_TUPLE(0),
    CALL_FUNCTION_EX(0),
    ...,
]
