# Copyright (c) Facebook, Inc. and its affiliates. (http://www.facebook.com)
"""
    Simple, brute-force N-Queens solver. Using typing python
    Made by sebastiancr@fb.com(Sebastian Chaves) based on nqueens.py made by collinwinter@google.com (Collin Winter)
"""
from __future__ import annotations
import __static__
from typing import Generator, Tuple, Iterator

# Pure-Python implementation of itertools.permutations().
def permutations(iterable: Iterator[int], r: int = -1) -> Iterator[List[int]]:
    """permutations(range(3), 2) --> (0,1) (0,2) (1,0) (1,2) (2,0) (2,1)"""
    pool: List[int] = list(iterable)
    n: int = len(pool)
    if r == -1:
        r = n
    indices: List[int] = list(range(n))
    cycles: List[int] = list(range(n - r + 1, n + 1))[::-1]
    yield list(pool[i] for i in indices[:r])
    while n:
        for i in reversed(range(r)):
            cycles[i] -= 1
            if cycles[i] == 0:
                indices[i:] = indices[i + 1 :] + indices[i : i + 1]
                cycles[i] = n - i
            else:
                j = cycles[i]
                indices[i], indices[-j] = indices[-j], indices[i]
                yield list(pool[i] for i in indices[:r])
                break
        else:
            return


# From http://code.activestate.com/recipes/576647/
def n_queens(queen_count: int) -> Iterator[List[int]]:
    """N-Queens solver.

    Args:
        queen_count: the number of queens to solve for. This is also the
            board size.

    Yields:
        Solutions to the problem. Each yielded value is looks like
        (3, 8, 2, 1, 4, ..., 6) where each number is the column position for the
        queen, and the index into the tuple indicates the row.
    """
    cols: Iterator[int] = range(queen_count)
    for vec in permutations(cols):
        if (
            queen_count
            == len(set(vec[i] + i for i in cols))  # noqa: C401
            == len(set(vec[i] - i for i in cols))  # noqa: C401
        ):
            yield vec


def bench_n_queens(queen_count: int) -> List[List[int]]:
    return list(n_queens(queen_count))
