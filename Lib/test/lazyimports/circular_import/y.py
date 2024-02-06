# Copyright (c) Meta, Inc. and its affiliates. All Rights Reserved
# File added for Lazy Imports

def Y1():
    return "Y"

from .x import X2

def Y2():
    return X2()
