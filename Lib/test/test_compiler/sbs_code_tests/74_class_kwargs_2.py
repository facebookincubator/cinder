class Foo(int, x=42):
    pass
# EXPECTED:
[
    ...,
    LOAD_NAME('int'),
    LOAD_CONST(42),
    LOAD_CONST(('x',)),
    CALL_FUNCTION_KW(4),
    ...
]
