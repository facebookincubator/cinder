"""
Test disabling lazy imports by using `importlib.set_lazy_imports(False)`
"""
import self
if not self._lazy_imports:
    self.skipTest("Test relevant only when running with lazy imports enabled")

import importlib

def disable_lazy_imports():
    importlib.set_lazy_imports(False)

self.assertTrue(importlib.is_lazy_imports_enabled())

disable_lazy_imports()

self.assertFalse(importlib.is_lazy_imports_enabled())
