from .common import StaticTestBase


class PrimitivesTests(StaticTestBase):
    def test_primitive_context_ifexp(self) -> None:
        codestr = """
            from __static__ import int64

            def f(x: int | None = None) -> int64:
                return int64(x) if x is not None else 0
        """
        with self.in_module(codestr) as mod:
            f = mod.f
            self.assertEqual(f(), 0)
            self.assertEqual(f(1), 1)

    def test_double_augassign(self) -> None:
        codestr = """
            from __static__ import box, double

            def f() -> float:
                x: double = 3
                x /= 2
                return box(x)
        """
        with self.in_module(codestr) as mod:
            f = mod.f
            self.assertNotInBytecode(f, "LOAD_FAST")
            self.assertNotInBytecode(f, "STORE_FAST")
            self.assertEqual(f(), 1.5)

    def test_no_useless_convert(self) -> None:
        codestr = """
            from __static__ import int64

            def f(x: int64) -> int64:
                return x + 1
        """
        with self.in_module(codestr) as mod:
            f = mod.f
            self.assertNotInBytecode(f, "CONVERT_PRIMITIVE")
            self.assertEqual(f(2), 3)

    def test_not(self) -> None:
        for typ, vals in [
            ("cbool", {True: False, False: True}),
            ("int64", {5: False, 0: True, -27: False}),
            ("uint8", {1: False, 0: True}),
        ]:
            codestr = f"""
                from __static__ import cbool, int64, uint8

                def f(x: {typ}) -> cbool:
                    return not x
            """
            with self.in_module(codestr) as mod:
                self.assertInBytecode(mod.f, "PRIMITIVE_UNARY_OP")
                for k, v in vals.items():
                    with self.subTest(typ=typ, val=k, res=v):
                        self.assertEqual(mod.f(k), v)
