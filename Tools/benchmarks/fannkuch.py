#!/usr/bin/env python3
"""
The Computer Language Benchmarks Game
http://benchmarksgame.alioth.debian.org/
Contributed by Sokolov Yura, modified by Tupteq.
"""
DEFAULT_ARG = 9


def fannkuch(n):
    count = list(range(1, n + 1))
    max_flips = 0
    m = n - 1
    r = n
    check = 0
    perm1 = list(range(n))
    perm = list(range(n))
    perm1_ins = perm1.insert
    perm1_pop = perm1.pop

    while 1:
        if check < 30:
            check += 1

        while r != 1:
            count[r - 1] = r
            r -= 1

        if perm1[0] != 0 and perm1[m] != m:
            perm = perm1[:]
            flips_count = 0
            k = perm[0]
            while k:
                perm[: k + 1] = perm[k::-1]
                flips_count += 1
                k = perm[0]

            if flips_count > max_flips:
                max_flips = flips_count

        while r != n:
            perm1_ins(r, perm1_pop(0))
            count[r] -= 1
            if count[r] > 0:
                break
            r += 1
        else:
            return max_flips


def run():
    arg = DEFAULT_ARG
    fannkuch(arg)


if __name__ == "__main__":
    import sys

    num_iterations = 1
    if len(sys.argv) > 1:
        num_iterations = int(sys.argv[1])
    for _ in range(num_iterations):
        res = fannkuch(DEFAULT_ARG)
        assert res == 30
