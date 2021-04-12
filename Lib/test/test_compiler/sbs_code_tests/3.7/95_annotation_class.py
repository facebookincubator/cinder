class F():
    z: int = 5

# EXPECTED:
[
    ...,
    CODE_START('F'),
    ...,
    SETUP_ANNOTATIONS(0),
    LOAD_CONST(5),
    STORE_NAME('z'),
    LOAD_NAME('int'),
    LOAD_NAME('__annotations__'),
    LOAD_CONST('z'),
    STORE_SUBSCR(0),
    ...
]
