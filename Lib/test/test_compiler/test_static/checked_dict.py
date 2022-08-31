from __static__ import chkdict

from compiler.static.types import FAST_LEN_DICT, TypedSyntaxError
from unittest import skip, skipIf

from .common import StaticTestBase, type_mismatch

try:
    import cinderjit
except ImportError:
    cinderjit = None


class CheckedDictTests(StaticTestBase):
    def test_invoke_chkdict_method(self):
        codestr = """
        from __static__ import CheckedDict
        def dict_maker() -> CheckedDict[int, int]:
            return CheckedDict[int, int]({2:2})
        def func():
            a = dict_maker()
            return a.keys()

        """
        with self.in_module(codestr) as mod:
            f = mod.func

            self.assertInBytecode(
                f,
                "INVOKE_FUNCTION",
                (
                    (
                        "__static__",
                        "chkdict",
                        (("builtins", "int"), ("builtins", "int")),
                        "!",
                        "keys",
                    ),
                    1,
                ),
            )
            self.assertEqual(list(f()), [2])
            self.assert_jitted(f)

    def test_generic_method_ret_type(self):
        codestr = """
            from __static__ import CheckedDict

            from typing import Optional
            MAP: CheckedDict[str, Optional[str]] = CheckedDict[str, Optional[str]]({'abc': 'foo', 'bar': None})
            def f(x: str) -> Optional[str]:
                return MAP.get(x)
        """

        with self.in_module(codestr) as mod:
            f = mod.f
            self.assertInBytecode(
                f,
                "INVOKE_FUNCTION",
                (
                    (
                        "__static__",
                        "chkdict",
                        (("builtins", "str"), ("builtins", "str", "?")),
                        "!",
                        "get",
                    ),
                    3,
                ),
            )
            self.assertEqual(f("abc"), "foo")
            self.assertEqual(f("bar"), None)

    def test_compile_nested_dict(self):
        codestr = """
            from __static__ import CheckedDict

            class B: pass
            class D(B): pass

            def testfunc():
                x = CheckedDict[B, int]({B():42, D():42})
                y = CheckedDict[int, CheckedDict[B, int]]({42: x})
                return y
        """
        with self.in_module(codestr) as mod:
            test = mod.testfunc
            B = mod.B
            self.assertEqual(type(test()), chkdict[int, chkdict[B, int]])

    def test_compile_dict_setdefault(self):
        codestr = """
            from __static__ import CheckedDict
            def testfunc():
                x = CheckedDict[int, str]({42: 'abc', })
                x.setdefault(100, 43)
        """
        with self.assertRaisesRegex(
            TypedSyntaxError,
            r"Literal\[43\] received for positional arg 2, expected Optional\[str\]",
        ):
            self.compile(codestr, modname="foo")

    def test_compile_dict_get(self):
        codestr = """
            from __static__ import CheckedDict
            def testfunc():
                x = CheckedDict[int, str]({42: 'abc', })
                x.get(42, 42)
        """
        with self.assertRaisesRegex(
            TypedSyntaxError,
            r"Literal\[42\] received for positional arg 2, expected Optional\[str\]",
        ):
            self.compile(codestr, modname="foo")

        codestr = """
            from __static__ import CheckedDict

            class B: pass
            class D(B): pass

            def testfunc():
                x = CheckedDict[B, int]({B():42, D():42})
                return x
        """
        with self.in_module(codestr) as mod:
            test = mod.testfunc
            B = mod.B
            self.assertEqual(type(test()), chkdict[B, int])

    def test_chkdict_literal(self):
        codestr = """
            from __static__ import CheckedDict
            def testfunc():
                x: CheckedDict[int,str]  = {}
                return x
        """
        with self.in_module(codestr) as mod:
            f = mod.testfunc
            self.assertEqual(type(f()), chkdict[int, str])

    def test_compile_dict_get_typed(self):
        codestr = """
            from __static__ import CheckedDict
            def testfunc():
                x = CheckedDict[int, str]({42: 'abc', })
                y: str | None = x.get(42)
        """
        self.compile(codestr)

    def test_compile_dict_setdefault_typed(self):
        codestr = """
            from __static__ import CheckedDict
            def testfunc():
                x = CheckedDict[int, str]({42: 'abc', })
                y: str | None = x.setdefault(100, 'foo')
        """
        self.compile(codestr)

    def test_compile_dict_setitem(self):
        codestr = """
            from __static__ import CheckedDict

            def testfunc():
                x = CheckedDict[int, str]({1:'abc'})
                x.__setitem__(2, 'def')
                return x
        """
        with self.in_module(codestr) as mod:
            test = mod.testfunc
            x = test()
            self.assertInBytecode(
                test,
                "INVOKE_FUNCTION",
                (
                    (
                        "__static__",
                        "chkdict",
                        (("builtins", "int"), ("builtins", "str")),
                        "!",
                        "__setitem__",
                    ),
                    3,
                ),
            )
            self.assertEqual(x, {1: "abc", 2: "def"})

    def test_compile_dict_setitem_subscr(self):
        codestr = """
            from __static__ import CheckedDict

            def testfunc():
                x = CheckedDict[int, str]({1:'abc'})
                x[2] = 'def'
                return x
        """
        with self.in_module(codestr) as mod:
            test = mod.testfunc
            x = test()
            self.assertInBytecode(
                test,
                "INVOKE_FUNCTION",
                (
                    (
                        "__static__",
                        "chkdict",
                        (("builtins", "int"), ("builtins", "str")),
                        "!",
                        "__setitem__",
                    ),
                    3,
                ),
            )
            self.assertEqual(x, {1: "abc", 2: "def"})

    def test_compile_generic_dict_getitem_bad_type(self):
        codestr = """
            from __static__ import CheckedDict

            def testfunc():
                x = CheckedDict[str, int]({"abc": 42})
                return x[42]
        """
        with self.assertRaisesRegex(
            TypedSyntaxError,
            type_mismatch("Literal[42]", "str"),
        ):
            self.compile(codestr, modname="foo")

    def test_compile_generic_dict_setitem_bad_type(self):
        codestr = """
            from __static__ import CheckedDict

            def testfunc():
                x = CheckedDict[str, int]({"abc": 42})
                x[42] = 42
        """
        with self.assertRaisesRegex(
            TypedSyntaxError,
            type_mismatch("Literal[42]", "str"),
        ):
            self.compile(codestr, modname="foo")

    def test_compile_generic_dict_setitem_bad_type_2(self):
        codestr = """
            from __static__ import CheckedDict

            def testfunc():
                x = CheckedDict[str, int]({"abc": 42})
                x["foo"] = "abc"
        """
        with self.assertRaisesRegex(
            TypedSyntaxError,
            type_mismatch("str", "int"),
        ):
            self.compile(codestr, modname="foo")

    def test_compile_checked_dict_shadowcode(self):
        codestr = """
            from __static__ import CheckedDict

            class B: pass
            class D(B): pass

            def testfunc():
                x = CheckedDict[B, int]({B():42, D():42})
                return x
        """
        with self.in_module(codestr) as mod:
            test = mod.testfunc
            B = mod.B
            for i in range(200):
                self.assertEqual(type(test()), chkdict[B, int])

    def test_compile_checked_dict_optional(self):
        codestr = """
            from __static__ import CheckedDict
            from typing import Optional

            def testfunc():
                x = CheckedDict[str, str | None]({
                    'x': None,
                    'y': 'z'
                })
                return x
        """
        with self.in_module(codestr) as mod:
            f = mod.testfunc
            x = f()
            x["z"] = None
            self.assertEqual(type(x), chkdict[str, str | None])

    def test_compile_checked_dict_bad_annotation(self):
        codestr = """
            from __static__ import CheckedDict

            def testfunc():
                x: 42 = CheckedDict[str, str]({'abc':'abc'})
                return x
        """
        with self.in_module(codestr) as mod:
            test = mod.testfunc
            self.assertEqual(type(test()), chkdict[str, str])

    def test_compile_checked_dict_ann_differs(self):
        codestr = """
            from __static__ import CheckedDict

            def testfunc():
                x: CheckedDict[int, int] = CheckedDict[str, str]({'abc':'abc'})
                return x
        """
        with self.assertRaisesRegex(
            TypedSyntaxError,
            type_mismatch(
                "chkdict[str, str]",
                "chkdict[int, int]",
            ),
        ):
            self.compile(codestr, modname="foo")

    def test_compile_checked_dict_ann_differs_2(self):
        codestr = """
            from __static__ import CheckedDict

            def testfunc():
                x: int = CheckedDict[str, str]({'abc':'abc'})
                return x
        """
        with self.assertRaisesRegex(
            TypedSyntaxError,
            type_mismatch("chkdict[str, str]", "int"),
        ):
            self.compile(codestr, modname="foo")

    def test_compile_checked_dict_opt_out_by_default(self):
        codestr = """
            class B: pass
            class D(B): pass

            def testfunc():
                x = {B():42, D():42}
                return x
        """
        with self.in_module(codestr) as mod:
            test = mod.testfunc
            self.assertEqual(type(test()), dict)

    def test_compile_checked_dict_opt_in(self):
        codestr = """
            from __static__.compiler_flags import checked_dicts
            class B: pass
            class D(B): pass

            def testfunc():
                x = {B():42, D():42}
                return x
        """
        with self.in_module(codestr) as mod:
            test = mod.testfunc
            B = mod.B
            self.assertEqual(type(test()), chkdict[B, int])

    def test_compile_checked_dict_explicit_dict(self):
        codestr = """
            from __static__ import pydict
            class B: pass
            class D(B): pass

            def testfunc():
                x: pydict = {B():42, D():42}
                return x
        """
        with self.in_module(codestr) as mod:
            test = mod.testfunc
            self.assertEqual(type(test()), dict)

    def test_compile_checked_dict_reversed(self):
        codestr = """
            from __static__ import CheckedDict

            class B: pass
            class D(B): pass

            def testfunc():
                x = CheckedDict[B, int]({D():42, B():42})
                return x
        """
        with self.in_module(codestr) as mod:
            test = mod.testfunc
            B = mod.B
            self.assertEqual(type(test()), chkdict[B, int])

    def test_compile_checked_dict_type_specified(self):
        codestr = """
            from __static__ import CheckedDict

            class B: pass
            class D(B): pass

            def testfunc():
                x: CheckedDict[B, int] = CheckedDict[B, int]({D():42})
                return x
        """
        with self.in_module(codestr) as mod:
            test = mod.testfunc
            B = mod.B
            self.assertEqual(type(test()), chkdict[B, int])

    def test_compile_checked_dict_with_annotation_comprehension(self):
        codestr = """
            from __static__ import CheckedDict

            def testfunc():
                x: CheckedDict[int, object] = {int(i): object() for i in range(1, 5)}
                return x
        """
        with self.in_module(codestr) as mod:
            test = mod.testfunc
            self.assertEqual(type(test()), chkdict[int, object])

    def test_compile_checked_dict_with_annotation(self):
        codestr = """
            from __static__ import CheckedDict

            class B: pass

            def testfunc():
                x: CheckedDict[B, int] = {B():42}
                return x
        """
        with self.in_module(codestr) as mod:
            test = mod.testfunc
            B = mod.B
            test()
            self.assertEqual(type(test()), chkdict[B, int])

    def test_compile_checked_dict_with_annotation_wrong_value_type(self):
        codestr = """
            from __static__ import CheckedDict

            class B: pass

            def testfunc():
                x: CheckedDict[B, int] = {B():'hi'}
                return x
        """
        with self.assertRaisesRegex(
            TypedSyntaxError,
            type_mismatch(
                "chkdict[foo.B, str]",
                "chkdict[foo.B, int]",
            ),
        ):
            self.compile(codestr, modname="foo")

    def test_compile_checked_dict_with_annotation_wrong_key_type(self):
        codestr = """
            from __static__ import CheckedDict

            class B: pass

            def testfunc():
                x: CheckedDict[B, int] = {object():42}
                return x
        """
        with self.assertRaisesRegex(
            TypedSyntaxError,
            type_mismatch(
                "chkdict[object, Literal[42]]",
                "chkdict[foo.B, int]",
            ),
        ):
            self.compile(codestr, modname="foo")

    def test_compile_checked_dict_wrong_unknown_type(self):
        codestr = """
            def f(x: int):
                return x

            def testfunc(iter):
                return f({x:42 for x in iter})

        """
        with self.assertRaisesRegex(
            TypedSyntaxError,
            r"dict received for positional arg 'x', expected int",
        ):
            self.compile(codestr, modname="foo")

    def test_compile_checked_dict_explicit_dict_as_dict(self):
        codestr = """
            from __static__ import pydict as dict
            class B: pass
            class D(B): pass

            def testfunc():
                x: dict = {B():42, D():42}
                return x
        """
        with self.in_module(codestr) as mod:
            test = mod.testfunc
            self.assertEqual(type(test()), dict)

    def test_compile_checked_dict_from_dict_call(self):
        codestr = """
            from __static__.compiler_flags import checked_dicts

            def testfunc():
                x = dict(x=42)
                return x
        """
        with self.assertRaisesRegex(
            TypeError, "cannot create '__static__.chkdict\\[K, V\\]' instances"
        ):
            with self.in_module(codestr) as mod:
                test = mod.testfunc
                test()

    def test_compile_checked_dict_from_dict_call_2(self):
        codestr = """
            from __static__.compiler_flags import checked_dicts

            def testfunc():
                x = dict[str, int](x=42)
                return x
        """
        with self.in_module(codestr) as mod:
            test = mod.testfunc
            self.assertEqual(type(test()), chkdict[str, int])

    def test_compile_checked_dict_from_dict_call_3(self):
        # we emit the chkdict import first before future annotations, but that
        # should be fine as we're the compiler.
        codestr = """
            from __future__ import annotations
            from __static__.compiler_flags import checked_dicts

            def testfunc():
                x = dict[str, int](x=42)
                return x
        """
        with self.in_module(codestr) as mod:
            test = mod.testfunc
            self.assertEqual(type(test()), chkdict[str, int])

    def test_compile_checked_dict_len(self):
        codestr = """
            from __static__ import CheckedDict

            def testfunc():
                x = CheckedDict[int, str]({1:'abc'})
                return len(x)
        """
        with self.in_module(codestr) as mod:
            test = mod.testfunc
            self.assertInBytecode(test, "FAST_LEN", FAST_LEN_DICT)
            if cinderjit is not None:
                cinderjit.get_and_clear_runtime_stats()
            self.assertEqual(test(), 1)
            if cinderjit is not None:
                stats = cinderjit.get_and_clear_runtime_stats().get("deopt")
                self.assertFalse(stats)

    def test_compile_checked_dict_clen(self):
        codestr = """
            from __static__ import CheckedDict, clen, int64

            def testfunc() -> int64:
                x = CheckedDict[int, str]({1:'abc'})
                return clen(x)
        """
        with self.in_module(codestr) as mod:
            test = mod.testfunc
            self.assertInBytecode(test, "FAST_LEN", FAST_LEN_DICT)
            if cinderjit is not None:
                cinderjit.get_and_clear_runtime_stats()
            self.assertEqual(test(), 1)
            if cinderjit is not None:
                stats = cinderjit.get_and_clear_runtime_stats().get("deopt")
                self.assertFalse(stats)

    def test_compile_checked_dict_create_with_dictcomp(self):
        codestr = """
            from __static__ import CheckedDict, clen, int64

            def testfunc() -> None:
                x = CheckedDict[int, str]({int(i): int(i) for i in
                               range(1, 5)})
        """
        with self.assertRaisesRegex(
            TypedSyntaxError,
            type_mismatch("chkdict[int, int]", "chkdict[int, str]"),
        ):
            self.compile(codestr)

    def test_chkdict_float_is_dynamic(self):
        codestr = """
        from __static__ import CheckedDict

        def main():
            d = CheckedDict[float, str]({2.0: "hello", 2.3: "foobar"})
            reveal_type(d)
        """
        with self.assertRaisesRegex(
            TypedSyntaxError,
            r"reveal_type\(d\): 'Exact\[chkdict\[dynamic, str\]\]'",
        ):
            self.compile(codestr)

    def test_build_checked_dict_cached(self):
        codestr = """
        from __static__ import CheckedDict

        def f() -> str:
            d: CheckedDict[float, str] = {2.0: "hello", 2.3: "foobar"}
            return d[2.0]
        """
        with self.in_module(codestr) as mod:
            self.assertInBytecode(mod.f, "BUILD_CHECKED_MAP")
            for i in range(50):
                self.assertEqual(mod.f(), "hello")
            self.assertEqual(mod.f(), "hello")
