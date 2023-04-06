import self
if self._lazy_imports:
    self.skipTest("Test relevant only when running with global lazy imports disabled")

import importlib

importlib.set_lazy_imports(excluding=["test.lazyimports.data.excluding.bar"])

from test.lazyimports.data.excluding import foo
self.assertTrue(importlib.is_lazy_import(foo.__dict__, "Foo"))  # should be lazy

from test.lazyimports.data.excluding import bar
self.assertFalse(importlib.is_lazy_import(bar.__dict__, "Bar"))  # should be loaded because bar is eager

self.assertEqual(foo.Foo, "Foo")  # force load
self.assertFalse(importlib.is_lazy_import(foo.__dict__, "Foo"))  # should be loaded now
