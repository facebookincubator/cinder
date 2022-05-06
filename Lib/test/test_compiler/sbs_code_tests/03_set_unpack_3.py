{1, *[1, 2, 3], *[4, 5, 6]}
# EXPECTED:
[
    LOAD_CONST(1),
    BUILD_SET(1),
    BUILD_LIST(0),
    LOAD_CONST((1, 2, 3)),
    LIST_EXTEND(1),
    SET_UPDATE(1),
    BUILD_LIST(0),
    LOAD_CONST((4, 5, 6)),
    LIST_EXTEND(1),
    SET_UPDATE(1),
    ...,
]
