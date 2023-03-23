"""
Test `importlib.is_lazy_imports_enabled`
"""
import self
import _imp
import importlib

previous = importlib.is_lazy_imports_enabled()

p = _imp._set_lazy_imports()
self.assertTrue(importlib.is_lazy_imports_enabled())
_imp._set_lazy_imports(*p)

self.assertEqual(importlib.is_lazy_imports_enabled(), previous)
