fun(var, 10, a=a, kw=2)
# EXPECTED:
[
    ...,
    LOAD_CONST(('a', 'kw')),
    CALL_FUNCTION_KW(4),
    ...,
]
