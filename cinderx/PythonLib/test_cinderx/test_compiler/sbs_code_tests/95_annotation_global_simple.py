def f():
    some_global: int
    print(some_global)

# EXPECTED:
[
    ...,
    CODE_START('f'),
    ~LOAD_CONST('int'),
]
