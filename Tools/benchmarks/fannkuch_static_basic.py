#!/usr/bin/env python3
import sys

from fannkuch_static_basic_lib import DEFAULT_ARG, fannkuch


if __name__ == "__main__":
    num_iterations = 1
    if len(sys.argv) > 1:
        num_iterations = int(sys.argv[1])
    for _ in range(num_iterations):
        res = fannkuch(DEFAULT_ARG)
        assert res == 30
