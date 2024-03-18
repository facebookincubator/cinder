def foo():
    ann = None

    def bar(a: ann) -> ann:
        pass


# EXPECTED:
[
    LOAD_CONST(Code((1, 0))),
    LOAD_CONST("foo"),
    MAKE_FUNCTION(0),
    STORE_NAME("foo"),
    ...,
    CODE_START("foo"),
    ...,
    LOAD_CONST(None),
    STORE_FAST("ann"),
    LOAD_CONST("a"),
    LOAD_FAST("ann"),
    LOAD_CONST("return"),
    LOAD_FAST("ann"),
    BUILD_TUPLE(4),
    LOAD_CONST(Code((3, 4))),
    LOAD_CONST("foo.<locals>.bar"),
    MAKE_FUNCTION(4),
    ...,
    CODE_START("bar"),
    ...,
]
