z: int = 5
# EXPECTED:
[
    SETUP_ANNOTATIONS(0),
    ...,
    LOAD_NAME('int'),
    LOAD_NAME('__annotations__'),
    LOAD_CONST('z'),
    STORE_SUBSCR(0),
    ...
]
