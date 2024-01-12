import ast
import re
from compiler.static import StaticCodeGenerator
from compiler.static.compiler import Compiler
from compiler.static.module_table import ModuleTable, ModuleTableException
from compiler.static.types import Class, TypeName
from textwrap import dedent

from .common import bad_ret_type, StaticTestBase


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
        bcomp = self.compiler(a=acode, b=bcode).compile_module("b")
        x = self.find_code(bcomp, "f")
        self.assertInBytecode(x, "INVOKE_FUNCTION", (("a", "C", "f"), 1))

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
                bcomp = self.compiler(a=acode, b=bcode).compile_module("b")
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
        bcomp = self.compiler(a=acode, b=bcode).compile_module("b")
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
        bcomp = self.compiler(a=acode, b=bcode).compile_module("b")
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
        bcomp = self.compiler(a=acode, b=bcode).compile_module("b")
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
        self.compiler(a=acode, b=bcode).type_error(
            "b",
            re.escape(
                "type mismatch: str received for positional arg 'x', expected int"
            ),
            at="'abc'",
        )

        bcode = """
            from a import C

            def f() -> str:
                return C().f(42)
        """
        self.compiler(a=acode, b=bcode).type_error(
            "b", bad_ret_type("int", "str"), at="return"
        )

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
        self.compiler(a=acode, b=bcode).type_error(
            "b",
            re.escape("type mismatch: str cannot be assigned to int"),
            at="C().x",
        )

        bcode = """
            from a import C

            def f() -> str:
                return C().x
        """
        self.compiler(a=acode, b=bcode).type_error(
            "b", bad_ret_type("int", "str"), at="return"
        )

    def test_cross_module_import_time_resolution(self) -> None:
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
        bcomp = self.compiler(a=acode, b=bcode).compile_module("b")
        x = self.find_code(bcomp, "f")
        self.assertInBytecode(x, "INVOKE_FUNCTION", (("a", "C", "f"), 1))

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
        bcomp = self.compiler(a=acode, b=bcode).compile_module("b")
        x = self.find_code(bcomp, "f")
        self.assertInBytecode(x, "INVOKE_METHOD", (("a", "C", "f"), 0))

    def test_cross_module_rewrite(self) -> None:
        acode = """
            from b import B
            class C(B):
                def f(self):
                    return self.g()
        """
        bcode = """
            class B:
                def g(self):
                    return 1 + 2
        """
        testcase = self

        class CustomCompiler(Compiler):
            def __init__(self):
                super().__init__(StaticCodeGenerator)

            def import_module(self, name: str, optimize: int) -> ModuleTable:
                if name == "b":
                    btree = ast.parse(dedent(bcode))
                    self.btree = self.add_module("b", "b.py", btree, optimize=optimize)
                    testcase.assertFalse(self.btree is btree)

        compiler = CustomCompiler()
        acomp = compiler.compile("a", "a.py", ast.parse(dedent(acode)), optimize=1)
        bcomp = compiler.compile("b", "b.py", compiler.btree, optimize=1)
        x = self.find_code(self.find_code(acomp, "C"), "f")
        self.assertInBytecode(x, "INVOKE_METHOD", (("b", "B", "g"), 0))

    def test_declaring_toplevel_local_after_decl_visit_error(self) -> None:
        codestr = """
        class C:
            pass
        """

        class CustomCodeGenerator(StaticCodeGenerator):
            def visitClassDef(self, node):
                super().visitClassDef(node)
                self.cur_mod.declare_class(
                    node, Class(TypeName("mod", "C"), self.compiler.type_env)
                )

        class CustomCompiler(Compiler):
            def __init__(self):
                super().__init__(CustomCodeGenerator)

            def import_module(self, name: str, optimize: int) -> ModuleTable:
                if name == "b":
                    btree = ast.parse(dedent(bcode))

        compiler = CustomCompiler()
        with self.assertRaisesRegex(
            ModuleTableException,
            "Attempted to declare a class after the declaration visit",
        ):
            compiler.compile("a", "a.py", ast.parse(dedent(codestr)), optimize=1)
