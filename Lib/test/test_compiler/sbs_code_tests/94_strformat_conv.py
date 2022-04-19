def f(name, args):
    return f"foo.{name!r}{name!s}{name!a}"
# EXPECTED:
[
    ...,
    CODE_START('f'),
    LOAD_CONST('foo.'),
    ...,
    FORMAT_VALUE(2),
    ...,
    FORMAT_VALUE(1),
    ...,
    FORMAT_VALUE(3),
    BUILD_STRING(4),
    ...,
]
