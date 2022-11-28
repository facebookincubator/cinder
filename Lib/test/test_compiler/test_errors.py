import ast
import dis
import inspect
import sys
import unittest
from dis import opmap, opname
from re import escape
from unittest import TestCase

from .common import CompilerTest

try:
    import cinderjit
except ImportError:
    cinderjit = None


class ErrorTests(CompilerTest):
    def test_bare_starred(self):
        with self.assertRaisesRegex(SyntaxError, "can't use starred expression here"):
            self.compile("*foo")

    def test_bare_starred_assign(self):
        with self.assertRaisesRegex(
            SyntaxError, "starred assignment target must be in a list or tuple"
        ):
            self.compile("*foo = 42")

    def test_yield_outside_func(self):
        with self.assertRaisesRegex(SyntaxError, "'yield' outside function"):
            self.compile("yield 42")

    def test_yield_outside_func_class(self):
        with self.assertRaisesRegex(SyntaxError, "'yield' outside function"):
            self.compile("class C: yield 42")

    def test_yield_from_async_func(self):
        with self.assertRaisesRegex(SyntaxError, "'yield from' inside async function"):
            self.compile(
                """
                async def f():
                    yield from [1,2]"""
            )

    def test_yield_from_outside_func(self):
        with self.assertRaisesRegex(SyntaxError, "'yield' outside function"):
            self.compile("yield from [1,2]")

    def test_yield_from_outside_func_class(self):
        with self.assertRaisesRegex(SyntaxError, "'yield' outside function"):
            self.compile("class C: yield from [1,2]")

    def test_return_outside_func(self):
        with self.assertRaisesRegex(SyntaxError, "'return' outside function"):
            self.compile("return")

    def test_return_outside_func_class(self):
        with self.assertRaisesRegex(SyntaxError, "'return' outside function"):
            self.compile(
                """
                class C:
                    return"""
            )

    def test_async_with_outside_async_func(self):
        with self.assertRaisesRegex(SyntaxError, "'async with' outside async function"):
            self.compile(
                """
                def f():
                    async with x:
                        pass"""
            )

    def test_async_with_outside_func(self):
        with self.assertRaisesRegex(SyntaxError, "'async with' outside async function"):
            self.compile(
                """
                async with x:
                    pass"""
            )

    def test_return_with_value_async_gen(self):
        with self.assertRaisesRegex(
            SyntaxError, "'return' with value in async generator"
        ):
            self.compile(
                """
                async def f():
                    yield 42
                    return 42"""
            )

    def test_continue_outside_loop(self):
        with self.assertRaisesRegex(SyntaxError, "'continue' not properly in loop"):
            self.compile("continue")

    def test_continue_outside_loop_in_try(self):
        with self.assertRaisesRegex(SyntaxError, "'continue' not properly in loop"):
            self.compile(
                """
                try:
                    continue
                except:
                    pass"""
            )

    def test_continue_outside_loop_in_except(self):
        with self.assertRaisesRegex(SyntaxError, "'continue' not properly in loop"):
            self.compile(
                """
                try:
                    pass
                except:
                    continue"""
            )

    def test_break_outside_loop(self):
        with self.assertRaisesRegex(SyntaxError, "'break' outside loop"):
            self.compile("break")

    def test_bare_except_ordering(self):
        with self.assertRaisesRegex(SyntaxError, "default 'except:' must be last"):
            self.compile(
                """
                try:
                    pass
                except:
                    pass
                except Exception as e:
                    pass"""
            )

    def test_future_import_order(self):
        with self.assertRaisesRegex(
            SyntaxError,
            "from __future__ imports must occur at the beginning of the file",
        ):
            self.compile(
                """
                x = 1
                from __future__ import generator_stop"""
            )

    def test_future_import_in_func(self):
        with self.assertRaisesRegex(
            SyntaxError,
            "from __future__ imports must occur at the beginning of the file",
        ):
            self.compile(
                """
                def f():
                    from __future__ import generator_stop"""
            )

    def test_double_star_in_assignment(self):
        with self.assertRaisesRegex(
            SyntaxError, "multiple starred expressions in assignment"
        ):
            self.compile("*x, *y = 1, 2")

    def test_star_unpack_limit(self):
        self.compile(", ".join(("x",) * 255) + ", *x, = range(255)")

    def test_star_unpack_limit_exceeded(self):
        with self.assertRaisesRegex(
            SyntaxError, "too many expressions in star-unpacking assignment"
        ):
            self.compile(", ".join(("x",) * 256) + ", *x, = range(256)")

    @unittest.skipUnless(cinderjit is None, "JIT doesn't support recursion checks")
    def test_recursion_error_when_expression_too_deep(self):
        fail_depth = sys.getrecursionlimit() * 3

        def check_limit(prefix, repeated):
            with self.assertRaisesRegex(
                RecursionError, "maximum recursion depth exceeded"
            ):
                self.compile(f"{prefix}{repeated * fail_depth}")

        check_limit("a", ".b")
        check_limit("a", "[0]")
        check_limit("a", "*a")


class ErrorTestsBuiltin(ErrorTests):
    def compile(self, code):
        code = inspect.cleandoc("\n" + code)
        return compile(code, "file.py", "exec")


if __name__ == "__main__":
    unittest.main()
