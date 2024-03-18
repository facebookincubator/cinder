import __static__

from dep import callee


def caller(x: int) -> int:
    return callee(x)
