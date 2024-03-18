from .common import StaticTestBase


class MatchTests(StaticTestBase):
    def test_match_simple(self) -> None:
        codestr = """
            def f(i: int | None) -> int:
                s: str | None = None
                match i:
                    case 1:
                        pass
                    case 2:
                        pass
                    case 3:
                        pass
                reveal_type(s)
          """
        self.revealed_type(codestr, "Exact[None]")

    def test_match_adds_type(self) -> None:
        codestr = """
            def f(i: int | None) -> int:
                s: str | None = None
                match i:
                    case 1:
                        s = "foo"
                    case 2:
                        s = None
                    case 3:
                        pass
                reveal_type(s)
          """
        self.revealed_type(codestr, "Optional[str]")

    def test_match_resets_branch_state(self) -> None:
        codestr = """
            def f(i: int | None) -> int:
                s: str | None = None
                match i:
                    case 1:
                        s = "bar"
                    case 2:
                        reveal_type(s)
          """
        self.revealed_type(codestr, "Exact[None]")

    def test_match_case_pollutes_later_case(self) -> None:
        codestr = """
            def f(i: int | None) -> int:
                s: str | None = None
                match i:
                    case 1:
                        pass
                    case 2:
                        s = "foo"
                    case 3:
                        reveal_type(s)
        """
        self.revealed_type(codestr, "Exact[None]")

    def test_match_all_terminal(self) -> None:
        codestr = """
            def f(i: int | None) -> int:
                s: str | None = None
                match i:
                    case 1:
                        s = "foo"
                        return 1
                    case 2:
                        return 2
                    case 3:
                        return 3
                reveal_type(s)
          """
        self.revealed_type(codestr, "Exact[None]")

    def test_match_ignore_return(self) -> None:
        codestr = """
            def f(i: int | None) -> int:
                s: str | None = None
                match i:
                    case 1:
                        s = "foo"
                        return
                    case 2:
                        s = None
                    case 3:
                        pass
                reveal_type(s)
        """
        self.revealed_type(codestr, "Exact[None]")

    def test_match_parse_breakorcontinue(self) -> None:
        codestr = """
            def f(i: int | None) -> int:
                s: str | None = None
                while True:
                    match i:
                        case 1:
                            s = "foo"
                            break
                        case 2:
                            s = None
                        case 3:
                            pass
                    reveal_type(s)
        """
        self.revealed_type(codestr, "Optional[str]")

    def test_match_invalid_assignment(self) -> None:
        codestr = """
            def f(i: int | None) -> int:
                s: str | None = None
                match i:
                    case 1:
                        s = 1
                    case 2:
                        s = None
                    case 3:
                        pass
                reveal_type(s)
        """
        self.type_error(
            codestr,
            r"type mismatch: Literal\[1\] cannot be assigned to Optional\[str\]",
        )

    def test_match_nonexhaustive(self) -> None:
        codestr = """
            def f(i: int | None) -> int:
                s: str | None = None
                match i:
                    case 1:
                        s = "foo"
                    case 2:
                        s = "bar"
                reveal_type(s)
        """
        self.revealed_type(codestr, "Optional[str]")

    # TODO: If we ever support checking for exhaustiveness of match statements, this test *should* fail.
    # So you can replace the Optional[str] with Exact[str]
    def test_match_exhaustive_wildcard(self) -> None:
        codestr = """
            def f(i: int | None) -> int:
                s: str | None = None
                match i:
                    case 1:
                        s = "foo"
                    case _:
                        s = "bar"
                reveal_type(s)
        """
        self.revealed_type(codestr, "Optional[str]")

    # TODO: If we ever support checking for exhaustiveness of match statements, this test *should* fail.
    # So you can replace the Optional[str] with Exact[str]
    def test_match_exhaustive_all_patterns(self) -> bool:
        codestr = """
            def f(i: bool) -> int:
                s: str | None = None
                match i:
                    case True:
                        s = "foo"
                    case False:
                        s = "bar"
                reveal_type(s)
        """
        self.revealed_type(codestr, "Optional[str]")

    def test_match_walrus_assignment(self) -> None:
        codestr = """
            def f(i: int | None) -> int:
                s: str | None = None
                match (s := "foo"):
                    case 1:
                        pass
                    case _:
                        pass
                reveal_type(s)
        """
        self.revealed_type(codestr, "Exact[str]")

    # TODO: We can narrow the type of s to "Exact[str]" here based on casing
    # if we add this analysis in the type binder.
    def test_match_value(self) -> None:
        codestr = """
            def f(s: str | None) -> int:
                match s:
                    case "relevant":
                        reveal_type(s)
                        pass
        """
        self.revealed_type(codestr, "Optional[str]")

    # TODO: We can narrow the type of s to "Exact[None]" here based on casing
    # if we add this analysis in the type binder.
    def test_match_singleton(self) -> None:
        codestr = """
            def f(s: str | None) -> int:
                match s:
                    case None:
                        reveal_type(s)
                        pass
        """
        self.revealed_type(codestr, "Optional[str]")

    def test_match_sequence(self) -> None:
        codestr = """
            def f() -> int:
                l: list[int] = [1, 2, 3]
                match l:
                    case [1, 2]:
                        pass
                    case [1, 2, 3]:
                        reveal_type(l)
        """
        self.revealed_type(codestr, "Exact[list]")

    def test_match_sequence_assignment(self) -> None:
        codestr = """
            def f() -> int:
                l: list[int] = [1, 2, 3]
                match l:
                    case [1, 2, rest]:
                        reveal_type(rest)
                    case [1, 2, 3]:
                        pass
        """
        self.revealed_type(codestr, "dynamic")

    def test_match_sequence_empty(self) -> None:
        codestr = """
            def f() -> int:
                l: list[int] = [1, 2, 3]
                match l:
                    case []:
                        reveal_type(l)
        """
        self.revealed_type(codestr, "Exact[list]")

    def test_match_sequence_shadow(self) -> None:
        codestr = """
            def f() -> None:
                l: list[int] = [1, 2, 3]
                match l:
                    case [l, 2, 3]:
                        pass
        """
        f = self.find_code(self.compile(codestr, modname="foo"))
        self.assertInBytecode(f, "CAST", ("builtins", "list"))

    # TODO: We can narrow the type of `rest` to `Exact[list]` here with further analysis.
    def test_match_star_named(self) -> None:
        codestr = """
            def f() -> int:
                l: list[int] = [1, 2, 3]
                match l:
                    case [*rest]:
                        reveal_type(rest)
        """
        self.revealed_type(codestr, "dynamic")

    def test_match_star_unnamed(self) -> None:
        codestr = """
            def f() -> int:
                l: list[int] = [1, 2, 3]
                match l:
                    case [*_]:
                        reveal_type(_)
        """
        self.type_error(codestr, r"Name `_` is not defined")

    # TODO: We can narrow the type of `rest` to `Exact[list]` here with further analysis.
    def test_match_star_redefine(self) -> None:
        codestr = """
            def f() -> int:
                l: list[int] = [1, 2, 3]
                match l:
                    case [*rest]:
                        pass
                    case [*rest]:
                        reveal_type(rest)
        """
        self.revealed_type(codestr, "dynamic")

    def test_match_mapping_assignment(self) -> None:
        codestr = """
            from typing import Dict

            def f() -> int:
                d: Dict[int, str] = { 1: "foo", 2: "bar" }
                match d:
                    case { 1: f, 2: _ }:
                        reveal_type(f)
        """
        self.revealed_type(codestr, "dynamic")

    def test_match_mapping_spillover(self) -> None:
        codestr = """
            from typing import Dict

            def f() -> int:
                d: Dict[int, str] = { 1: "foo", 2: "bar" }
                match d:
                    case { 1: f, 2: _ }:
                        pass
                    case { 1: _, 2: _ }:
                        reveal_type(f)
        """

        self.type_error(codestr, "Name `f` is not defined")

    # TODO: We can narrow the type of `rest` to `Exact[dict]` here with further analysis.
    def test_match_mapping_has_rest(self) -> None:
        codestr = """
            from typing import Dict

            def f() -> int:
                d: Dict[int, str] = { 1: "foo", 2: "bar" }
                match d:
                    case { 1: _, 2: g, **rest }:
                        reveal_type(rest)
        """
        self.revealed_type(codestr, "dynamic")

    def test_match_mapping_has_rest_spillover(self) -> None:
        codestr = """
            from typing import Dict

            def f() -> int:
                d: Dict[int, str] = { 1: "foo", 2: "bar" }
                match d:
                    case { 1: _, 2: _, **rest }:
                        pass
                    case { 1: _, 2: _ }:
                        reveal_type(rest)
        """
        self.type_error(codestr, "Name `rest` is not defined")

    def test_match_mapping_redefine(self) -> None:
        codestr = """
            from typing import Dict

            def f() -> int:
                d: Dict[int, str] = { 1: "foo", 2: "bar" }
                match d:
                    case { 1: _, 2: g }:
                        pass
                    case { 1: _, 2: g }:
                        reveal_type(g)
        """
        self.revealed_type(codestr, "dynamic")

    def test_match_mapping_nested_pattern(self) -> None:
        codestr = """
            from typing import Dict

            def f() -> int:
                d: Dict[int, str] = { 1: "foo", 2: "bar" }
                match d:
                    case { 1: _, 2: _ }:
                        pass
                    case { 1: _, 2: { 1: _, 2: f } }:
                        reveal_type(f)
        """
        self.revealed_type(codestr, "dynamic")

    def test_match_mapping_shadow(self) -> None:
        codestr = """
            from typing import Dict

            def f() -> None:
                d: Dict[int, str] = { 1: "foo", 2: "bar" }
                match d:
                    case { 1: _, 2: d }:
                        pass
        """
        f = self.find_code(self.compile(codestr, modname="foo"))
        self.assertInBytecode(f, "CAST", ("builtins", "dict"))

    # TODO: We can narrow the type of `q` to `int64` here with further analysis.
    def test_match_class(self) -> None:
        codestr = """
            from __static__ import int64
            from dataclasses import dataclass

            @dataclass
            class Point2D:
                x: int64
                y: int64

            def f() -> int:
                p: Point2D = Point2D(x=0, y=0)
                match p:
                    case Point2D(x=q):
                        reveal_type(q)
        """
        self.revealed_type(codestr, "dynamic")

    def test_match_class_shadow(self) -> None:
        codestr = """
            from __static__ import int64
            from dataclasses import dataclass

            @dataclass
            class Point2D:
                x: int64
                y: int64

            def f() -> None:
                p: Point2D = Point2D(x=0, y=0)
                match p:
                    case Point2D(x=p):
                        pass
        """
        f = self.find_code(self.compile(codestr, modname="foo"), name="f")
        self.assertInBytecode(f, "CAST", ("foo", "Point2D"))

    # TODO: We can narrow the type of `q` to `int64` here with further analysis.
    def test_match_class_redefine(self) -> None:
        codestr = """
            from __static__ import int64
            from dataclasses import dataclass

            @dataclass
            class Point2D:
                x: int64
                y: int64

            def f() -> int:
                p: Point2D = Point2D(x=0, y=0)
                match p:
                    case Point2D(x=q):
                        pass
                    case Point2D(x=q):
                        reveal_type(q)
         """
        self.revealed_type(codestr, "dynamic")

    # TODO: We can narrow the type of `q` to `int64` here with further analysis.
    def test_match_class_nested(self) -> None:
        codestr = """
            from __static__ import int64
            from dataclasses import dataclass

            @dataclass
            class Point:
                x: int64

            @dataclass
            class Point2D:
                x: Point
                y: int64

            def f() -> int:
                p: Point2D = Point2D(x=Point(x=0), y=0)
                match p:
                    case Point2D(x=Point(x=q)):
                        reveal_type(q)
        """
        self.revealed_type(codestr, "dynamic")

    # TODO: We can narrow the type of `t` to `Optional[str]` with further
    # analysis
    def test_match_as(self) -> None:
        codestr = """
            def f(s: str | None) -> int:
                match s:
                    case t:
                        reveal_type(t)
        """
        self.revealed_type(codestr, "dynamic")

    # TODO: We can narrow the type of `t` to `Optional[str]` with further
    # analysis
    def test_match_as_keyword_only(self) -> None:
        codestr = """
            def f(s: str | None) -> int:
                match s:
                    case _ as t:
                        reveal_type(t)
        """
        self.revealed_type(codestr, "dynamic")

    # TODO: We can narrow the types of `t` and `u` to `Optional[str]` with
    # further analysis
    def test_match_as_and_keyword(self) -> None:
        initial_codestr = """
            def f(s: str | None) -> int:
                match s:
                    case t as u:
        """
        self.revealed_type(
            initial_codestr
            + """
                        reveal_type(t)
        """,
            "dynamic",
        )
        self.revealed_type(
            initial_codestr
            + """
                        reveal_type(u)
        """,
            "dynamic",
        )

    def test_match_as_shadow(self) -> None:
        codestr = """
            def f() -> None:
                s: str = "foo"
                match s:
                    case 13 as s:
                        print(s)
        """
        # This should cast `13` to a string, but will result in a runtime error.
        f = self.find_code(self.compile(codestr, modname="foo"))
        self.assertInBytecode(f, "CAST", ("builtins", "str"))

    # TODO: We can narrow `t` to `str | None`
    def test_match_as_redefine(self) -> None:
        codestr = """
            def f() -> int:
                s: str | None = "foo"
                match s:
                    case _ as t:
                        pass
                    case _ as t:
                        reveal_type(t)
        """
        self.revealed_type(codestr, "dynamic")

    def test_match_as_unavailable_after_case(self) -> None:
        codestr = """
            def f() -> int:
                s: str | None = "foo"
                match s:
                    case f:
                        pass
                    case g:
                        pass
                reveal_type(f)
        """
        # At runtime `f` will actually still be defined after the match
        # statement, if the appropriate case matched. But in general
        # we report possibly-undefined as undefined in Static Python.
        self.type_error(codestr, "Name `f` is not defined")

    # TODO: Can narrow to `Exact[list]`
    def test_match_as_with_sequence(self) -> None:
        codestr = """
            from typing import List

            def f() -> int:
                l: List[int] = [1, 2, 3]
                match l:
                    case [*_] as m:
                        reveal_type(m)
        """
        self.revealed_type(codestr, "dynamic")

    # TODO: Can narrow to `Exact[dict]`
    def test_match_as_with_mapping(self) -> None:
        codestr = """
            from typing import Dict

            def f() -> int:
                m: Dict[int, str] = { 1: "foo", 2: "bar" }
                match m:
                    case { 1: _, 2: _ } as n:
                        reveal_type(n)
        """
        self.revealed_type(codestr, "dynamic")

    # TODO: Can narrow to `Exact[Point2D]`
    def test_match_as_with_class(self) -> None:
        codestr = """
        from __static__ import int64
        from dataclasses import dataclass

        @dataclass
        class Point2D:
            x: int64
            y: int64

        def f() -> int:
            p: Point2D = Point2D(x=0, y=0)
            match p:
                case Point2D(_) as r:
                    reveal_type(r)
        """
        self.revealed_type(codestr, "dynamic")

    def test_match_or_parses(self) -> None:
        codestr = """
            def f() -> int:
                s: str | None = "foo"
                match s:
                    case "foo" | "bar":
                        reveal_type(s)
        """
        self.revealed_type(codestr, "Exact[str]")

    # TODO: We can narrow `t` to `Exact[str]`
    def test_match_or_with_as(self) -> None:
        codestr = """
            def f() -> int:
                s: str | None = "foo"
                match s:
                    case "foo" | "bar" as t:
                        reveal_type(t)
        """
        self.revealed_type(codestr, "dynamic")

    # TODO: We can narrow `t` to `Exact[str]`
    def test_match_or_with_as_redefine(self) -> None:
        codestr = """
            def f() -> int:
                s: str | None = "foo"
                match s:
                    case "foo" | "bar" as t:
                        pass
                    case "foo" | "bar" as t:
                        reveal_type(t)
        """
        self.revealed_type(codestr, "dynamic")

    def test_match_or_with_sequence_and_class(self) -> None:
        codestr = """
        from __static__ import int64
        from dataclasses import dataclass

        @dataclass
        class Point2D:
            x: int64
            y: int64

        def f() -> int:
            p: Point2D = Point2D(x=0, y=0)
            match p:
                case Point2D(_) | [*_] as q:
                    reveal_type(q)
        """
        self.revealed_type(codestr, "dynamic")

    # TODO: We can narrow `x` to `Exact[int]` with further analysis.
    def test_match_guard(self) -> None:
        codestr = """
            def f(x: int | None) -> int:
                match x:
                    case y if x is not None:
                        reveal_type(x)
        """
        self.revealed_type(codestr, "Optional[int]")

    def test_match_guard_walrus_side_effect(self) -> None:
        codestr = """
            def f(b: bool) -> int:
                x = 1
                match b:
                    case True if (x := None):
                        pass
                    case False:
                        reveal_type(x)
        """
        self.revealed_type(codestr, "Optional[Literal[1]]")
