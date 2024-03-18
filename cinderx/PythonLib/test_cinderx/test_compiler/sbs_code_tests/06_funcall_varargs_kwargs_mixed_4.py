fun(a, *c)
# EXPECTED:
[
    LOAD_NAME("fun"),
    LOAD_NAME("a"),
    BUILD_LIST(1),
    LOAD_NAME("c"),
    LIST_EXTEND(1),
    LIST_TO_TUPLE(0),
    CALL_FUNCTION_EX(0),
    ...,
]
