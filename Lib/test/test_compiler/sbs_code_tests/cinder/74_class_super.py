class Foo:

    def __init__(self):
        super().__init__()

    def no_super(self):
        return
# EXPECTED:
[
    LOAD_BUILD_CLASS(0),
    LOAD_CONST(Code((1, 0))),
    LOAD_CONST('Foo'),
    MAKE_FUNCTION(0),
    LOAD_CONST('Foo'),
    CALL_FUNCTION(2),
    STORE_NAME('Foo'),
    ...,
    CODE_START('Foo'),
    ...,
    LOAD_CLOSURE('__class__'),
    ...,
    MAKE_FUNCTION(8),
    ...,
    DUP_TOP(0),
    STORE_NAME('__classcell__'),
    ...,
    CODE_START('__init__'),
    LOAD_GLOBAL('super'),
    LOAD_DEREF('__class__'),
    LOAD_FAST('self'),
    LOAD_METHOD_SUPER(('__init__', True)),
    CALL_METHOD(0),
    ...,
    CODE_START('no_super'),
    ...,
]
