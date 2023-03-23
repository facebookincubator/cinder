import self
if self._lazy_imports:
    self.skipTest("Test relevant only when running with global lazy imports disabled")

import importlib

importlib.set_lazy_imports(excluding=["test.lazyimports.data.metasyntactic.bar"])

from test.lazyimports.data.metasyntactic import foo
self.assertTrue(importlib.is_lazy_import(globals(), "foo"))  # should be lazy

from test.lazyimports.data.metasyntactic import bar
self.assertTrue(importlib.is_lazy_import(globals(), "bar"))  # should be eager

foo  # force loaded
self.assertFalse(importlib.is_lazy_import(globals(), "foo"))  # should be loaded
