# Copyright (c) Meta, Inc. and its affiliates. All Rights Reserved
# File added for Lazy Imports

"""
Test the behavior of dict.update when it is manipulating a lazy object
"""
import self
import warnings

vars = {}
vars.update(globals())

result = vars['warnings']

self.assertEqual(result, warnings)
