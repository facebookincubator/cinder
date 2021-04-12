def f(name, args):
    return f"foo.{name}({', '.join(args)})"
# EXPECTED:
[
    ...,
    CODE_START('f'),
    LOAD_CONST('foo.'),
    ...,
    FORMAT_VALUE(0),
    LOAD_CONST('('),
    ...,
    FORMAT_VALUE(0),
    LOAD_CONST(')'),
    BUILD_STRING(5),
    ...,
]
