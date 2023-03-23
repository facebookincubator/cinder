import _imp
import importlib
import os
import sys
import unittest


class LazyImportsTest(unittest.TestCase):
    def test_lazy_imports(self):
        original_lazy_modules = sys.lazy_modules.copy()
        original_modules = sys.modules.copy()
        try:
            sys.lazy_modules.clear()
            sys.modules["self"] = self
            for modname in list(sys.modules):
                if modname == "test" or modname.startswith("test."):
                    del sys.modules[modname]
            stripped_modules = sys.modules.copy()
            base = os.path.dirname(__file__)
            tests = []
            for path in os.listdir(os.path.join(base, "lazyimports")):
                if path == "data" or path.startswith(("_", ".")):
                    continue
                if path.endswith(".py"):
                    path = path[:-3]
                test_name = "test.lazyimports." + path.replace(os.sep, ".")
                tests.append(test_name)
            for test_name in tests:
                for lazy_imports in (True, False):
                    msg = f"{test_name}{' (lazy)' if lazy_imports else ' (eager)'}"
                    with self.subTest(msg=msg):
                        self._test_name = test_name
                        self._lazy_imports = lazy_imports
                        previously = _imp._set_lazy_imports(lazy_imports)
                        try:
                            importlib.import_module(test_name)
                        finally:
                            _imp._set_lazy_imports(*previously)
                            del self._test_name
                            del self._lazy_imports
                            sys.lazy_modules.clear()
                            sys.modules.clear()
                            sys.modules.update(stripped_modules)

        finally:
            sys.lazy_modules.clear()
            sys.lazy_modules.update(original_lazy_modules)
            sys.modules.clear()
            sys.modules.update(original_modules)
