async def foo(x: int, y, z: bytes, *args: 1, a: str, **kwargs: "sth") -> bool:
    pass


# EXPECTED:
[
    LOAD_CONST("x"),
    LOAD_NAME("int"),
    LOAD_CONST("z"),
    LOAD_NAME("bytes"),
    LOAD_CONST("args"),
    LOAD_CONST(1),
    LOAD_CONST("a"),
    LOAD_NAME("str"),
    LOAD_CONST("kwargs"),
    LOAD_CONST("sth"),
    LOAD_CONST("return"),
    LOAD_NAME("bool"),
    BUILD_TUPLE(12),
    LOAD_CONST(Code((1, 0))),
    LOAD_CONST("foo"),
    MAKE_FUNCTION(4),
    ...,
    CODE_START("foo"),
    ...,
]
