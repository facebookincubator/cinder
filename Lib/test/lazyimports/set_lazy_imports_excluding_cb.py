import self
if self._lazy_imports:
    self.skipTest("Test relevant only when running with global lazy imports disabled")

import importlib

class Checker:
    matches = 0
    def filter(self, name):
        return name == "test.lazyimports.data.excluding.bar"
    def __contains__(self, name):
        if self.filter(name):
            self.matches += 1
            return True
        return False

checker = Checker()
importlib.set_lazy_imports(excluding=checker)

from test.lazyimports.data.excluding import foo
self.assertTrue(importlib.is_lazy_import(foo.__dict__, "Foo"))  # should be lazy

from test.lazyimports.data.excluding import bar
self.assertFalse(importlib.is_lazy_import(bar.__dict__, "Bar"))  # should be loaded because bar is eager

self.assertEqual(foo.Foo, "Foo")  # force load
self.assertFalse(importlib.is_lazy_import(foo.__dict__, "Foo"))  # should be loaded now

self.assertEqual(checker.matches, 1)
