import __static__

from dep2 import callee2


def callee(x: int) -> int:
    return callee2(x)
