import importlib

"""
Verify `from foo import *`, imported names that are lazy in `foo` should still be lazy.
"""
from test.lazyimports.customized_modules.module import *
assert(importlib.is_lazy_import(globals(), "sub_module1"))
assert(importlib.is_lazy_import(globals(), "sub_module2"))
assert(importlib.is_lazy_import(globals(), "sub_module3"))
