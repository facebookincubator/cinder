c = (a, b)
d = {e: 1, f: 2}
fun(a, b, *c, **d)
# EXPECTED:
[
    ...,
    LOAD_NAME("fun"),
    LOAD_NAME("a"),
    LOAD_NAME("b"),
    BUILD_LIST(2),
    LOAD_NAME("c"),
    LIST_EXTEND(1),
    LIST_TO_TUPLE(0),
    BUILD_MAP(0),
    LOAD_NAME("d"),
    DICT_MERGE(1),
    CALL_FUNCTION_EX(1),
    ...,
]
