from itertools import permutations

from .common import StaticTestBase


class TopLevelTests(StaticTestBase):
    def test_cross_type_redefine(self) -> None:
        cases = [
            ("class", "class x: pass"),
            ("func", "def x(): pass"),
            ("int", "x: int = 1"),
            ("str", "x: str = 'foo'"),
        ]
        for (name1, code1), (name2, code2) in permutations(cases, 2):
            with self.subTest(first=name1, second=name2):
                codestr = f"{code1}\n{code2}"
                msg = "Cannot redefine local variable x"
                if (name1, name2) == ("class", "func"):
                    msg = "function conflicts with other member x"
                self.type_error(codestr, msg, at=code2)
