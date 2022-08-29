import importlib
from importlib import is_lazy_imports_enabled
import unittest
from test.support.script_helper import run_python_until_end

class TestLazyImportsSanity(unittest.TestCase):
    def __init__(self, *args, **kwargs):
        super(TestLazyImportsSanity, self).__init__(*args, **kwargs)

    @unittest.skipUnless(is_lazy_imports_enabled(), "Test relevant only when running with lazy imports enabled")
    def test_lazy_imports_is_enabled(self):
        self.assertTrue(importlib.is_lazy_imports_enabled())

    @unittest.skipIf(is_lazy_imports_enabled(), "Test relevant only when running with lazy imports disabled")
    def test_lazy_imports_is_disabled(self):
        self.assertFalse(importlib.is_lazy_imports_enabled())

class LazyImportsTest(unittest.TestCase):
    def assertStartswith(self, str1, str2):
        self.assertTrue(str1.startswith(str2), f"{str1!r}.startswith({str2!r})")

    def assertNotStartswith(self, str1, str2):
        self.assertFalse(str1.startswith(str2), f"not {str1!r}.startswith({str2!r})")

    def python_run(self, m, lazy=True):
        args = []
        if lazy:
            args += [
                "-L"
            ]

        args += [
            "-m",
            m,
        ]
        res, cmd_line = run_python_until_end(*args)
        return (
            res.rc,
            res.out.decode("ascii", "replace").rstrip(),
            res.err.decode("ascii", "replace").rstrip(),
        )

    def run_and_check(self, module, lazy_imports=True, expected_rc=0, expected_out="", expected_err=""):
        rc, out, err = self.python_run(module, lazy=lazy_imports)
        self.assertEqual(rc, expected_rc)
        self.assertEqual(out, expected_out)
        self.assertEqual(err, expected_err)

    def test_dict_update(self):
        rc, out, err = self.python_run("test.lazyimports.dict_update")
        self.assertRegex(out, r"<module 'warnings' from '.+/warnings.py'>")

    def test_deferred_resolve_failure(self):
        rc, out, err = self.python_run("test.lazyimports.deferred_resolve_failure")
        self.assertStartswith(out, "<function type_from_ast at ")

        rc, out, err = self.python_run("test.lazyimports.deferred_resolve_failure", lazy=False)
        self.assertNotEqual(rc, 0)
        self.assertIn("(most likely due to a circular import)", err)

    def test_split_fromlist(self):
        correct_out = "['test.lazyimports.split_fromlist.foo', 'test.lazyimports.split_fromlist.foo.bar']"
        self.run_and_check("test.lazyimports.split_fromlist", expected_out=correct_out)

    def test_enable_lazy_imports_at_runtime(self):
        self.run_and_check("test.lazyimports.importlib_apis.enable_lazy_imports_at_runtime", False)

    def test_set_lazy_imports_excluding(self):
        self.run_and_check("test.lazyimports.importlib_apis.set_lazy_imports_excluding_list", False)
        self.run_and_check("test.lazyimports.importlib_apis.set_lazy_imports_excluding_cb", False)
        self.run_and_check("test.lazyimports.importlib_apis.set_lazy_imports_excluding_cb_list", False)
        self.run_and_check("test.lazyimports.importlib_apis.set_lazy_imports_excluding_list")
        self.run_and_check("test.lazyimports.importlib_apis.set_lazy_imports_excluding_cb")
        self.run_and_check("test.lazyimports.importlib_apis.set_lazy_imports_excluding_cb_list")

    def test_future_eager_imports(self):
        self.run_and_check("test.lazyimports.future_eager.future_eager_mod", True)

    def test_attribute_side_effect(self):
        self.run_and_check("test.lazyimports.attr_side_effect", False)
        self.run_and_check("test.lazyimports.attr_side_effect")

    def test_dict_changes_when_loading(self):
        self.run_and_check("test.lazyimports.check_dict_changes_when_loading", False)
        self.run_and_check("test.lazyimports.check_dict_changes_when_loading")

    def test_dict(self):
        self.run_and_check("test.lazyimports.dict_tests")

    def test_from_import_star(self):
        self.run_and_check("test.lazyimports.from_import_star")

    def test_lazy_attribute_side_effect(self):
        self.run_and_check("test.lazyimports.lazy_attribute_side_effect")

if __name__ == '__main__':
    unittest.main()
