import importlib
import unittest

class TestLazyImportsSanity(unittest.TestCase):
    def __init__(self, *args, **kwargs):
        super(TestLazyImportsSanity, self).__init__(*args, **kwargs)

    @unittest.skipIfLazyImportsDisabled("Test relevant only when running with lazy imports enabled")
    def test_lazy_imports_is_enabled(self):
        self.assertTrue(importlib.is_lazy_imports_enabled())

    @unittest.skipIfLazyImportsEnabled("Test relevant only when running with lazy imports disabled")
    def test_lazy_imports_is_disabled(self):
        self.assertFalse(importlib.is_lazy_imports_enabled())

if __name__ == '__main__':
    unittest.main()
