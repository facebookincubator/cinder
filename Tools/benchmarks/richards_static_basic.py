# Copyright (c) Facebook, Inc. and its affiliates. (http://www.facebook.com)
import sys
from richards_static_basic_lib import Richards


if __name__ == "__main__":
    num_iterations = 1
    if len(sys.argv) > 1:
        num_iterations = int(sys.argv[1])
    Richards().run(num_iterations)
