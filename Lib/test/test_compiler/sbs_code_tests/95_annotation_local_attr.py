def f():
    int.new_attr: int
# EXPECTED:
[
    ...,
    CODE_START('f'),
    LOAD_GLOBAL('int'),
    POP_TOP(0),
    ...,
]
