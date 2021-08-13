from __future__ import annotations

import unittest

from .common import StrictTestBase, StrictTestWithCheckerBase


class StrictCompilationTests(StrictTestBase):
    def test_strictmod_freeze_type(self):
        codestr = """
        class C:
            x = 1
        """
        code = self.compile(codestr)
        self.assertInBytecode(
            code,
            "LOAD_GLOBAL",
            "<freeze-type>",
        )
        self.assertInBytecode(
            code,
            "STORE_GLOBAL",
            "<classes>",
        )
        with self.with_freeze_type_setting(True), self.in_module(codestr) as mod:
            C = mod.C
            self.assertEqual(C.x, 1)
            with self.assertRaises(TypeError):
                C.x = 2
            self.assertEqual(C.x, 1)

    def test_strictmod_freeze_set_false(self):
        codestr = """
        class C:
            x = 1
        """
        code = self.compile(codestr)
        with self.with_freeze_type_setting(False), self.in_module(codestr) as mod:
            C = mod.C
            self.assertEqual(C.x, 1)
            C.x = 2
            self.assertEqual(C.x, 2)

    def test_strictmod_class_in_function(self):
        codestr = """
        def f():
            class C:
                x = 1
            return C
        """
        with self.with_freeze_type_setting(True), self.in_module(codestr) as mod:
            f = mod.f
            C = f()
            self.assertEqual(C.x, 1)
            with self.assertRaises(TypeError):
                C.x = 2
            self.assertEqual(C.x, 1)

            code = f.__code__
            self.assertInBytecode(
                code,
                "SETUP_FINALLY",
            )
            self.assertInBytecode(
                code,
                "STORE_FAST",
                "<classes>",
            )

    def test_strictmod_class_not_in_function(self):
        codestr = """
        class C:
            pass
        def f():
            return C
        """
        code = self.compile(codestr)
        self.assertNotInBytecode(
            code,
            "SETUP_FINALLY",
        )
        self.assertInBytecode(
            code,
            "STORE_GLOBAL",
            "<classes>",
        )

    def test_strictmod_fixed_modules_typing(self):
        codestr = """
        from typing import final

        @final
        class C:
            x = 1
        """
        code = self.compile(codestr)
        self.assertInBytecode(
            code,
            "STORE_GLOBAL",
            "final",
        )
        with self.with_freeze_type_setting(True), self.in_module(codestr) as mod:
            C = mod.C
            self.assertEqual(C.x, 1)
            with self.assertRaises(TypeError):
                C.x = 2
            self.assertEqual(C.x, 1)


class StrictCheckedCompilationTests(StrictTestWithCheckerBase):
    def test_strictmod_freeze_type(self):
        codestr = """
        import __strict__
        class C:
            x = 1
        """
        with self.with_freeze_type_setting(True), self.in_checked_module(
            codestr
        ) as mod:
            C = mod.C
            self.assertEqual(C.x, 1)
            with self.assertRaises(TypeError):
                C.x = 2
            self.assertEqual(C.x, 1)

    def test_strictmod_mutable(self):
        codestr = """
        import __strict__
        from __strict__ import mutable

        @mutable
        class C:
            x = 1
        """
        code = self.check_and_compile(codestr)
        self.assertInBytecode(
            code,
            "STORE_GLOBAL",
            "mutable",
        )
        with self.with_freeze_type_setting(True), self.in_checked_module(
            codestr
        ) as mod:
            C = mod.C
            self.assertEqual(C.x, 1)
            C.x = 2
            self.assertEqual(C.x, 2)

    def test_strictmod_cached_property(self):
        codestr = """
        import __strict__
        from __strict__ import strict_slots, _mark_cached_property, mutable
        def dec(x):
            _mark_cached_property(x, False, dec)
            class C:
                def __get__(self, inst, ctx):
                    return x(inst)

            return C()

        @mutable
        @strict_slots
        class C:
            @dec
            def f(self):
                return 1
        """
        with self.with_freeze_type_setting(True), self.in_checked_module(
            codestr
        ) as mod:
            C = mod.C
            c = C()
            self.assertEqual(c.f, 1)
            self.assertEqual(c.f, 1)
            self.assertEqual(C.__slots__, ("f",))


if __name__ == "__main__":
    unittest.main()
