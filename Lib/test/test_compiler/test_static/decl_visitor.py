import ast
import inspect
import re
from compiler.static import StaticCodeGenerator
from compiler.static.errors import TypedSyntaxError
from compiler.static.symbol_table import SymbolTable
from textwrap import dedent
from types import MemberDescriptorType

from .common import StaticTestBase
from .tests import bad_ret_type


class DeclarationVisitorTests(StaticTestBase):
    def test_cross_module(self) -> None:
        acode = """
            class C:
                def f(self):
                    return 42
        """
        bcode = """
            from a import C

            def f():
                x = C()
                return x.f()
        """
        symtable = SymbolTable(StaticCodeGenerator)
        acomp = symtable.compile("a", "a.py", ast.parse(dedent(acode)))
        bcomp = symtable.compile("b", "b.py", ast.parse(dedent(bcode)))
        x = self.find_code(bcomp, "f")
        self.assertInBytecode(x, "INVOKE_METHOD", (("a", "C", "f"), 0))

    def test_cross_module_nested(self) -> None:
        for parent, close in [
            ("if FOO:", ""),
            ("for x in []:", ""),
            ("while True:", ""),
            ("with foo:", ""),
            ("try:", "except: pass"),
        ]:
            with self.subTest(parent=parent, close=close):
                acode = f"""
                    {parent}
                        class C:
                            def f(self):
                                return 42
                    {close}
                """
                bcode = """
                    from a import C

                    def f():
                        x = C()
                        return x.f()
                """
                symtable = SymbolTable(StaticCodeGenerator)
                acomp = symtable.compile("a", "a.py", ast.parse(dedent(acode)))
                bcomp = symtable.compile("b", "b.py", ast.parse(dedent(bcode)))
                x = self.find_code(bcomp, "f")
                self.assertNotInBytecode(x, "INVOKE_METHOD", (("a", "C", "f"), 0))

    def test_cross_module_inst_decl_visit_only(self) -> None:
        acode = """
            class C:
                def f(self):
                    return 42

            x: C = C()
        """
        bcode = """
            from a import x

            def f():
                return x.f()
        """
        symtable = SymbolTable(StaticCodeGenerator)
        acomp = symtable.add_module("a", "a.py", ast.parse(dedent(acode)))
        bcomp = symtable.compile("b", "b.py", ast.parse(dedent(bcode)))
        x = self.find_code(bcomp, "f")
        self.assertInBytecode(x, "INVOKE_METHOD", (("a", "C", "f"), 0))

    def test_cross_module_inst_decl_final_dynamic_is_invoked(self) -> None:
        acode = """
            from typing import Final, Protocol
            def foo(x: int) -> int:
                    return x + 42

            class CallableProtocol(Protocol):
                def __call__(self, x: int) -> int:
                    pass

            f: Final[CallableProtocol] = foo
        """
        bcode = """
            from a import f

            def g():
                return f(1)
        """
        symtable = SymbolTable(StaticCodeGenerator)
        acomp = symtable.add_module("a", "a.py", ast.parse(dedent(acode)))
        bcomp = symtable.compile("b", "b.py", ast.parse(dedent(bcode)))
        x = self.find_code(bcomp, "g")
        self.assertInBytecode(x, "INVOKE_FUNCTION")

    def test_cross_module_inst_decl_alias_is_not_invoked(self) -> None:
        acode = """
            from typing import Final, Protocol
            def foo(x: int) -> int:
                    return x + 42
            f = foo
        """
        bcode = """
            from a import f

            def g():
                return f(1)
        """
        symtable = SymbolTable(StaticCodeGenerator)
        acomp = symtable.add_module("a", "a.py", ast.parse(dedent(acode)))
        bcomp = symtable.compile("b", "b.py", ast.parse(dedent(bcode)))
        x = self.find_code(bcomp, "g")
        self.assertNotInBytecode(x, "INVOKE_FUNCTION")

    def test_cross_module_decl_visit_type_check_methods(self) -> None:
        acode = """
            class C:
                def f(self, x: int = 42) -> int:
                    return x
        """
        bcode = """
            from a import C

            def f():
                return C().f('abc')
        """
        symtable = SymbolTable(StaticCodeGenerator)
        acomp = symtable.add_module("a", "a.py", ast.parse(dedent(acode)))
        with self.assertRaisesRegex(
            TypedSyntaxError,
            re.escape(
                "type mismatch: Exact[str] received for positional arg 'x', expected int"
            ),
        ):
            symtable.compile("b", "b.py", ast.parse(dedent(bcode)))

        bcode = """
            from a import C

            def f() -> str:
                return C().f(42)
        """
        symtable = SymbolTable(StaticCodeGenerator)
        acomp = symtable.add_module("a", "a.py", ast.parse(dedent(acode)))
        with self.assertRaisesRegex(TypedSyntaxError, bad_ret_type("int", "str")):
            symtable.compile("b", "b.py", ast.parse(dedent(bcode)))

    def test_cross_module_decl_visit_type_check_fields(self) -> None:
        acode = """
            class C:
                def __init__(self):
                    self.x: int = 42
        """
        bcode = """
            from a import C

            def f():
                C().x = 'abc'
        """
        symtable = SymbolTable(StaticCodeGenerator)
        acomp = symtable.add_module("a", "a.py", ast.parse(dedent(acode)))
        with self.assertRaisesRegex(
            TypedSyntaxError,
            re.escape("type mismatch: Exact[str] cannot be assigned to int"),
        ):
            symtable.compile("b", "b.py", ast.parse(dedent(bcode)))

        bcode = """
            from a import C

            def f() -> str:
                return C().x
        """
        symtable = SymbolTable(StaticCodeGenerator)
        acomp = symtable.add_module("a", "a.py", ast.parse(dedent(acode)))
        with self.assertRaisesRegex(TypedSyntaxError, bad_ret_type("int", "str")):
            symtable.compile("b", "b.py", ast.parse(dedent(bcode)))

    def test_cross_module_import_time_resolution(self) -> None:
        class TestSymbolTable(SymbolTable):
            def import_module(self, name):
                if name == "a":
                    symtable.add_module("a", "a.py", ast.parse(dedent(acode)))

        acode = """
            class C:
                def f(self):
                    return 42
        """
        bcode = """
            from a import C

            def f():
                x = C()
                return x.f()
        """
        symtable = TestSymbolTable(StaticCodeGenerator)
        bcomp = symtable.compile("b", "b.py", ast.parse(dedent(bcode)))
        x = self.find_code(bcomp, "f")
        self.assertInBytecode(x, "INVOKE_METHOD", (("a", "C", "f"), 0))

    def test_cross_module_type_checking(self) -> None:
        acode = """
            class C:
                def f(self):
                    return 42
        """
        bcode = """
            from typing import TYPE_CHECKING

            if TYPE_CHECKING:
                from a import C

            def f(x: C):
                return x.f()
        """
        symtable = SymbolTable(StaticCodeGenerator)
        acode = ast.parse(dedent(acode))
        bcode = ast.parse(dedent(bcode))
        symtable.add_module("a", "a.py", acode)
        symtable.add_module("b", "b.py", bcode)
        acomp = symtable.compile("a", "a.py", acode)
        bcomp = symtable.compile("b", "b.py", bcode)
        x = self.find_code(bcomp, "f")
        self.assertInBytecode(x, "INVOKE_METHOD", (("a", "C", "f"), 0))
