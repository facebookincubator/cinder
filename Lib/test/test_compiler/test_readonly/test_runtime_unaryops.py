import unittest

from .common import ReadonlyTestBase


class RuntimeUnaryOpsTests(ReadonlyTestBase):
    def _compile_and_run(self, code: str, func: str) -> None:
        compiled = self.compile_and_run(code)
        f = compiled[func]
        return f()

    def test_neg_mutable_arg(self) -> None:
        code = """
        class TestObj:
            @readonly_func
            def __init__(self, val: Readonly[int]):
                self.value = val

            @readonly_func
            def __neg__(self: TestObj) -> Readonly[TestObj]:
                return TestObj(-self.value)

        @readonly_func
        def f() -> Readonly[int]:
            a = readonly(TestObj(3))
            c = readonly(-a)
            return c.value
        """
        with self.assertImmutableErrors(
            [
                (
                    13,
                    "Attempted to pass a readonly arguments to an operation that expects mutable parameters.",
                    (),
                ),
            ]
        ):
            c = self._compile_and_run(code, "f")
            self.assertEqual(c, -3)

    def test_neg_readonly_return(self) -> None:
        code = """
        class TestObj:
            @readonly_func
            def __init__(self, val: Readonly[int]):
                self.value = val

            @readonly_func
            def __neg__(self: Readonly[TestObj]) -> Readonly[TestObj]:
                return TestObj(-self.value)

        @readonly_func
        def f() -> int:
            a = TestObj(3)
            c = -a
            return c.value
        """
        with self.assertImmutableErrors(
            [
                (14, "Operator returns readonly, but expected mutable.", ()),
            ]
        ):
            c = self._compile_and_run(code, "f")
            self.assertEqual(c, -3)

    def test_neg_working(self) -> None:
        code = """
        class TestObj:
            @readonly_func
            def __init__(self, val: Readonly[int]):
                self.value = val

            @readonly_func
            def __neg__(self: Readonly[TestObj]) -> Readonly[TestObj]:
                return TestObj(-self.value)

        @readonly_func
        def f() -> Readonly[int]:
            a = TestObj(3)
            c = readonly(-a)
            return c.value
        """
        with self.assertNoImmutableErrors():
            c = self._compile_and_run(code, "f")
            self.assertEqual(c, -3)
