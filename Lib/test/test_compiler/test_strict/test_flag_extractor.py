import ast

from compiler.strict.common import DEFAULT_STUB_PATH
from compiler.strict.flag_extractor import BadFlagException, FlagExtractor, Flags
from textwrap import dedent
from typing import final, Optional, Sequence

from cinderx.strictmodule import StrictAnalysisResult, StrictModuleLoader

from .common import StrictTestBase
from .sandbox import sandbox


@final
class FlagExtractorTest(StrictTestBase):
    def _get_flags(self, code: str) -> Flags:
        code = dedent(code)
        pyast = ast.parse(code)
        flags = FlagExtractor().get_flags(pyast)
        return flags

    def test_strict_import(self):
        code = """
        import __strict__
        x = 1
        """
        flags = self._get_flags(code)
        self.assertEqual(Flags(is_strict=True), flags)

    def test_static_import(self):
        code = """
        import __static__
        x = 1
        """
        flags = self._get_flags(code)
        self.assertEqual(Flags(is_static=True), flags)

    def test_both_static_and_strict_import(self):
        code = """
        import __static__
        import __strict__
        x = 1
        """
        flags = self._get_flags(code)
        self.assertEqual(Flags(is_static=True, is_strict=True), flags)

        code = """
        import __strict__
        import __static__
        x = 1
        """
        flags = self._get_flags(code)
        self.assertEqual(Flags(is_static=True, is_strict=True), flags)

    def test_import_in_class(self):
        code = """
        class A:
            import __strict__
            x = 1
        """
        self.assertRaisesRegex(
            BadFlagException,
            "__strict__ must be a globally namespaced import",
            lambda: self._get_flags(code),
        )

    def test_import_in_function(self):
        code = """
        def foo():
            import __strict__
            x = 1
        """
        self.assertRaisesRegex(
            BadFlagException,
            "__strict__ must be a globally namespaced import",
            lambda: self._get_flags(code),
        )

    def test_import_after_other_import(self):
        code = """
        import foo
        import __strict__
        x = 1
        """
        self.assertRaisesRegex(
            BadFlagException,
            "Cinder flag __strict__ must be at the top of a file",
            lambda: self._get_flags(code),
        )

    def test_import_after_docstring(self):
        code = """
        '''
        here is a docstring
        '''
        import __strict__
        x = 1
        """
        self.assertEqual(Flags(is_strict=True), self._get_flags(code))

    def test_import_after_two_docstrings(self):
        code = """
        '''
        here is a docstring
        '''
        '''
        here is another docstring
        '''
        import __strict__
        x = 1
        """
        self.assertRaisesRegex(
            BadFlagException,
            "Cinder flag __strict__ must be at the top of a file",
            lambda: self._get_flags(code),
        )

    def test_import_after_constant(self):
        code = """
        42
        import __strict__
        x = 1
        """
        self.assertRaisesRegex(
            BadFlagException,
            "Cinder flag __strict__ must be at the top of a file",
            lambda: self._get_flags(code),
        )

    def test_import_after_docstring_and_constant(self):
        code = """
        '''
        here is a docstring
        '''
        42
        import __strict__
        x = 1
        """
        self.assertRaisesRegex(
            BadFlagException,
            "Cinder flag __strict__ must be at the top of a file",
            lambda: self._get_flags(code),
        )

    def test_import_after_class(self):
        code = """
        class Foo:
            pass
        import __strict__
        x = 1
        """
        self.assertRaisesRegex(
            BadFlagException,
            "Cinder flag __strict__ must be at the top of a file",
            lambda: self._get_flags(code),
        )

    def test_import_alias(self):
        code = """
        import __strict__ as strict
        x = 1
        """
        self.assertRaisesRegex(
            BadFlagException,
            "__strict__ flag may not be aliased",
            lambda: self._get_flags(code),
        )

    def test_flag_after_future_import(self):
        code = """
        from __future__ import annotations
        import __strict__
        """
        flags = self._get_flags(code)
        self.assertEqual(Flags(is_strict=True), flags)
