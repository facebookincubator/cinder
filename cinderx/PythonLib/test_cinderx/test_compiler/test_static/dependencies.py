import itertools
from unittest import TestCase

from cinderx.compiler.static.module_table import find_transitive_deps

from .common import StaticTestBase


class DependencyTrackingTests(StaticTestBase):
    def assertDep(
        self,
        deps: dict[str, set[tuple[str, str]]],
        key: str,
        value: set[tuple[str, str]],
    ) -> None:
        self.assertIn(key, deps)
        self.assertEqual(deps[key], value)

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
                expected = {("b", "B")}
                if not from_import:
                    expected.add(("a", "b1" if alias else "b"))
                self.assertDep(compiler.modules["a"].decl_deps, "C", expected)

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
                expected = {("b", "x")}
                if not from_import:
                    expected.add(("a", "b1" if alias else "b"))
                self.assertDep(compiler.modules["a"].bind_deps, "f", expected)

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

    def test_function_argument_annotation_is_decl_dep(self) -> None:
        """Function arg annotations are decl deps.

        They are decl deps because they should create transitive dependencies:
        if modC calls modB.func(), and modB.func() has an annotation from modA,
        then modC has a dependency on modA. (If modA switches from non-static to
        static; compiling modC may now need additional CAST to make the call to
        modB.func() safe.)
        """
        acode = """
            class A:
                pass

            class A2:
                pass
        """
        bcode = """
            from a import A1, A2

            def f(a1: A1, /, a2: A2):
                pass

            def g():
                f(A1(), A2())
        """
        ccode = """
            from b import f, A1, A2

            def g():
                f(A1(), A2())
        """
        for direct in [True, False]:
            with self.subTest(direct=direct):
                compiler = self.compiler(a=acode, b=bcode, c=ccode)
                compiler.compile_module("b" if direct else "c")
                self.assertDep(
                    compiler.modules["b"].decl_deps, "f", {("a", "A1"), ("a", "A2")}
                )

    def test_function_return_annotation_is_decl_dep(self) -> None:
        """Function annotations are decl deps.

        Return annotations are also decl deps: consider modC calls modB.func(),
        which returns modA.A. If modA switches from non-static to static, modC
        requires recompilation; it now has a static type rather than dynamic in
        the code following the call.
        """
        acode = """
            class A:
                pass
        """
        for from_import in [True, False]:
            if from_import:
                bcode = """
                    from a import A

                    def f() -> A:
                        return A()

                    def g():
                        f()
                """
            else:
                bcode = """
                    import a

                    def f() -> a.A:
                        return a.A()

                    def g():
                        f()
                """
            ccode = """
                from b import f

                def g():
                    f()
            """
            for direct in [True, False]:
                with self.subTest(direct=direct, from_import=from_import):
                    compiler = self.compiler(a=acode, b=bcode, c=ccode)
                    compiler.compile_module("b" if direct else "c")
                    expected = {("a", "A")}
                    if not from_import:
                        expected.add(("b", "a"))
                    self.assertDep(compiler.modules["b"].decl_deps, "f", expected)

    def test_module_level_annassign_is_decl_dep(self) -> None:
        """An annotated assignment at module level is a decl dep."""
        acode = """
            from b import B

            x: B = B()
        """
        bcode = """
            class B:
                pass
        """
        ccode = """
            from a import x
        """
        for direct in [True, False]:
            with self.subTest(direct=direct):
                compiler = self.compiler(a=acode, b=bcode, c=ccode)
                compiler.compile_module("a" if direct else "c")
                self.assertDep(compiler.modules["a"].decl_deps, "x", {("b", "B")})

    def test_class_level_annassign_is_decl_dep(self) -> None:
        """An annotated assignment at class level is a decl dep."""
        acode = """
            from b import B

            class C:
                x: B
        """
        bcode = """
            class B:
                pass
        """
        ccode = """
            from a import C
            C.x
        """
        for direct in [True, False]:
            with self.subTest(direct=direct):
                compiler = self.compiler(a=acode, b=bcode, c=ccode)
                compiler.compile_module("a" if direct else "c")
                self.assertDep(compiler.modules["a"].decl_deps, "C", {("b", "B")})

    def test_init_annassign_is_decl_dep(self) -> None:
        """An annotated assignment in __init__ is a decl dep.

        It declares an attribute slot on the class, which is public API.
        """
        acode = """
            from b import B

            class C:
                def __init__(self):
                    self.x: B = B()
        """
        bcode = """
            class B:
                pass
        """
        ccode = """
            from a import C
        """
        for direct in [True, False]:
            with self.subTest(direct=direct):
                compiler = self.compiler(a=acode, b=bcode, c=ccode)
                compiler.compile_module("a" if direct else "c")
                self.assertDep(compiler.modules["a"].decl_deps, "C", {("b", "B")})

    def test_function_scope_annassign_is_bind_dep(self) -> None:
        """An annotated assignment inside a function is a bind dep."""
        acode = """
            from b import B

            def f():
                x: B = B()
        """
        bcode = """
            class B:
                pass
        """
        compiler = self.compiler(a=acode, b=bcode)
        compiler.compile_module("a")
        self.assertNotIn("f", compiler.modules["a"].decl_deps)
        self.assertDep(compiler.modules["a"].bind_deps, "f", {("b", "B")})

    def test_cast_first_arg_is_bind_dep(self) -> None:
        """Casts are bind deps.

        This is true even for casts used at module or class scope; in such cases
        the decl dep should come from an annotation (e.g. on the name the cast
        expression is being assigned to), not from the cast itself. Except in
        very specific cases (e.g. module-level Final[Any], which we handle), we
        don't infer decl types from the RHS.
        """
        acode = """
            from typing import cast
            from b import B

            def f():
                b = cast(B, None)
        """
        bcode = """
            class B:
                pass
        """
        compiler = self.compiler(a=acode, b=bcode)
        compiler.compile_module("a")
        self.assertNotIn("f", compiler.modules["a"].decl_deps)
        self.assertDep(
            compiler.modules["a"].bind_deps, "f", {("b", "B"), ("typing", "cast")}
        )

    def test_module_level_final_inference_is_decl_dep(self) -> None:
        """Module-level Final[Any] type inference creates a decl dep.

        The module-level Final is part of the public API of the module, so
        should create a transitive dep. And the result of the type inference
        depends on the referenced module.
        """
        for alias in [True, False]:
            with self.subTest(alias=alias):
                acode = f"""
                    from typing import Any, Final
                    from b import x{' as x1' if alias else ''}

                    y: Final[Any] = x{'1' if alias else ''}
                """
                bcode = """
                    x: int = 42
                """
                compiler = self.compiler(a=acode, b=bcode)
                compiler.compile_module("a")
                self.assertDep(
                    compiler.modules["a"].decl_deps,
                    "y",
                    {("b", "x"), ("typing", "Any"), ("typing", "Final")},
                )

    def test_dep_within_module(self) -> None:
        """Dependencies are also tracked between objects within the same module.

        If some other module depends on `f`, and `C` depends on something from
        another module, we need to follow that transitive chain across the
        modules, through the intra-module dependencies.

        """
        code = """
            class C:
                pass

            class D:
                c: C

            def f(d: D) -> None:
                pass

            def g():
                f(D())
        """
        compiler = self.compiler(mod=code)
        compiler.compile_module("mod")
        decl_deps = compiler.modules["mod"].decl_deps
        self.assertDep(decl_deps, "f", {("mod", "D")})
        self.assertDep(decl_deps, "D", {("mod", "C")})

    def test_dep_on_unknown_module(self) -> None:
        """We record deps on non-static modules; they could become static."""
        for from_import in [True, False]:
            if from_import:
                code = """
                    from unknown import U

                    class C(U):
                        pass
                """
            else:
                code = """
                    import unknown as unk

                    class C(unk.U):
                        pass
                """
            with self.subTest(from_import=from_import):
                compiler = self.compiler(mod=code)
                compiler.compile_module("mod")
                expected = {("unknown", "U")} if from_import else {("mod", "unk")}
                self.assertDep(compiler.modules["mod"].decl_deps, "C", expected)
                if not from_import:
                    self.assertDep(
                        compiler.modules["mod"].decl_deps, "unk", {("unknown", "<any>")}
                    )


class GetDependenciesTests(StaticTestBase):
    def assertDeps(self, compiler, mod: str, deps: set[str]) -> None:
        self.assertEqual(
            deps,
            {mod.name for mod in compiler.modules[mod].get_dependencies()},
        )

    def test_none(self) -> None:
        code = """
            class A:
                pass
        """
        compiler = self.compiler(a=code)
        compiler.compile_module("a")
        self.assertDeps(compiler, "a", set())

    def test_simple(self) -> None:
        code = """
            from b import B
        """
        compiler = self.compiler(a=code, b="class B: pass")
        compiler.compile_module("a")
        self.assertDeps(compiler, "a", {"b"})

    def test_follow_immediate_bind_dep(self) -> None:
        acode = """
            import b

            def f():
                return b.B()
        """
        compiler = self.compiler(a=acode, b="class B: pass")
        compiler.compile_module("a")
        self.assertDeps(compiler, "a", {"b"})

    def test_transitive_bind_dep(self) -> None:
        acode = """
            from b import f
        """
        bcode = """
            import c

            def f():
                return c.C()
        """
        compiler = self.compiler(a=acode, b=bcode, c="class C: pass")
        compiler.compile_module("a")
        self.assertDeps(compiler, "a", {"b"})

    def test_transitive_decl_dep(self) -> None:
        acode = """
            from b import f

            def g():
                return f()
        """
        bcode = """
            import c

            def f() -> c.C:
                return c.C()
        """
        compiler = self.compiler(a=acode, b=bcode, c="class C: pass")
        compiler.compile_module("a")
        self.assertDeps(compiler, "a", {"b", "c"})

    def test_dont_follow_deps_of_unused_item(self) -> None:
        acode = """
            from b import f

            def x():
                return f()
        """
        bcode = """
            import c, d

            def f() -> c.C:
                return c.C()

            def g() -> d.D:
                return d.D()
        """
        compiler = self.compiler(a=acode, b=bcode, c="class C: pass", d="class D: pass")
        compiler.compile_module("a")
        self.assertDeps(compiler, "a", {"b", "c"})

    def test_dep_on_unknown_module(self) -> None:
        code = """
            from unknown import U

            class C(U):
                pass
        """
        compiler = self.compiler(mod=code)
        compiler.compile_module("mod")
        self.assertDeps(compiler, "mod", {"unknown"})


class FindTransitiveDepsTests(TestCase):
    def test_empty(self) -> None:
        self.assertEqual(find_transitive_deps("a", {}), set())

    def test_no_external_deps(self) -> None:
        self.assertEqual(find_transitive_deps("a", {"a": {"x": {("a", "X")}}}), set())

    def test_simple(self) -> None:
        self.assertEqual(
            find_transitive_deps(
                "a",
                {"a": {"A": {("b", "B")}}},
            ),
            {"b"},
        )

    def test_transitive(self) -> None:
        self.assertEqual(
            find_transitive_deps(
                "a",
                {
                    "a": {"A": {("b", "B")}},
                    "b": {"B": {("c", "C")}},
                },
            ),
            {"b", "c"},
        )

    def test_multiple_transitive(self) -> None:
        self.assertEqual(
            find_transitive_deps(
                "a",
                {
                    "a": {
                        "x": {("b", "B"), ("c", "C")},
                        "y": {("d", "D")},
                    },
                    "b": {"B": {("e", "E"), ("f", "F")}},
                    "c": {"C": {("d", "D")}},
                    "d": {"D": {("e", "E")}, "D1": {("g", "G")}},
                },
            ),
            {"b", "c", "d", "e", "f"},
        )

    def test_cycle(self) -> None:
        self.assertEqual(
            find_transitive_deps(
                "a",
                {
                    "a": {"A": {("b", "B")}},
                    "b": {"B": {("a", "A")}},
                },
            ),
            {"b"},
        )
