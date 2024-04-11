from cinderx.compiler.static.types import TypedSyntaxError

from .common import StaticTestBase


class AnnotatedTests(StaticTestBase):
    def test_parse(self) -> None:
        codestr = """
            from typing import Annotated
            def foo() -> Annotated[int, "aaaaa"]:
                return 0
            reveal_type(foo())
        """
        with self.assertRaisesRegex(
            TypedSyntaxError,
            r"reveal_type\(foo\(\)\): 'int'",
        ):
            self.compile(codestr)

    def test_type_error(self) -> None:
        codestr = """
            from typing import Annotated
            def foo() -> Annotated[str, "aaaaa"]:
                return 0
            reveal_type(foo())
        """
        with self.assertRaisesRegex(
            TypedSyntaxError,
            r"return type must be Literal\[0\], not str",
        ):
            self.compile(codestr)

    def test_parse_nested(self) -> None:
        codestr = """
            from typing import Annotated, Optional
            def foo() -> Optional[Annotated[int, "aaaaa"]]:
                return 0
            reveal_type(foo())
        """
        with self.assertRaisesRegex(
            TypedSyntaxError,
            r"reveal_type\(foo\(\)\): 'Optional\[int\]'",
        ):
            self.compile(codestr)

    def test_subscript_must_be_tuple(self) -> None:
        codestr = """
            from typing import Annotated, Optional
            def foo() -> Optional[Annotated[int]]:
                return 0
            reveal_type(foo())
        """
        with self.assertRaisesRegex(
            TypedSyntaxError,
            r"Annotated types must be parametrized by at least one annotation",
        ):
            self.compile(codestr)

    def test_subscript_must_be_tuple(self) -> None:
        codestr = """
            from typing import Annotated, Optional
            def foo() -> Optional[Annotated[int]]:
                return 0
            reveal_type(foo())
        """
        with self.assertRaisesRegex(
            TypedSyntaxError,
            r"Annotated types must be parametrized by at least one annotation",
        ):
            self.compile(codestr)

    def test_subscript_must_contain_annotation(self) -> None:
        codestr = """
            from typing import Annotated, Optional
            def foo() -> Optional[Annotated[int,]]:
                return 0
            reveal_type(foo())
        """
        with self.assertRaisesRegex(
            TypedSyntaxError,
            r"Annotated types must be parametrized by at least one annotation",
        ):
            self.compile(codestr)

    def test_exact_type_annotations_reveal_type(self) -> None:
        codestr = """
            from typing import Annotated

            class C:
                def m(self) -> int:
                    return 0

            def foo(c: Annotated[C, "Exact"]) -> int:
                reveal_type(c)
        """
        self.type_error(codestr, r"reveal_type\(c\): 'Exact\[<module>.C\]'")

    def test_exact_type_annotations_invoke(self) -> None:
        codestr = """
            from typing import Annotated

            class C:
                def m(self) -> int:
                    return 0

            class D(C):
                def m(self) -> int:
                    return 1

            def foo(c: Annotated[C, "Exact"]) -> int:
                return c.m()

            def bar(c: C) -> int:
                return c.m()
        """
        with self.in_module(codestr) as mod:
            self.assertInBytecode(mod.foo, "INVOKE_FUNCTION")
            self.assertNotInBytecode(mod.foo, "INVOKE_METHOD")
            self.assertInBytecode(mod.bar, "INVOKE_METHOD")

    def test_exact_type_annotations_inexact_parameter_type_error(self) -> None:
        codestr = """
            from typing import Annotated

            class C:
                def m(self) -> int:
                    return 0

            class D(C):
                def m(self) -> int:
                    return 1

            def foo(c: Annotated[C, "Exact"]) -> int:
                return c.m()

            def bar(c: C) -> int:
                return foo(c)
        """
        # TODO this needs exactness threaded back
        self.type_error(
            codestr,
            r"<module>.C received for positional arg 'c', expected Exact\[<module>.C\]",
        )

    def test_exact_type_annotations_nonstatic_subclass_runtime_type_error(self) -> None:
        codestr = """
            from typing import final
            from typing import Annotated

            class C:
                def m(self) -> int:
                    return 0

            class D(C):
                def m(self) -> int:
                    return 1

            def foo(c: Annotated[C, "Exact"]) -> int:
                return c.m()
        """
        with self.in_module(codestr) as mod:
            d = mod.D()
            with self.assertRaisesRegex(
                TypeError, "foo expected 'C' for argument c, got 'D'"
            ):
                mod.foo(d)

    def test_exact_type_annotations_nonstatic_subclass_runtime_type_error(self) -> None:
        codestr = """
            from typing import Annotated

            class C:
                def m(self) -> int:
                    return 0

            class D(C):
                def m(self) -> int:
                    return 1

            def foo(c: Annotated[C, "Exact"]) -> int:
                return c.m()
        """
        with self.in_module(codestr) as mod:
            d = mod.D()
            with self.assertRaisesRegex(
                TypeError, "foo expected 'C' for argument c, got 'D'"
            ):
                mod.foo(d)

    def test_exact_does_not_allow_literals(self) -> None:
        codestr = """
            from typing import Annotated

            x: Annotated[int, "Exact"] = 3
            reveal_type(x)
        """
        self.revealed_type(codestr, "Exact[int]")

    def test_exact_final_type_error_on_subclass_assignment(self) -> None:
        codestr = """
            from typing import Annotated, Final

            class C:
               pass

            class D(C):
               pass

            x: Final[Annotated[C, "Exact"]] = D()
        """
        self.type_error(
            codestr,
            r"type mismatch: Exact\[<module>.D\] cannot be assigned to Exact\[<module>.C\]",
        )

    def test_exact_final_exact_assignment_ok(self) -> None:
        codestr = """
            from typing import Annotated, Final

            class C:
               pass

            class D(C):
               pass

            x: Final[Annotated[C, "Exact"]] = C()
            x = C()
        """
        self.type_error(codestr, "Cannot assign to a Final variable", at="x = C()")

    def test_exact_float_is_dynamic(self) -> None:
        codestr = """
            from typing import Annotated

            class C:
                def m(self) -> int:
                    return 0

            def foo(c: Annotated[float, "Exact"]) -> int:
                reveal_type(c)
        """
        self.type_error(codestr, r"reveal_type\(c\): 'dynamic'")

    def test_exact_optional(self) -> None:
        codestr = """
            from typing import Annotated, Optional
            class C:
                pass

            class D(C):
                pass

            def foo(c: Annotated[Optional[C], "Exact"]) -> None:
                pass
        """
        with self.in_module(codestr) as mod:
            self.assertEqual(
                self.get_arg_check_types(mod.foo), (0, (mod.__name__, "C", "!", "?"))
            )
            self.assertEqual(mod.foo(mod.C()), None)
            self.assertEqual(mod.foo(None), None)
            with self.assertRaisesRegex(
                TypeError, "foo expected 'C' for argument c, got 'D'"
            ):
                mod.foo(mod.D())

    def test_cast_exact(self) -> None:
        codestr = """
            from typing import Annotated
            from __static__ import cast
            class C:
                pass

            class D(C):
                pass

            def foo(c: object) -> None:
                cast(Annotated[C, "Exact"], c)
        """
        with self.in_module(codestr) as mod:
            self.assertInBytecode(mod.foo, "CAST", (mod.__name__, "C", "!"))

            self.assertEqual(mod.foo(mod.C()), None)
            with self.assertRaisesRegex(TypeError, "expected exactly 'C', got 'D'"):
                mod.foo(mod.D())
