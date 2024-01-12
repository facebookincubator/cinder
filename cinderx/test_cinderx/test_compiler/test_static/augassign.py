from .common import StaticTestBase


class AugAssignTests(StaticTestBase):
    def test_aug_assign(self) -> None:
        codestr = """
        def f(l):
            l[0] += 1
        """
        with self.in_module(codestr) as mod:
            f = mod.f
            l = [1]
            f(l)
            self.assertEqual(l[0], 2)

    def test_field(self):
        codestr = """
        class C:
            def __init__(self):
                self.x = 1

        def f(a: C):
            a.x += 1
        """
        code = self.compile(codestr, modname="foo")
        code = self.find_code(code, name="f")
        self.assertInBytecode(code, "LOAD_FIELD", ("foo", "C", "x"))
        self.assertInBytecode(code, "STORE_FIELD", ("foo", "C", "x"))

    def test_primitive_int(self):
        codestr = """
        from __static__ import int8, box, unbox

        def a(i: int) -> int:
            j: int8 = unbox(i)
            j += 2
            return box(j)
        """
        with self.in_module(codestr) as mod:
            a = mod.a
            self.assertInBytecode(a, "PRIMITIVE_BINARY_OP", 0)
            self.assertEqual(a(3), 5)

    def test_inexact(self):
        codestr = """
        def something():
            return 3

        def t():
            a: int = something()

            b = 0
            b += a
            return b
        """
        with self.in_module(codestr) as mod:
            t = mod.t
            self.assertInBytecode(t, "INPLACE_ADD")
            self.assertEqual(t(), 3)

    def test_list(self):
        for prim_idx in [True, False]:
            with self.subTest(prim_idx=prim_idx):
                codestr = f"""
                    from __static__ import int32

                    def f(x: int):
                        l = [x]
                        i: {"int32" if prim_idx else "int"} = 0
                        l[i] += 1
                        return l[i]
                """
                with self.in_module(codestr) as mod:
                    self.assertEqual(mod.f(3), 4)

    def test_checked_list(self):
        for prim_idx in [True, False]:
            with self.subTest(prim_idx=prim_idx):
                codestr = f"""
                    from __static__ import CheckedList, int32

                    def f(x: int):
                        l: CheckedList[int] = [x]
                        i: {"int32" if prim_idx else "int"} = 0
                        l[i] += 1
                        return l[i]
                """
                with self.in_module(codestr) as mod:
                    self.assertEqual(mod.f(3), 4)

    def test_checked_dict(self):
        codestr = """
            from __static__ import CheckedDict

            def f(x: int):
                d: CheckedDict[int, int] = {0: x}
                d[0] += 1
                return d[0]
        """
        with self.in_module(codestr) as mod:
            self.assertEqual(mod.f(3), 4)
