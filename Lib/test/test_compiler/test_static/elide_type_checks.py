import unittest

from .common import StaticTestBase

try:
    import cinderjit
except ImportError:
    cinderjit = None


class ElideTypeChecksTests(StaticTestBase):
    def test_check_args_precedes_gen_start(self) -> None:
        codestr = """
            async def f():
                pass
        """
        with self.in_module(codestr) as mod:
            self.assertInBytecode(mod.f, "CHECK_ARGS", (), index=0)
            self.assertInBytecode(mod.f, "GEN_START", 1, index=1)

    @unittest.skipIf(cinderjit is None, "JIT disabled")
    def test_invoke_function_skips_arg_type_checks(self) -> None:
        codestr = """
            from xxclassloader import unsafe_change_type

            class A:
                pass

            class B:
                pass

            def g(a: A) -> str:
                return a.__class__.__name__

            def f() -> str:
                a = A()
                # compiler is unaware that this changes the type of `a`,
                # so it unsafely allows the following call g(a)
                unsafe_change_type(a, B)
                return g(a)
        """
        with self.in_strict_module(codestr) as mod:
            self.assertInBytecode(mod.f, "INVOKE_FUNCTION", ((mod.__name__, "g"), 1))
            # Non-static call should always check arg types
            with self.assertRaisesRegex(
                TypeError, "g expected 'A' for argument a, got 'B'"
            ):
                mod.g(mod.B())

            cinderjit.force_compile(mod.f)

            # Should not raise a TypeError because static invokes skip arg
            # checks in JITed code.  This results in an unsound call in this case,
            # but only because we are using an unsound C extension method.
            self.assertEqual(mod.f(), "B")

    @unittest.skipIf(cinderjit is None, "JIT disabled")
    def test_invoke_method_skips_arg_type_checks(self) -> None:
        codestr = """
            from xxclassloader import unsafe_change_type

            class A:
                pass

            class B:
                pass

            class C:
                def g(self, a: A) -> str:
                    return a.__class__.__name__

            def f(c: C) -> str:
                a = A()
                # compiler is unaware that this changes the type of `a`,
                # so it unsafely allows the following call C.g(a)
                unsafe_change_type(a, B)
                return c.g(a)
        """
        with self.in_strict_module(codestr) as mod:
            self.assertInBytecode(mod.f, "INVOKE_METHOD", ((mod.__name__, "C", "g"), 1))
            # Non-static call should always check arg types
            with self.assertRaisesRegex(
                TypeError, "g expected 'A' for argument a, got 'B'"
            ):
                mod.C().g(mod.B())

            # force compilation on first call and populate the v-table
            # with the JITed entrypoint
            try:
                mod.f(mod.C())
            except TypeError:
                pass

            # Should not raise a TypeError because static invokes skip arg
            # checks in JITed code.  This results in an unsound call in this case,
            # but only because we are using an unsound C extension method.
            self.assertEqual(mod.f(mod.C()), "B")

    def test_elide_check_with_one_optional(self) -> None:
        codestr = """
            from typing import Optional
            def foo() -> int:
                def bar(g: Optional[str] = None) -> int:
                    return int(g or "42")
                return bar()
        """
        with self.in_module(codestr) as mod:
            f = mod.foo
            self.assertEqual(f(), 42)

    def test_type_error_raised_when_eliding_defaults(self) -> None:
        codestr = """
            from typing import Optional
            def foo(f: int, g: Optional[str] = None) -> int:
                return int(g or "42")
        """
        with self.in_module(codestr) as mod:
            f = mod.foo
            with self.assertRaises(TypeError):
                f("1")
