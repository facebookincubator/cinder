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
