from .common import StaticTestBase


class TypedDictTests(StaticTestBase):
    def test_typeddict_is_dynamic(self):
        codestr = """
            from typing import TypedDict
            class Player(TypedDict):
                hp: int
                fp: int

            def f():
                x = Player(hp=1000, fp=500)
                reveal_type(x)
        """
        self.revealed_type(codestr, "dynamic")
