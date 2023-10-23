import ast

from compiler.strict.common import DEFAULT_STUB_PATH
from compiler.strict.flag_extractor import FlagExtractor
from textwrap import dedent
from typing import final, Optional, Sequence

from cinderx.strictmodule import StrictAnalysisResult, StrictModuleLoader

from .common import StrictTestBase
from .sandbox import sandbox


class CompilerTests(StrictTestBase):
    def analyze(
        self,
        code: str,
        mod_name: str = "mod",
        import_path: Optional[Sequence[str]] = None,
        allow_list_prefix: Optional[Sequence[str]] = None,
        stub_root: str = DEFAULT_STUB_PATH,
        forced_strict_name: Optional[str] = None,
        allow_list_regex: Optional[Sequence[str]] = None,
    ) -> StrictAnalysisResult:
        code = dedent(code)
        compiler = StrictModuleLoader(
            import_path or [],
            stub_root,
            allow_list_prefix or [],
            [],
            True,
            allow_list_regex or [],
        )

        if forced_strict_name is not None:
            compiler.set_force_strict_by_name(forced_strict_name)

        module = compiler.check_source(code, f"{mod_name}.py", mod_name, [])
        return module

    def test_pyi_stub(self) -> None:
        with sandbox() as sbx:
            sbx.write_file(
                "a.pyi",
                """
                class C:
                    x = 1
                """,
            )
            code = """
            import __strict__
            from a import C
            x = C.x
            """
            m = self.analyze(code, import_path=[str(sbx.root)], allow_list_prefix=["a"])
            self.assertTrue(m.is_valid)
            self.assertEqual(m.errors, [])

    def test_allowlisted_stub_incomplete(self) -> None:
        with sandbox() as sbx:
            sbx.write_file(
                "a.pyi",
                """
                class C:
                    x = 1
                z = unknown.x
                """,
            )
            code = """
            import __strict__
            from a import C, z
            x = C.x
            """
            m = self.analyze(code, import_path=[str(sbx.root)], allow_list_prefix=["a"])
            self.assertTrue(m.is_valid)
            self.assertEqual(m.errors, [])

    def test_allowlisted_stub_qualified_access(self) -> None:
        with sandbox() as sbx:
            sbx.write_file(
                "b/a.py",
                """
                class C:
                    x = 1
                """,
            )
            code = """
            import __strict__
            import b.a
            x = b.a.C.x
            """
            m = self.analyze(
                code, import_path=[str(sbx.root)], allow_list_prefix=["b.a", "b"]
            )
            self.assertTrue(m.is_valid)
            self.assertEqual(m.errors, [])

    def test_allowlist_regex_stub_qualified_access(self) -> None:
        with sandbox() as sbx:
            sbx.write_file(
                "matchme/a.py",
                """
                class C:
                    x = 1
                """,
            )
            code = """
            import __strict__
            import matchme.a
            x = matchme.a.C.x
            """
            m = self.analyze(
                code,
                import_path=[str(sbx.root)],
                allow_list_regex=[".*match.*"],
            )
            self.assertTrue(m.is_valid)
            self.assertEqual(m.errors, [])

    def test_allowlisted_stub_qualified_access_nested(self) -> None:
        with sandbox() as sbx:
            sbx.write_file(
                "c/b/a.py",
                """
                import __strict__
                class C:
                    x = 1
                """,
            )
            code = """
            import __strict__
            import c.b.a
            x = c.b.a.C.x
            """
            m = self.analyze(
                code, import_path=[str(sbx.root)], allow_list_prefix=["c", "c.b"]
            )
            self.assertTrue(m.is_valid)
            self.assertEqual(m.errors, [])

    def test_allowlisted_stub_qualified_access_not_fully_allowlisted(self) -> None:
        """
        Module b is not on the allow list and therefore accessing
        qualified names from it is not allowed
        """
        with sandbox() as sbx:
            sbx.write_file(
                "b/a.py",
                """
                class C:
                    x = 1
                """,
            )
            code = """
            import __strict__
            import b.a
            x = b.a.C.x
            """
            m = self.analyze(
                code, import_path=[str(sbx.root)], allow_list_prefix=["b.a"]
            )
            self.assertTrue(m.is_valid)
            self.assertTrue(len(m.errors) > 0)
            err = "Module-level attribute access on non-strict value '<imported module b>.a' is prohibited."
            self.assertEqual(m.errors[0][0], err)

    def test_static(self) -> None:
        code = """
            import __static__
            from __static__ import int8, box

            def f():
                x: int8 = 0
                return box(x)

            x = f()
        """

        m = self.analyze(code)
        self.assertTrue(m.is_valid)
        self.assertEqual(m.errors, [])

    def test_forced_strict(self) -> None:
        with sandbox() as sbx:
            sbx.write_file(
                "a.py",
                """
                class C:
                    x = 1
                """,
            )
            code = """
            import __strict__
            import a
            x = a.C.x
            """
            m = self.analyze(code, import_path=[str(sbx.root)], forced_strict_name="a")
            self.assertTrue(m.is_valid)
            self.assertEqual(m.errors, [])

    def test_forced_strict_only_by_name(self) -> None:
        with sandbox() as sbx:
            sbx.write_file(
                "a.py",
                """
                class C:
                    x = 1
                """,
            )
            sbx.write_file(
                "b.py",
                """
                class C:
                    x = 1
                """,
            )
            code = """
            import __strict__
            import a
            import b
            x = a.C.x
            y = b.C
            """
            m = self.analyze(code, import_path=[str(sbx.root)], forced_strict_name="a")
            err = "Module-level attribute access on non-strict value '<imported module b>.C' is prohibited."
            self.assertTrue(m.is_valid)
            self.assertEqual(m.errors[0][0], err)


@final
class GetModuleKindTest(StrictTestBase):
    def _get_kind(self, code: str, mod_name: str = "mod"):
        code = dedent(code)
        compiler = StrictModuleLoader([], "", [], [], True)
        module = compiler.check_source(code, f"{mod_name}.py", mod_name, [])

        return module.module_kind, module.errors

    def _get_kind_and_flags(self, code: str, mod_name: str = "mod"):
        code = dedent(code)
        compiler = StrictModuleLoader([], "", [], [], True)

        module = compiler.check_source(code, f"{mod_name}.py", mod_name, [])
        self.assertTrue(module.is_valid)

        flags = FlagExtractor().get_flags(ast.parse(code))
        return module.module_kind, module.errors, flags

    def test_strict_flag(self):
        code = """
        import __strict__
        x = 1
        """
        kind, _, flags = self._get_kind_and_flags(code)
        self.assertEqual(kind, 1)
        self.assertTrue(flags.is_strict)
        self.assertFalse(flags.is_static)

    def test_static_flag(self):
        code = """
        import __static__
        x = 1
        """
        kind, _, flags = self._get_kind_and_flags(code)
        self.assertEqual(kind, 2)
        self.assertTrue(flags.is_static)
        self.assertFalse(flags.is_strict)

    def test_no_flag(self):
        code = """
        x = 1
        """
        kind, _, flags = self._get_kind_and_flags(code)
        self.assertEqual(kind, 0)
        self.assertFalse(flags.is_static)
        self.assertFalse(flags.is_strict)

    def test_flag_after_doc(self):
        code = """
        '''First docstring.'''
        import __strict__
        """
        kind, _, flags = self._get_kind_and_flags(code)
        self.assertEqual(kind, 1)
        self.assertTrue(flags.is_strict)
        self.assertFalse(flags.is_static)

    def test_flag_after_doc_comment(self):
        code = """
        '''First docstring.'''
        # comment
        import __strict__
        """
        kind, _, flags = self._get_kind_and_flags(code)
        self.assertEqual(kind, 1)
        self.assertTrue(flags.is_strict)
        self.assertFalse(flags.is_static)

    def test_flag_after_future_import(self):
        code = """
        from __future__ import annotations
        import __strict__
        """
        kind, _, flags = self._get_kind_and_flags(code)
        self.assertEqual(kind, 1)
        self.assertTrue(flags.is_strict)
        self.assertFalse(flags.is_static)

    def test_flag_in_functions(self):
        code = """
        def f():
            import __strict__
        """
        kind, _ = self._get_kind(code)
        self.assertEqual(kind, 0)

    def test_flag_in_class(self):
        code = """
        class Foo:
            import __strict__
            pass
        """
        kind, _ = self._get_kind(code)
        self.assertEqual(kind, 0)
