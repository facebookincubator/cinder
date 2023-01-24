# This is in its own file as it modifies the global JIT-list.

import os
import unittest

from test import cinder_support


try:
    import cinderjit
except:
    cinderjit = None


def _jit_function_1():
    pass


def _jit_function_2():
    pass


def _no_jit_function():
    pass


class _JitClass:
    def jitMethod(self):
        pass


class JitListTest(unittest.TestCase):
    @cinder_support.skipUnlessJITEnabled("No JIT-list if no JIT")
    def test_comments(self) -> None:
        cinderjit.jit_list_append("")
        initial_jit_list = cinderjit.get_jit_list()
        cinderjit.jit_list_append("# asdfasdfasd")
        cinderjit.jit_list_append("# x:y.z")
        cinderjit.jit_list_append("# x@y:1")
        self.assertEqual(initial_jit_list, cinderjit.get_jit_list())

    @cinder_support.skipUnlessJITEnabled("No JIT-list if no JIT")
    def test_py_function(self) -> None:
        meth = _JitClass.jitMethod
        func = _jit_function_1
        cinderjit.jit_list_append(f"{meth.__module__}:{meth.__qualname__}")
        cinderjit.jit_list_append(f"{func.__module__}:{func.__qualname__}")
        py_funcs = cinderjit.get_jit_list()[0]
        self.assertIn(meth.__qualname__, py_funcs[__name__])
        self.assertIn(func.__qualname__, py_funcs[__name__])
        self.assertNotIn(_no_jit_function.__qualname__, py_funcs[__name__])
        meth(None)
        self.assertTrue(cinderjit.is_jit_compiled(meth))
        func()
        self.assertTrue(cinderjit.is_jit_compiled(func))
        _no_jit_function()
        self.assertFalse(cinderjit.is_jit_compiled(_no_jit_function))

    @cinder_support.skipUnlessJITEnabled("No JIT-list if no JIT")
    def test_py_code(self) -> None:
        code_obj = _jit_function_2.__code__
        cinderjit.jit_list_append(
            f"{code_obj.co_name}@{code_obj.co_filename}:{code_obj.co_firstlineno}"
        )
        py_code_objs = cinderjit.get_jit_list()[1]
        thisfile = os.path.basename(__file__)
        self.assertIn(code_obj.co_firstlineno, py_code_objs[code_obj.co_name][thisfile])
        self.assertNotIn(_no_jit_function.__code__.co_name, py_code_objs)
        _jit_function_2()
        self.assertTrue(cinderjit.is_jit_compiled(_jit_function_2))
        _no_jit_function()
        self.assertFalse(cinderjit.is_jit_compiled(_no_jit_function))

    @cinder_support.skipUnlessJITEnabled("No JIT-list if no JIT")
    def test_change_func_qualname(self) -> None:
        def inner_func():
            return 24

        cinderjit.jit_list_append(
            f"{inner_func.__module__}:{inner_func.__qualname__}_foo"
        )

        self.assertEqual(inner_func(), 24)
        self.assertFalse(cinderjit.is_jit_compiled(inner_func))
        inner_func.__qualname__ += "_foo"
        self.assertEqual(inner_func(), 24)
        self.assertTrue(cinderjit.is_jit_compiled(inner_func))


if __name__ == "__main__":
    unittest.main()
