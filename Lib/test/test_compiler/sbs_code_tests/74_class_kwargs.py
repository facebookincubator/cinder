class Foo(x=42):
    pass
# EXPECTED:
[
    ...,
    LOAD_CONST(Code((1, 0))),
    ...,
    LOAD_CONST(42),
    LOAD_CONST(('x',)),
    CALL_FUNCTION_KW(3),
    ...
]
