class C:
    pass
# EXPECTED:
[
    LOAD_BUILD_CLASS(0),
    LOAD_CONST(Code((1, 0))),
    LOAD_CONST('C'),
    MAKE_FUNCTION(0),
    LOAD_CONST('C'),
    CALL_FUNCTION(2),
    STORE_NAME('C'),
    ...,
    CODE_START('C'),
    LOAD_NAME('__name__'),
    STORE_NAME('__module__'),
    LOAD_CONST('C'),
    STORE_NAME('__qualname__'),
    ...,
]
