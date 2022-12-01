import re
import sys

from compiler.static.types import TypedSyntaxError
from itertools import product

from .common import StaticTestBase


class SysHexVersionTests(StaticTestBase):
    def test_sys_hexversion(self):
        current_version = sys.hexversion

        for version_check_should_pass, is_on_left in product(
            (False, True), (False, True)
        ):
            with self.subTest(msg=f"{version_check_should_pass=}, {is_on_left=}"):
                if version_check_should_pass:
                    checked_version = current_version - 1
                    expected_attribute = "a"
                else:
                    checked_version = current_version + 1
                    expected_attribute = "b"

                if is_on_left:
                    condition_str = f"sys.hexversion >= {hex(checked_version)}"
                else:
                    condition_str = f"{hex(checked_version)} <= sys.hexversion"

                codestr = f"""
                import sys

                if {condition_str}:
                    class A:
                        def a(self):
                            pass
                else:
                    class A:
                        def b(self):
                            pass
                """
                with self.in_strict_module(codestr) as mod:
                    self.assertTrue(hasattr(mod.A, expected_attribute))

    def test_sys_hexversion_unsupported_operator(self):
        op_to_err = {
            "in": "in",
            "is": "is",
            "is not": "is",
        }
        for op, is_on_left in product(op_to_err.keys(), (True, False)):

            if is_on_left:
                condition_str = f"sys.hexversion {op} 50988528"
            else:
                condition_str = f"50988528 {op} sys.hexversion"

            with self.subTest(msg=f"{op=}, {is_on_left=}"):
                codestr = f"""
                import sys

                if {condition_str}:
                    class A:
                        def a(self):
                            pass
                else:
                    class A:
                        def b(self):
                            pass
                """
                with self.assertRaisesRegex(
                    TypedSyntaxError, "Cannot redefine local variable A"
                ):
                    self.compile(codestr)

    def test_sys_hexversion_dynamic_compare(self):
        codestr = f"""
        import sys
        from something import X

        if sys.hexversion >= X:
            class A:
                def a(self):
                    pass
        else:
            class A:
                def b(self):
                    pass
        """
        with self.assertRaisesRegex(
            TypedSyntaxError, "Cannot redefine local variable A"
        ):
            self.compile(codestr)

    # comparing with double is kinda meaningless
    def test_sys_hexversion_compare_double(self):
        codestr = f"""
        import sys

        if sys.hexversion >= 3.12:
            class A:
                def a(self):
                    pass
        else:
            class A:
                def b(self):
                    pass
        """
        with self.assertRaisesRegex(
            TypedSyntaxError, "Cannot redefine local variable A"
        ):
            self.compile(codestr)
