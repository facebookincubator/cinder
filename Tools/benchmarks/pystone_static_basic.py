# Copyright (c) Meta Platforms, Inc. and affiliates.
import sys
from pystone_static_basic_lib import run

if __name__ == "__main__":
    import sys

    num_iterations = 2
    if len(sys.argv) > 1:
        num_iterations = int(sys.argv[1])
    for _ in range(num_iterations):
        run()
