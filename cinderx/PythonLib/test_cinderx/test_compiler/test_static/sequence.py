from .common import StaticTestBase


class SequenceTests(StaticTestBase):
    def test_exact_list_int_index_sequence_get_set(self) -> None:
        codestr = """
            def f(x: int) -> int:
                l = [0, 1, 2]
                l[0] = x
                return l[0]
        """
        with self.in_module(codestr) as mod:
            self.assertInBytecode(mod.f, "SEQUENCE_GET")
            self.assertInBytecode(mod.f, "SEQUENCE_SET")
            self.assertEqual(mod.f(2), 2)

    def test_list_subclass_int_index(self) -> None:
        codestr = """
            from typing import List

            def f(l: List[int]) -> int:
                l[1] = 3
                return l[0]
        """
        with self.in_module(codestr) as mod:
            self.assertNotInBytecode(mod.f, "SEQUENCE_GET")
            self.assertNotInBytecode(mod.f, "SEQUENCE_SET")
            l = [1, 2]
            self.assertEqual(mod.f(l), 1)
            self.assertEqual(l, [1, 3])

            class MyList(list):
                def __getitem__(self, idx):
                    return super().__getitem__(idx) * 10

                def __setitem__(self, idx, val):
                    super().__setitem__(idx, val + 1)

            myl = MyList([1, 2])
            self.assertEqual(mod.f(myl), 10)
            self.assertEqual(list(myl), [1, 4])

    def test_negative_index(self) -> None:
        codestr = """
            def f() -> int:
                l = [0, 1, 2]
                l[-1] = 3
                return l[-1]
        """
        with self.in_module(codestr) as mod:
            self.assertInBytecode(mod.f, "SEQUENCE_GET")
            self.assertInBytecode(mod.f, "SEQUENCE_SET")
            self.assertEqual(mod.f(), 3)
