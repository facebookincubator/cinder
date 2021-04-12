def foo():
    class Foo:
        def bar(a=b.c, *, b=c.d):
            pass
# EXPECTED:
[
    ...,
    CODE_START('foo'),
    ...,
    CODE_START('Foo'),
    ...,
    LOAD_NAME('b'),
    LOAD_ATTR('c'),
    BUILD_TUPLE(1),
    LOAD_NAME('c'),
    LOAD_ATTR('d'),
    LOAD_CONST(('b',)),
    BUILD_CONST_KEY_MAP(1),
    ...,
    MAKE_FUNCTION(3),
    ...,
    CODE_START('bar'),
    ...,
]
