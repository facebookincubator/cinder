def foo():
    a = 1

    def bar():
        nonlocal a
        a = 2
# EXPECTED:
[
    LOAD_CONST(Code((1, 0))),
    LOAD_CONST('foo'),
    MAKE_FUNCTION(0),
    ...,
    CODE_START('foo'),
    ...,
    STORE_DEREF('a'),
    LOAD_CLOSURE('a'),
    BUILD_TUPLE(1),
    LOAD_CONST(Code((4, 4))),
    LOAD_CONST('foo.<locals>.bar'),
    MAKE_FUNCTION(8),
    ...,
    CODE_START('bar'),
    ...,
    STORE_DEREF('a'),
    ...,
]
