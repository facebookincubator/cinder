import unittest

from test.support.script_helper import run_python_until_end


class LazyImportsTest(unittest.TestCase):
    def assertStartswith(self, str1, str2):
        self.assertTrue(str1.startswith(str2), f"{str1!r}.startswith({str2!r})")

    def assertNotStartswith(self, str1, str2):
        self.assertFalse(str1.startswith(str2), f"not {str1!r}.startswith({str2!r})")

    def python_run(self, m, lazy=True, warmup=False):
        args = []
        if lazy:
            args += [
                "-X",
                "lazyimportsall",
            ]
        if warmup:
            args += [
                "-X",
                "lazyimportswarmup",
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

    def test_dict_update(self):
        rc, out, err = self.python_run("test.lazyimports.dict_update")
        self.assertNotStartswith(out, "<deferred ")
        self.assertStartswith(out, "<module 'warnings' from ")

    def test_deferred_resolve_failure(self):
        rc, out, err = self.python_run("test.lazyimports.deferred_resolve_failure")
        self.assertStartswith(out, "<function type_from_ast at ")

        rc, out, err = self.python_run("test.lazyimports.deferred_resolve_failure", warmup=True)
        self.assertStartswith(out, "<function type_from_ast at ")

        rc, out, err = self.python_run("test.lazyimports.deferred_resolve_failure", lazy=False)
        self.assertNotEqual(rc, 0)
        self.assertIn("(most likely due to a circular import)", err)

    def test_split_fromlist(self):
        rc, out, err = self.python_run("test.lazyimports.split_fromlist")
        self.assertEqual(out, "['test.lazyimports.split_fromlist.foo', 'test.lazyimports.split_fromlist.foo.bar']")
