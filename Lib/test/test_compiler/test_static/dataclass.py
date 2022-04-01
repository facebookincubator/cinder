from .common import StaticTestBase


class DataclassTests(StaticTestBase):
    def test_dataclasses_dataclass_is_dynamic(self) -> None:
        codestr = """
        from dataclasses import dataclass

        @dataclass
        class C:
            x: str

        reveal_type(C)
        """
        self.type_error(codestr, r"reveal_type\(C\): 'Type\[dynamic\]'")
