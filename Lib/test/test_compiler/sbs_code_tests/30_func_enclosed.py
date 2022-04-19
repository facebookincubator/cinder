def foo():
    a = 1

    def inner():
        a

# EXPECTED:
[
    LOAD_CONST(Code((1, 0))),
    LOAD_CONST('foo'),
    MAKE_FUNCTION(0),
    ...,
    CODE_START('foo'),
    LOAD_CONST(1),
    STORE_DEREF('a'),
    LOAD_CLOSURE('a'),
    BUILD_TUPLE(1),
    LOAD_CONST(Code((4, 4))),
    LOAD_CONST('foo.<locals>.inner'),
    MAKE_FUNCTION(8),
    ...,
    CODE_START('inner'),
    LOAD_DEREF('a'),
    ...,
]
