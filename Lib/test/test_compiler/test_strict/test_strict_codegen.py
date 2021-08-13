from __future__ import annotations

import unittest

from .common import StrictTestBase


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


if __name__ == "__main__":
    unittest.main()
