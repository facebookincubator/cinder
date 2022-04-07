from compiler.pycodegen import PythonCodeGenerator
from compiler.static.types import TypedSyntaxError

from .common import StaticTestBase


class ImportTests(StaticTestBase):
    def test_unknown_import_with_fallback_is_not_allowed(self):
        codestr = """
        try:
            from unknown import foo
        except ImportError:
            def foo() -> int:
                return 0
        """
        self.type_error(codestr, r"Cannot redefine local variable foo")

    def test_function_definition_with_import_fallback_is_not_allowed(self):
        codestr = """
        try:
            def foo() -> int:
                return 0
        except ImportError:
            from unknown import foo
        """
        self.type_error(codestr, r"Cannot redefine local variable foo")

    def test_unknown_value_from_known_module_is_dynamic(self):
        acode = """
        x: int = 1
        """
        bcode = """
            from a import x, y

            reveal_type(y)
        """
        with self.assertRaisesRegex(TypedSyntaxError, "dynamic"):
            bcomp = self.compiler(a=acode, b=bcode).compile_module("b")

    def test_unknown_value_from_nonstatic_module_is_dynamic(self):
        nonstatic_code = """
        pass
        """
        with self.in_module(
            nonstatic_code, code_gen=PythonCodeGenerator, name="nonstatic"
        ):
            codestr = """
            from nonstatic import x

            reveal_type(x)
            """
            self.type_error(codestr, r"reveal_type\(x\): 'dynamic'")
