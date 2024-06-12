# Copyright (c) Meta, Inc. and its affiliates. All Rights Reserved
# File added for Lazy Imports

import self
if self._lazy_imports:
    self.skipTest("Test relevant only when running with global lazy imports disabled")

import importlib

importlib.set_lazy_imports(eager=[
    "test.lazyimports.data.metasyntactic.foo",
    "test.lazyimports.data.metasyntactic.waldo",
    "test.lazyimports.data.metasyntactic.plugh.Plugh",
])

import test.lazyimports.data.metasyntactic.foo as foo
self.assertFalse(importlib.is_lazy_import(globals(), "foo"))  # should be eager

from test.lazyimports.data.metasyntactic.foo import bar
self.assertFalse(importlib.is_lazy_import(globals(), "bar"))  # maybe this should have been lazy?

from test.lazyimports.data.metasyntactic.waldo import Waldo
self.assertFalse(importlib.is_lazy_import(globals(), "Waldo"))  # maybe this should have been lazy?

import test.lazyimports.data.metasyntactic.waldo.fred as fred
self.assertTrue(importlib.is_lazy_import(globals(), "fred"))  # this should be lazy

from test.lazyimports.data.metasyntactic.waldo.fred import Fred
self.assertTrue(importlib.is_lazy_import(globals(), "Fred"))  # this should be lazy

from test.lazyimports.data.metasyntactic.waldo import fred
self.assertFalse(importlib.is_lazy_import(globals(), "fred"))  # maybe this should have been lazy?

import test.lazyimports.data.metasyntactic.plugh as plugh
self.assertTrue(importlib.is_lazy_import(globals(), "plugh"))  # this should be lazy

from test.lazyimports.data.metasyntactic.plugh import Plugh
self.assertFalse(importlib.is_lazy_import(globals(), "Plugh"))  # explicitly eager
