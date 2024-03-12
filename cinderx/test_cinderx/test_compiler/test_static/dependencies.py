import itertools

from .common import StaticTestBase


class DependencyTrackingTests(StaticTestBase):
    def test_decl_dep(self) -> None:
        """A dependency of the API surface is recorded in `decl_deps`.

        Any user of `a.C` must be recompiled if `b.B` changes.

        """
        for alias, from_import in itertools.product([True, False], [True, False]):
            with self.subTest(alias=alias, from_import=from_import):
                if from_import:
                    acode = f"""
                        from b import B{' as B1' if alias else ''}

                        class C(B{'1' if alias else ''}):
                            pass
                    """
                else:
                    acode = f"""
                        import b{' as b1' if alias else ''}

                        class C(b{'1' if alias else ''}.B):
                            pass
                    """
                bcode = """
                    class B:
                        pass
                """
                compiler = self.compiler(a=acode, b=bcode)
                compiler.compile_module("a")
                self.assertDep(compiler.modules["a"].decl_deps, "C", {("b", "B")})

    def test_bind_dep(self) -> None:
        """A dependency used only internally is recorded in `bind_deps`.

        If `b.x` changes, then `a` needs to be recompiled, but another module
        importing and using `a.f` does not need to be recompiled.

        """
        for from_import, alias in itertools.product([True, False], [True, False]):
            with self.subTest(alias=alias, from_import=from_import):
                if from_import:
                    acode = f"""
                        from b import x{' as x1' if alias else ''}

                        def f() -> int:
                            return x{'1' if alias else ''}
                    """
                else:
                    acode = f"""
                        import b{' as b1' if alias else ''}

                        def f() -> int:
                            return b{'1' if alias else ''}.x
                    """
                bcode = """
                    x: int = 42
                """
                compiler = self.compiler(a=acode, b=bcode)
                compiler.compile_module("a")
                self.assertDep(compiler.modules["a"].bind_deps, "f", {("b", "x")})

    def test_module_level_import_is_decl_dep(self) -> None:
        """Every module-level import is a decl_dep.

        Some other module could be re-importing from this import (in other
        words: all imports in Python are de facto public API of your module),
        and we have to be able to track that dependency through the intermediate
        module.
        """
        for alias in [True, False]:
            with self.subTest(alias=alias):
                acode = f"""
                    from b import B{' as B1' if alias else ''}
                """
                bcode = """
                    class B:
                        pass
                """
                compiler = self.compiler(a=acode, b=bcode)
                compiler.compile_module("a")
                self.assertDep(
                    compiler.modules["a"].decl_deps,
                    "B1" if alias else "B",
                    {("b", "B")},
                )

    def test_inline_import_is_bind_dep(self) -> None:
        """Every inline import is a bind_dep.

        It doesn't affect the external-facing API of the module, but it does
        impact the codegen for this module.
        """
        for alias in [True, False]:
            with self.subTest(alias=alias):
                acode = f"""
                    def f() -> int:
                        from b import x{' as x1' if alias else ''}
                        return x{'1' if alias else ''}
                """
                bcode = """
                    x: int = 42
                """
                compiler = self.compiler(a=acode, b=bcode)
                compiler.compile_module("a")
                self.assertDep(compiler.modules["a"].bind_deps, "f", {("b", "x")})

    def test_nested_class(self) -> None:
        """A nested class records itself correctly in dependencies."""
        acode = f"""
            from b import B

            class Outer1:
                class Outer2:
                    class C(B):
                        pass
        """
        bcode = """
            class B:
                pass
        """
        compiler = self.compiler(a=acode, b=bcode)
        compiler.compile_module("a")
        self.assertDep(compiler.modules["a"].decl_deps, "Outer1.Outer2.C", {("b", "B")})

    def assertDep(
        self,
        deps: dict[str, set[tuple[str, str]]],
        key: str,
        value: set[tuple[str, str]],
    ) -> None:
        self.assertIn(key, deps)
        self.assertEqual(deps[key], value)
