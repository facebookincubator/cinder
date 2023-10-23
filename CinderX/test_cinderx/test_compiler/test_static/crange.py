from compiler.static.types import TypedSyntaxError

from .common import StaticTestBase, type_mismatch


class CRangeTests(StaticTestBase):
    def test_crange_only_limit(self):
        codestr = """
        from __static__ import crange, int64, box

        def sum_until(n: int) -> int:
            x: int64 = 0
            for j in crange(int64(n)):
                x += j
            return box(x)
        """
        with self.in_strict_module(codestr) as mod:
            self.assertNotInBytecode(mod.sum_until, "FOR_ITER")
            self.assertEqual(mod.sum_until(6), 15)

    def test_crange_start_and_limit(self):
        codestr = """
        from __static__ import crange, int64, box

        def sum_between(m: int, n: int) -> int:
            x: int64 = 0
            for j in crange(int64(m), int64(n)):
                x += j
            return box(x)
        """
        with self.in_strict_module(codestr) as mod:
            self.assertNotInBytecode(mod.sum_between, "FOR_ITER")
            self.assertEqual(mod.sum_between(3, 6), 12)

    def test_crange_incorrect_arg_count(self):
        codestr = """
        from __static__ import crange, int64, box

        for j in crange():
            pass
        """
        with self.assertRaisesRegex(
            TypedSyntaxError, r"crange\(\) accepts only 1 or 2 parameters"
        ):
            self.compile(codestr)

        other_codestr = """
        from __static__ import crange, int64, box

        for j in crange(int64(0), int64(1), int64(2)):
            pass
        """
        with self.assertRaisesRegex(
            TypedSyntaxError, r"crange\(\) accepts only 1 or 2 parameters"
        ):
            self.compile(other_codestr)

    def test_crange_break_start_and_limit(self):
        codestr = """
        from __static__ import crange, int64, box

        def sum_between(m: int, n: int) -> int:
            x: int64 = 0
            for j in crange(int64(m), int64(n)):
                x += j
                if x == 7:
                    break
            return box(x)
        """
        with self.in_strict_module(codestr) as mod:
            self.assertEqual(mod.sum_between(3, 6), 7)

    def test_crange_break_only_limit(self):
        codestr = """
        from __static__ import crange, int64, box

        def sum_until(n: int) -> int:
            x: int64 = 0
            for j in crange(int64(n)):
                x += j
                if x == 6:
                    break
            return box(x)
        """
        with self.in_strict_module(codestr) as mod:
            self.assertEqual(mod.sum_until(6), 6)

    def test_crange_orelse_iterator_exhausted(self):
        codestr = """
        from __static__ import crange, int64, box

        def sum_until(n: int) -> int:
            x: int64 = 0
            for j in crange(int64(n)):
                x += j
            else:
                return 666
            return box(x)
        """
        with self.in_strict_module(codestr) as mod:
            self.assertEqual(mod.sum_until(6), 666)

    def test_crange_orelse_iterator_not_exhausted(self):
        codestr = """
        from __static__ import crange, int64, box

        def sum_until(n: int) -> int:
            x: int64 = 0
            for j in crange(int64(n)):
                x += j
                if x == 6:
                    break
            else:
                return 666
            return box(x)
        """
        with self.in_strict_module(codestr) as mod:
            self.assertEqual(mod.sum_until(6), 6)

    def test_crange_without_for_loop(self):
        codestr = """
        from __static__ import crange, int64, box

        def bad_fn():
            x: int64 = 1
            y: int64 = 4

            z = crange(1, 4)
        """
        with self.assertRaisesRegex(
            TypedSyntaxError, r"crange\(\) must be used as an iterator in a for loop"
        ):
            self.compile(codestr)

    def test_crange_in_loop_body(self):
        codestr = """
        from __static__ import crange, int64, box

        def sum_until() -> None:
            x: int64 = 0
            for j in range(4):
                p = crange(int64(14))
        """
        with self.assertRaisesRegex(
            TypedSyntaxError, r"crange\(\) must be used as an iterator in a for loop"
        ):
            self.compile(codestr)

    def test_crange_incompatible_arg_types(self):
        codestr = """
        from __static__ import crange, int64, box

        for j in crange(12):
            pass
        """
        with self.assertRaisesRegex(
            TypedSyntaxError, r"can't use crange with arg: Literal\[12\]"
        ):
            self.compile(codestr)

        codestr = """
        from __static__ import crange, int64, box

        for j in crange(object()):
            pass
        """
        with self.assertRaisesRegex(
            TypedSyntaxError, r"can't use crange with arg: object"
        ):
            self.compile(codestr)

    def test_crange_continue(self):
        codestr = """
        from __static__ import crange, int64, box

        def run_loop() -> int:
            n: int64 = 7
            c = 0
            for i in crange(n):
                c += 1
                continue
            return c
        """
        with self.in_strict_module(codestr) as mod:
            self.assertEqual(mod.run_loop(), 7)
