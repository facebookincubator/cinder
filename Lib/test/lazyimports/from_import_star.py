import self
if not self._lazy_imports:
    self.skipTest("Test relevant only when running with lazy imports enabled")

import importlib

"""
Verify `from foo import *`, imported names that are lazy in `foo` should still be lazy.
"""

from test.lazyimports.data.metasyntactic.names import *

self.assertTrue(importlib.is_lazy_import(globals(), "Foo"))
self.assertTrue(importlib.is_lazy_import(globals(), "Ack"))
self.assertTrue(importlib.is_lazy_import(globals(), "Bar"))
self.assertFalse(importlib.is_lazy_import(globals(), "Metasyntactic"))
