#!/usr/bin/env python3

import os
import sys
import multiprocessing

def compute():
    n = 0
    for i in range(10):
        n += i
    return n

def child1():
    print("child1({}) computed {}".format(os.getpid(), compute()))

def child2():
    print("child2({}) computed {}".format(os.getpid(), compute()))

def parent():
    print("parent({}) computed {}".format(os.getpid(), compute()))

def main():
    queue = multiprocessing.Queue(2)

    pid1 = os.fork()
    if pid1 == 0:
        queue.put("child1", False)
        sys.exit(child1())

    pid2 = os.fork()
    if pid2 == 0:
        queue.put("child2", False)
        sys.exit(child2())

    read = {queue.get(True, timeout=2), queue.get(True, timeout=2)}
    if read != {"child1", "child2"}:
        raise RuntimeError(f"Unexpected message queue contents: {read}")

    parent()
    os.waitpid(pid1, 0)
    os.waitpid(pid2, 0)

if __name__ == "__main__":
    sys.exit(main())
