from compiler.static.types import TypedSyntaxError

from .common import StaticTestBase


class AnnotatedTests(StaticTestBase):
    def test_parse(self) -> None:
        codestr = """
            from typing_extensions import Annotated
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
            from typing_extensions import Annotated
            def foo() -> Annotated[str, "aaaaa"]:
                return 0
            reveal_type(foo())
        """
        with self.assertRaisesRegex(
            TypedSyntaxError,
            r"return type must be str, not Literal\[0\]",
        ):
            self.compile(codestr)

    def test_parse_nested(self) -> None:
        codestr = """
            from typing import Optional
            from typing_extensions import Annotated
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
            from typing import Optional
            from typing_extensions import Annotated
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
            from typing import Optional
            from typing_extensions import Annotated
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
            from typing import Optional
            from typing_extensions import Annotated
            def foo() -> Optional[Annotated[int,]]:
                return 0
            reveal_type(foo())
        """
        with self.assertRaisesRegex(
            TypedSyntaxError,
            r"Annotated types must be parametrized by at least one annotation",
        ):
            self.compile(codestr)
