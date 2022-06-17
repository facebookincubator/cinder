import unittest

from .common import ReadonlyTestBase


class RuntimeBinopsTests(ReadonlyTestBase):
    def test_binop_add_mutable_arg(self) -> None:
        code = """
        class TestObj:
            @readonly_func
            def __init__(self, val: Readonly[int]):
                self.value = val

            @readonly_func
            def __add__(self: Readonly[TestObj], other: TestObj) -> Readonly[TestObj]:
                return TestObj(self.value + other.value)

        @readonly_func
        def f() -> Readonly[int]:
            a = TestObj(3)
            b = readonly(TestObj(4))
            c = a + b
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
            c = self.compile_and_call(code, "f")
            self.assertEqual(c, 7)

    def test_binop_add_readonly_return(self) -> None:
        code = """
        class TestObj:
            @readonly_func
            def __init__(self, val: Readonly[int]):
                self.value = val

            @readonly_func
            def __add__(self: TestObj, other: TestObj) -> Readonly[TestObj]:
                return TestObj(self.value + other.value)

        @readonly_func
        def f() -> int:
            a = TestObj(3)
            b = TestObj(4)
            c = a + b
            return c.value
        """
        with self.assertImmutableErrors(
            [
                (14, "Operator returns readonly, but expected mutable.", ()),
            ]
        ):
            c = self.compile_and_call(code, "f")
            self.assertEqual(c, 7)

    def test_binop_add_working(self) -> None:
        code = """
        class TestObj:
            @readonly_func
            def __init__(self, val: Readonly[int]):
                self.value = val

            @readonly_func
            def __add__(self: Readonly[TestObj], other: Readonly[TestObj]) -> TestObj:
                return TestObj(self.value + other.value)

        @readonly_func
        def f() -> Readonly[int]:
            a = TestObj(3)
            b = readonly(TestObj(4))
            c = a + b
            return c.value
        """
        with self.assertNoImmutableErrors():
            c = self.compile_and_call(code, "f")
            self.assertEqual(c, 7)

    def test_binop_radd_mutable_arg(self) -> None:
        code = """
        class TestObj:
            @readonly_func
            def __init__(self, val: Readonly[int]):
                self.value = val

            @readonly_func
            def __add__(self: Readonly[TestObj], other: Readonly[TestObj]) -> Readonly[TestObj]:
                return TestObj(self.value + other.value)

        class TestObjB(TestObj):
            @readonly_func
            def __init__(self, val: Readonly[int]):
                super().__init__(val)

            @readonly_func
            def __radd__(self: TestObjB, other: Readonly[TestObj]) -> Readonly[TestObjB]:
                return TestObjB(self.value + other.value + 8)

        @readonly_func
        def f() -> Readonly[int]:
            a = TestObj(3)
            b = readonly(TestObjB(4))
            c = a + b
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
            c = self.compile_and_call(code, "f")
            self.assertEqual(c, 15)

    def test_binop_fallback_add_mutable_arg(self) -> None:
        code = """
        class TestObj:
            @readonly_func
            def __init__(self, val: Readonly[int]):
                self.value = val

            @readonly_func
            def __add__(self: Readonly[TestObj], other: TestObj) -> Readonly[TestObj]:
                return TestObj(self.value + other.value)

        class TestObjB(TestObj):
            @readonly_func
            def __init__(self, val: Readonly[int]):
                super().__init__(val)

            @readonly_func
            def __radd__(self: Readonly[TestObjB], other: Readonly[TestObj]) -> Readonly[TestObjB]:
                return NotImplemented

        @readonly_func
        def f() -> Readonly[int]:
            a = TestObj(3)
            b = readonly(TestObjB(4))
            c = a + b
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
            c = self.compile_and_call(code, "f")
            self.assertEqual(c, 7)
