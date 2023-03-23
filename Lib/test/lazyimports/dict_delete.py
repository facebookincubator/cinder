"""
Test deleting a module from the dict
"""
import self
import sys
import importlib

import test.lazyimports.data.metasyntactic.waldo
test.lazyimports.data.metasyntactic.waldo
import test.lazyimports.data.metasyntactic.waldo.fred

del sys.modules["test.lazyimports.data.metasyntactic.waldo"]

import test.lazyimports.data.metasyntactic.waldo
test.lazyimports.data.metasyntactic.waldo
import test.lazyimports.data.metasyntactic.waldo.fred

self.assertIs(test.lazyimports.data.metasyntactic.waldo, sys.modules["test.lazyimports.data.metasyntactic.waldo"])

if self._lazy_imports:
    self.assertTrue(importlib.is_lazy_import(test.lazyimports.data.metasyntactic.waldo.__dict__, "fred"))
else:
    self.assertNotIn("fred", dir(test.lazyimports.data.metasyntactic.waldo))
