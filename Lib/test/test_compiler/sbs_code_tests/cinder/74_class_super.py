class Foo:
    def __init__(self):
        super().__init__()

    def no_super(self):
        return


# EXPECTED:
[
    LOAD_BUILD_CLASS(0),
    LOAD_CONST(Code((1, 0))),
    LOAD_CONST("Foo"),
    MAKE_FUNCTION(0),
    LOAD_CONST("Foo"),
    CALL_FUNCTION(2),
    STORE_NAME("Foo"),
    ...,
    CODE_START("Foo"),
    ...,
    LOAD_CLOSURE("__class__"),
    ...,
    MAKE_FUNCTION(8),
    ...,
    DUP_TOP(0),
    STORE_NAME("__classcell__"),
    ...,
    CODE_START("__init__"),
    LOAD_GLOBAL("super"),
    CALL_FUNCTION(0),
    LOAD_METHOD("__init__"),
    CALL_METHOD(0),
    ...,
    CODE_START("no_super"),
    ...,
]
