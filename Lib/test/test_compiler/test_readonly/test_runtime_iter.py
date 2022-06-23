import unittest

from .common import ReadonlyTestBase


class RuntimeIterTests(ReadonlyTestBase):
    @unittest.skipUnlessReadonly()
    def test_for_loop_iter(self) -> None:
        code = """
        from typing import List
        class C:
            @readonly_func
            def __iter__(self: C) -> List[int]:
                return iter([1, 2])

        @readonly_func
        def f():
            c = C()
            l = []
            for x in readonly(c):
                l.append(x)
            return l
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
            self.assertEqual(c, [1, 2])

    @unittest.skipUnlessReadonly()
    def test_for_loop_iter_ro(self) -> None:
        code = """
        from typing import Iterator
        class C:
            @readonly_func
            def __iter__(self: C) -> Readonly[Iterator[int]]:
                return iter([1, 2])

        @readonly_func
        def f():
            c = C()
            l = []
            for x in c:
                l.append(x)
            return l
        """
        with self.assertImmutableErrors(
            [(14, "Operator returns readonly, but expected mutable.", ())]
        ):
            c = self.compile_and_call(code, "f")
            self.assertEqual(c, [1, 2])

    @unittest.skipUnlessReadonly()
    def test_for_loop_iter_next_ro(self) -> None:
        code = """
        from typing import List
        class I:
            def __init__(self):
                self.c = 0
            @readonly_func
            def __next__(self: I) -> Readonly[int]:
                self.c += 1
                if self.c > 2:
                    raise StopIteration
                return 1
        class C:
            @readonly_func
            def __iter__(self: C) -> I:
                return I()

        @readonly_func
        def f():
            c = C()
            l = []
            for x in c:
                l.append(x)
            return l
        """
        with self.assertImmutableErrors(
            [(14, "Operator returns readonly, but expected mutable.", ())]
        ):
            c = self.compile_and_call(code, "f")
            self.assertEqual(c, [1, 1])

    @unittest.skipUnlessReadonly()
    def test_for_loop_iter_next_self_ro_ok(self) -> None:
        code = """
        from typing import List
        class I:
            @readonly_func
            def __next__(self: Readonly[I]) -> int:
                raise StopIteration
        class C:
            @readonly_func
            def __iter__(self: C) -> I:
                return I()

        @readonly_func
        def f():
            c = C()
            l = []
            for x in c:
                l.append(x)
            return l
        """
        with self.assertNoImmutableErrors():
            c = self.compile_and_call(code, "f")
            self.assertEqual(c, [])

    @unittest.skipUnlessReadonly()
    def test_for_loop_iter_ro_self_iter(self) -> None:
        code = """
        @readonly_func
        def f():
            l = []
            i = readonly(iter([1, 2]))

            for x in i:
                l.append(x)
            return l
        """
        with self.assertImmutableErrors(
            [(14, "Operator returns readonly, but expected mutable.", ())]
        ):
            c = self.compile_and_call(code, "f")
            self.assertEqual(c, [1, 2])

    @unittest.skipUnlessReadonly()
    def test_for_loop_iter_builtin(self) -> None:
        code = """
        @readonly_func
        def f():
            l = []
            i = readonly([1, 2])

            for x in i:
                l.append(x)
            return l
        """
        with self.assertNoImmutableErrors():
            c = self.compile_and_call(code, "f")
            self.assertEqual(c, [1, 2])
