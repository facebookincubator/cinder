from __static__ import TYPED_DOUBLE

import re
from compiler.errors import TypedSyntaxError

from .common import StaticTestBase


class UnknownNameTests(StaticTestBase):
    def test_unknown_name_toplevel(self) -> None:
        codestr = """
        b = a + 1
        """
        self.type_error(codestr, r"Name `a` is not defined.")

    def test_unknown_name_class_toplevel(self) -> None:
        codestr = """
        class C:
            b: int = a + 1
        """
        self.type_error(codestr, r"Name `a` is not defined.")

    def test_unknown_name_method(self) -> None:
        codestr = """
        class C:
            def foo(self) -> int:
                b = a + 1
                return 0
        """
        self.type_error(codestr, r"Name `a` is not defined.")

    def test_unknown_name_function(self) -> None:
        codestr = """
        def foo() -> int:
            return a
        """
        self.type_error(codestr, r"Name `a` is not defined.")

    def test_builtins_ok(self) -> None:
        codestr = """
        def foo() -> None:
            a = open("sourcefile.hs")
        """
        self.compile(codestr)

    def test_no_unknown_name_error_assignments(self) -> None:
        codestr = """
        def foo() -> None:
            a: int = 1
            b = 2
        """
        self.compile(codestr)

    def test_unknown_name_error_augassign(self) -> None:
        codestr = """
        def foo() -> None:
            a += 1
        """
        self.type_error(codestr, r"Name `a` is not defined.")

    def test_with_optional_vars_are_known(self) -> None:
        codestr = """
        def foo(x) -> None:
            with x() as y:
               pass
        """
        self.compile(codestr)

    def test_inline_import_supported(self) -> None:
        codestr = """
        def f():
            import math
            return math.isnan
        """
        self.compile(codestr)

    def test_inline_import_as_supported(self) -> None:
        codestr = """
        def f():
            import os.path as road # Modernization.
            return road.exists
        """
        self.compile(codestr)

    def test_inline_from_import_names_supported(self) -> None:
        acode = """
        x: int = 42
        """
        bcode = """
            def f():
                from a import x
                return x
        """
        bcomp = self.compiler(a=acode, b=bcode).compile_module("b")

    def test_inline_from_import_names_supported_alias(self) -> None:
        acode = """
        x: int = 42
        """
        bcode = """
            def f():
                from a import x as y
                return y
        """
        bcomp = self.compiler(a=acode, b=bcode).compile_module("b")

    def test_unknown_decorated_functions_declared(self) -> None:
        codestr = """
            def foo(x):
                return x
            def bar():
                baz()
            @foo
            def baz():
                pass
        """
        self.compile(codestr)

    def test_cellvars_known(self) -> None:
        codestr = """
            def use(x):
                return x

            def foo(x):
                use(x)
                def nested():
                    return x
                return nested
        """
        self.compile(codestr)

    def test_name_defined_in_except_and_else_known(self) -> None:
        codestr = """
            def foo(self):
                try:
                    pass
                except Exception:
                    a = None
                else:
                    a = None
                return a
        """
        self.compile(codestr)

    def test_name_defined_only_in_else_unknown(self) -> None:
        codestr = """
            def foo(self):
                try:
                    pass
                except Exception:
                    pass
                else:
                    a = None
                return a
        """
        self.type_error(codestr, r"Name `a` is not defined.")

    def test_name_defined_only_in_if_unknown(self) -> None:
        codestr = """
            def foo(self, p):
                if p:
                    a = None
                return a
        """
        self.type_error(codestr, r"Name `a` is not defined.")

    def test_name_defined_only_in_else_unknown(self) -> None:
        codestr = """
            def foo(self, p):
                if p:
                    pass
                else:
                    a = None
                return a
        """
        self.type_error(codestr, r"Name `a` is not defined.")

    def test_name_defined_terminal_except_raises(self) -> None:
        codestr = """
            def foo(self):
                try:
                    a = None
                except:
                    raise Exception
                return a
        """
        self.compile(codestr)

    def test_name_defined_terminal_except_returns(self) -> None:
        codestr = """
            def foo(self):
                try:
                    a = None
                except:
                    return None
                return a
        """
        self.compile(codestr)
