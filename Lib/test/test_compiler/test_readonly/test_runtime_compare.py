import unittest

from .common import ReadonlyTestBase


class RuntimeCompareTests(ReadonlyTestBase):
    def _compile_and_run(self, code: str, func: str) -> None:
        compiled = self.compile_and_run(code)
        f = compiled[func]
        return f()

    def test_compare_eq_mutable_arg(self) -> None:
        code = """
        class TestObj:
            @readonly_func
            def __init__(self, val: int):
                self.value = val

            @readonly_func
            def __eq__(self: Readonly[TestObj], other: TestObj) -> bool:
                return self.value == other.value

        @readonly_func
        def f() -> bool:
            a = TestObj(3)
            b = readonly(TestObj(4))
            c = a == b
            return c
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
            self.assertEqual(c, False)

    def test_compare_eq_readonly_return(self) -> None:
        code = """
        class TestObj:
            @readonly_func
            def __init__(self, val: int):
                self.value = val

            @readonly_func
            def __eq__(self: Readonly[TestObj], other: TestObj) -> Readonly[bool]:
                return self.value == other.value

        @readonly_func
        def f() -> bool:
            a = TestObj(3)
            b = TestObj(4)
            c = a == b
            return c
        """
        with self.assertImmutableErrors(
            [
                (14, "Operator returns readonly, but expected mutable.", ()),
            ]
        ):
            c = self._compile_and_run(code, "f")
            self.assertEqual(c, False)

    def test_compare_eq_working(self) -> None:
        code = """
        class TestObj:
            @readonly_func
            def __init__(self, val: int):
                self.value = val

            @readonly_func
            def __eq__(self: Readonly[TestObj], other: Readonly[TestObj]) -> bool:
                return self.value == other.value

        @readonly_func
        def f() -> bool:
            a = TestObj(3)
            b = readonly(TestObj(4))
            c = a == b
            return c
        """
        with self.assertNoImmutableErrors():
            c = self._compile_and_run(code, "f")
            self.assertEqual(c, False)

    def test_compare_reverse_mutable_arg(self) -> None:
        code = """
        class TestObj:
            @readonly_func
            def __init__(self, val: int):
                self.value = val

            @readonly_func
            def __eq__(self: Readonly[TestObj], other: TestObj) -> bool:
                return self.value == other.value

        class TestObjB(TestObj):
            @readonly_func
            def __init__(self, val: int):
                super().__init__(val)

            @readonly_func
            def __eq__(self: TestObjB, other: Readonly[TestObj]) -> bool:
                return True

        @readonly_func
        def f() -> bool:
            a = TestObj(3)
            b = readonly(TestObjB(4))
            c = a == b
            return c
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
            self.assertEqual(c, True)

    def test_compare_reverse_fallback_mutable_arg(self) -> None:
        code = """
        class TestObj:
            @readonly_func
            def __init__(self, val: int):
                self.value = val

            @readonly_func
            def __eq__(self: Readonly[TestObj], other: TestObj) -> bool:
                return self.value == other.value

        class TestObjB(TestObj):
            @readonly_func
            def __init__(self, val: int):
                super().__init__(val)

            @readonly_func
            def __eq__(self: Readonly[TestObjB], other: Readonly[TestObj]) -> bool:
                return NotImplemented

        @readonly_func
        def f() -> bool:
            a = TestObj(3)
            b = readonly(TestObjB(4))
            c = a == b
            return c
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
            self.assertEqual(c, False)
