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
