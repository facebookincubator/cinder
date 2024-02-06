# Copyright (c) Meta, Inc. and its affiliates. All Rights Reserved
# File added for Lazy Imports

def X1():
    return "X"

from .y import Y1

def X2():
    return Y1()
