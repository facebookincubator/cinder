fun(a, *b, c)
# EXPECTED:
[
    LOAD_NAME("fun"),
    LOAD_NAME("a"),
    BUILD_LIST(1),
    LOAD_NAME("b"),
    LIST_EXTEND(1),
    LOAD_NAME("c"),
    LIST_APPEND(1),
    LIST_TO_TUPLE(0),
    CALL_FUNCTION_EX(0),
    ...,
]
