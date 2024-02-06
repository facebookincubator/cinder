# Copyright (c) Meta, Inc. and its affiliates. All Rights Reserved
# File added for Lazy Imports

import self
if not self._lazy_imports:
    self.skipTest("Test relevant only when running with lazy imports enabled")

import importlib

importlib.set_lazy_imports(excluding=["test.lazyimports.data.metasyntactic.foo.bar"])

import test.lazyimports.data.metasyntactic.foo.bar as bar
import test.lazyimports.data.metasyntactic.foo.bar.baz

self.assertFalse(importlib.is_lazy_import(bar.__dict__, "baz"))
