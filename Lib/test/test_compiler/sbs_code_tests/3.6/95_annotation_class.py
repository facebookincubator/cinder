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
    STORE_ANNOTATION('z'),
    ...,
]
