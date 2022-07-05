import importlib
import unittest

class TestLazyImports(unittest.TestCase):
    def __init__(self, *args, **kwargs):
        super(TestLazyImports, self).__init__(*args, **kwargs)

    @unittest.skipIfLazyImportsIsDisabled("Test relevant only when running with lazy imports enabled.")
    def test_lazy_imports_is_enabled(self):
        self.assertTrue(importlib.is_lazy_imports_enabled())

    @unittest.skipIfLazyImportsIsEnabled("Test relevant only when running with lazy imports disabled.")
    def test_lazy_imports_is_disabled(self):
        self.assertFalse(importlib.is_lazy_imports_enabled())

if __name__ == '__main__':
    unittest.main()
