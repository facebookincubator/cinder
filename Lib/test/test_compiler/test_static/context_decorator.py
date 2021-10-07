import ast
import dis
from compiler.static import StaticCodeGenerator
from compiler.static.compiler import Compiler
from compiler.static.declaration_visitor import DeclarationVisitor
from textwrap import dedent

from .common import StaticTestBase


class ContextDecoratorTests(StaticTestBase):
    def test_simple(self):
        codestr = """
            from __static__ import ContextDecorator
            class MyDecorator(ContextDecorator):
                pass

            class C:
                @MyDecorator()
                def f(self):
                    return 42

            def f(c: C):
                return c.f()
        """
        with self.in_module(codestr) as mod:
            C = mod.C
            self.assertInBytecode(
                C.f,
                "INVOKE_METHOD",
                ((("__static__", "ContextDecorator", "_recreate_cm"), 0)),
            )
            self.assertEqual(C().f(), 42)

            f = mod.f
            self.assertInBytecode(
                f,
                "INVOKE_METHOD",
                (((mod.__name__, "C", "f"), 0)),
            )

    def test_simple_async(self):
        codestr = """
            from __future__ import annotations
            from __static__ import ContextDecorator
            calls = 0
            class MyDecorator(ContextDecorator):
                def __enter__(self) -> MyDecorator:
                    global calls
                    calls += 1
                    return self

            class C:
                @MyDecorator()
                async def f(self):
                    return 42
        """
        with self.in_module(codestr) as mod:
            C = mod.C
            self.assertInBytecode(
                C.f,
                "INVOKE_METHOD",
                ((("__static__", "ContextDecorator", "_recreate_cm"), 0)),
            )
            a = C().f()
            self.assertEqual(mod.calls, 0)
            try:
                a.send(None)
            except StopIteration as e:
                self.assertEqual(e.args[0], 42)
                self.assertEqual(mod.calls, 1)

    def test_property(self):
        codestr = """
            from __future__ import annotations
            from __static__ import ContextDecorator
            class MyDecorator(ContextDecorator):
                pass

            class C:
                @property
                @MyDecorator()
                def f(self):
                    return 42
        """
        with self.in_module(codestr) as mod:
            C = mod.C
            self.assertInBytecode(
                C.f.fget,
                "INVOKE_METHOD",
                ((("__static__", "ContextDecorator", "_recreate_cm"), 0)),
            )
            self.assertEqual(C().f, 42)

    def test_cached_property(self):
        codestr = """
            from __future__ import annotations
            from __static__ import ContextDecorator
            from cinder import cached_property

            class MyDecorator(ContextDecorator):
                def __enter__(self) -> MyDecorator:
                    return self

                def __exit__(self, exc_type: object, exc_val: object, exc_tb: object) -> bool:
                    return False

            X = 42
            class C:
                @cached_property
                @MyDecorator()
                def f(self):
                    global X
                    X += 1
                    return X
        """
        with self.in_module(codestr) as mod:
            C = mod.C
            self.assertInBytecode(
                C.f.func,
                "INVOKE_METHOD",
                ((("__static__", "ContextDecorator", "_recreate_cm"), 0)),
            )
            c = C()
            self.assertEqual(c.f, 43)
            self.assertEqual(c.f, 43)

    def test_async_cached_property(self):
        codestr = """
            from __future__ import annotations
            from __static__ import ContextDecorator
            from cinder import async_cached_property

            class MyDecorator(ContextDecorator):
                def __enter__(self) -> MyDecorator:
                    return self

                def __exit__(self, exc_type: object, exc_val: object, exc_tb: object) -> bool:
                    return False

            X = 42
            class C:
                @async_cached_property
                @MyDecorator()
                async def f(self):
                    global X
                    X += 1
                    return X
        """
        with self.in_module(codestr) as mod:
            C = mod.C
            self.assertInBytecode(
                C.f.func,
                "INVOKE_METHOD",
                ((("__static__", "ContextDecorator", "_recreate_cm"), 0)),
            )
            c = C()
            x = c.f.__await__()
            try:
                x.__next__()
            except StopIteration as e:
                self.assertEqual(e.args[0], 43)
            x = c.f.__await__()
            try:
                x.__next__()
            except StopIteration as e:
                self.assertEqual(e.args[0], 43)

    def test_static_method(self):
        codestr = """
            from __future__ import annotations
            from __static__ import ContextDecorator
            class MyDecorator(ContextDecorator):
                def __enter__(self) -> MyDecorator:
                    return self

                def __exit__(self, exc_type: object, exc_val: object, exc_tb: object) -> bool:
                    return False

            class C:
                @staticmethod
                @MyDecorator()
                def f():
                    return 42
        """
        with self.in_module(codestr) as mod:
            C = mod.C
            self.assertEqual(C().f(), 42)
            self.assertInBytecode(
                C.__dict__["f"].__func__,
                "INVOKE_METHOD",
                ((("__static__", "ContextDecorator", "_recreate_cm"), 0)),
            )
            self.assertEqual(C().f(), 42)
            self.assertEqual(C.f(), 42)

    def test_static_method_with_arg(self):
        codestr = """
            from __future__ import annotations
            from __static__ import ContextDecorator
            class MyDecorator(ContextDecorator):
                def __enter__(self) -> MyDecorator:
                    return self

                def __exit__(self, exc_type: object, exc_val: object, exc_tb: object) -> bool:
                    return False

            class C:
                @staticmethod
                @MyDecorator()
                def f(x):
                    return x
        """
        with self.in_module(codestr) as mod:
            C = mod.C
            self.assertEqual(C().f(42), 42)
            self.assertEqual(C.f(42), 42)
            self.assertInBytecode(
                C.__dict__["f"].__func__,
                "INVOKE_METHOD",
                ((("__static__", "ContextDecorator", "_recreate_cm"), 0)),
            )

    def test_static_method_compat_with_arg(self):
        codestr = """
            from __future__ import annotations
            from __static__ import ContextDecorator
            class MyDecorator(ContextDecorator):
                def __enter__(self) -> MyDecorator:
                    return self

                def __exit__(self, exc_type: object, exc_val: object, exc_tb: object) -> bool:
                    return False

            class C:
                @staticmethod
                @MyDecorator()
                def f(x: C):
                    return 42
        """
        with self.in_module(codestr) as mod:
            C = mod.C
            self.assertEqual(C().f(C()), 42)
            self.assertInBytecode(C.__dict__["f"].__func__, "LOAD_FAST", "x")
            self.assertInBytecode(
                C.__dict__["f"].__func__,
                "INVOKE_METHOD",
                ((("__static__", "ContextDecorator", "_recreate_cm"), 0)),
            )
            self.assertEqual(C().f(C()), 42)
            self.assertEqual(C.f(C()), 42)

    def test_class_method(self):
        codestr = """
            from __future__ import annotations
            from __static__ import ContextDecorator
            from typing import final
            calls = 0
            class MyDecorator(ContextDecorator):
                def __enter__(self) -> MyDecorator:
                    global calls
                    calls += 1
                    return self

                def __exit__(self, exc_type: object, exc_val: object, exc_tb: object) -> bool:
                    return False

            @final
            class C:
                @classmethod
                @MyDecorator()
                def f(cls):
                    return 42

            def f(c: C):
                return c.f()
        """
        with self.in_module(codestr) as mod:
            C = mod.C
            self.assertEqual(C().f(), 42)
            self.assertEqual(mod.calls, 1)
            self.assertInBytecode(
                C.__dict__["f"].__func__,
                "INVOKE_METHOD",
                ((("__static__", "ContextDecorator", "_recreate_cm"), 0)),
            )
            f = mod.f
            self.assertEqual(f(C()), 42)
            self.assertEqual(mod.calls, 2)

    def test_top_level(self):
        codestr = """
            from __static__ import ContextDecorator
            class MyDecorator(ContextDecorator):
                pass

            @MyDecorator()
            def f():
                return 42

            def g():
                return f()
        """
        with self.in_module(codestr) as mod:
            f = mod.f
            self.assertInBytecode(
                f,
                "INVOKE_METHOD",
                ((("__static__", "ContextDecorator", "_recreate_cm"), 0)),
            )
            self.assertEqual(f(), 42)

            g = mod.g
            self.assertInBytecode(g, "INVOKE_FUNCTION", (((mod.__name__, "f"), 0)))

    def test_recreate_cm(self):
        codestr = """
            from __future__ import annotations
            from __static__ import ContextDecorator
            class MyDecorator(ContextDecorator):
                def _recreate_cm(self) -> MyDecorator:
                    return self

            class C:
                @MyDecorator()
                def f(self):
                    return 42
        """
        with self.in_module(codestr) as mod:
            C = mod.C
            self.assertInBytecode(
                C.f,
                "INVOKE_METHOD",
                (((mod.__name__, "MyDecorator", "_recreate_cm"), 0)),
            )
            self.assertEqual(C().f(), 42)

    def test_recreate_cm_final(self):
        codestr = """
            from __future__ import annotations
            from __static__ import ContextDecorator
            from typing import final

            @final
            class MyDecorator(ContextDecorator):
                def _recreate_cm(self) -> MyDecorator:
                    return self

            class C:
                @MyDecorator()
                def f(self):
                    return 42
        """
        with self.in_module(codestr) as mod:
            C = mod.C
            self.assertInBytecode(
                C.f,
                "INVOKE_FUNCTION",
                (((mod.__name__, "MyDecorator", "_recreate_cm"), 1)),
            )
            self.assertEqual(C().f(), 42)

    def test_stacked(self):
        codestr = """
            from __future__ import annotations
            from __static__ import ContextDecorator
            calls = []
            class MyDecorator1(ContextDecorator):
                def __enter__(self) -> MyDecorator1:
                    calls.append("MyDecorator1.__enter__")
                    return self
                def __exit__(self, exc_type: object, exc_val: object, exc_tb: object) -> bool:
                    calls.append("MyDecorator1.__exit__")
                    return False
                def _recreate_cm(self):
                    return self

            class MyDecorator2(ContextDecorator):
                def __enter__(self) -> MyDecorator2:
                    calls.append("MyDecorator2.__enter__")
                    return self

                def __exit__(self, exc_type: object, exc_val: object, exc_tb: object) -> bool:
                    calls.append("MyDecorator2.__exit__")
                    return False

                def _recreate_cm(self):
                    return self

            class C:
                @MyDecorator1()
                @MyDecorator2()
                def f(self):
                    return 42
        """
        with self.in_module(codestr) as mod:
            C = mod.C
            self.assertEqual(C().f(), 42)
            self.assertInBytecode(
                C.f,
                "INVOKE_METHOD",
                (((mod.__name__, "MyDecorator1", "_recreate_cm"), 0)),
            )
            self.assertInBytecode(
                C.f,
                "INVOKE_METHOD",
                (((mod.__name__, "MyDecorator2", "_recreate_cm"), 0)),
            )
            self.assertEqual(
                mod.calls,
                [
                    "MyDecorator1.__enter__",
                    "MyDecorator2.__enter__",
                    "MyDecorator2.__exit__",
                    "MyDecorator1.__exit__",
                ],
            )

    def test_simple_func(self):
        codestr = """
            from __future__ import annotations
            from __static__ import ContextDecorator
            calls = 0
            class MyDecorator(ContextDecorator):
                def __enter__(self) -> MyDecorator:
                    global calls
                    calls += 1
                    return self

            def wrapper() -> MyDecorator:
                return MyDecorator()

            class C:
                @wrapper()
                def f(self) -> int:
                    return 42

                def x(self) -> int:
                    return self.f()
        """
        with self.in_module(codestr) as mod:
            C = mod.C
            a = C()
            self.assertEqual(a.f(), 42)
            self.assertEqual(mod.calls, 1)
            self.assertInBytecode(
                C.x,
                "INVOKE_METHOD",
                (((mod.__name__, "C", "f"), 0)),
            )

    def test_cross_module(self) -> None:
        acode = """
            from __future__ import annotations
            from __static__ import ContextDecorator

            def wrapper() -> MyDecorator:
                return MyDecorator()

            class MyDecorator(ContextDecorator):
                def __enter__(self) -> MyDecorator:
                    global calls
                    calls += 1
                    return self


                def __exit__(self, exc_type: object, exc_val: object, exc_tb: object) -> bool:
                    return False
        """
        bcode = """
            from a import wrapper

            class C:
                @wrapper()
                def f(self) -> int:
                    return 42

                def x(self) -> int:
                    return self.f()

        """
        compiler = Compiler(StaticCodeGenerator)
        aast = ast.parse(dedent(acode))
        bast = ast.parse(dedent(bcode))

        decl_visita = DeclarationVisitor("a", "a.py", compiler, optimize=0)
        decl_visita.visit(aast)

        decl_visitb = DeclarationVisitor("b", "b.py", compiler, optimize=0)
        decl_visitb.visit(bast)

        decl_visita.finish_bind()
        decl_visitb.finish_bind()

        bcomp = compiler.compile("b", "b.py", bast, optimize=0)
        f = self.find_code(self.find_code(bcomp, "C"), "f")
        self.assertInBytecode(
            f,
            "INVOKE_METHOD",
            ((("__static__", "ContextDecorator", "_recreate_cm"), 0)),
        )
