fun(*b, c)
# EXPECTED:
[
    LOAD_NAME("fun"),
    BUILD_LIST(0),
    LOAD_NAME("b"),
    LIST_EXTEND(1),
    LOAD_NAME("c"),
    LIST_APPEND(1),
    LIST_TO_TUPLE(0),
    CALL_FUNCTION_EX(0),
    ...,
]
