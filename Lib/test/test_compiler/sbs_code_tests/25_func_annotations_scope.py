def foo():
    ann = None
    def bar(a: ann) -> ann:
        pass
# EXPECTED:
[
    LOAD_CONST(Code((1, 0))),
    LOAD_CONST('foo'),
    MAKE_FUNCTION(0),
    STORE_NAME('foo'),
    ...,
    CODE_START('foo'),
    ...,
    LOAD_FAST('ann'),
    LOAD_FAST('ann'),
    LOAD_CONST(('a', 'return')),
    BUILD_CONST_KEY_MAP(2),
    LOAD_CONST(Code((3, 4))),
    LOAD_CONST('foo.<locals>.bar'),
    MAKE_FUNCTION(4),
    ...,
    CODE_START('bar'),
    ...,
]
