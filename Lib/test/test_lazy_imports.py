import importlib
import unittest

class TestLazyImports(unittest.TestCase):
    def __init__(self, *args, **kwargs):
        super(TestLazyImports, self).__init__(*args, **kwargs)

    @unittest.skipIfLazyImportsIsDisabled("Skip this test when Lazy Import is disabled.")
    def test_lazy_imports_is_enabled(self):
        self.assertTrue(importlib.is_lazy_imports_enabled())

    @unittest.skipIfLazyImportsIsEnabled("Skip this test when Lazy Import is enabled.")
    def test_lazy_imports_is_disabled(self):
        self.assertFalse(importlib.is_lazy_imports_enabled())

if __name__ == '__main__':
    unittest.main()
