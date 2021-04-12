async def foo(x: int, y, z: bytes, *args: 1, a: str, **kwargs: "sth") -> bool:
    pass
# EXPECTED:
[
    ...,
    LOAD_CONST(('x', 'z', 'args', 'a', 'kwargs', 'return')),
    BUILD_CONST_KEY_MAP(6),
    LOAD_CONST(Code((1, 0))),
    LOAD_CONST('foo'),
    MAKE_FUNCTION(4),
    ...,
    CODE_START('foo'),
    ...,
]
