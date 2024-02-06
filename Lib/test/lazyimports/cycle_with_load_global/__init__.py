# Copyright (c) Meta, Inc. and its affiliates. All Rights Reserved
# File added for Lazy Imports

try:
    from . import a
    a
# we expect a circular import ImportError; anything else is wrong
except ImportError as e:
    if "circular import" not in str(e):
        raise
