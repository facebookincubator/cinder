from __static__ import chkdict, chklist, int64

import inspect
import unittest
from cinder import freeze_type
from compiler.errors import TypedSyntaxError

from inspect import CO_SUPPRESS_JIT
from re import escape
from unittest import skip

from .common import StaticTestBase


class StaticObjCreationTests(StaticTestBase):
    def test_new_and_init(self):
        codestr = """
            class C:
                def __new__(cls, a):
                    return object.__new__(cls)
                def __init__(self, a):
                    self.a = a

            X = 0
            def g() -> int:
                global X
                X += 1
                return 1

            def f() -> C:
                return C(g())
        """
        with self.in_module(codestr) as mod:
            f = mod.f
            f()
            self.assertEqual(mod.X, 1)

    def test_object_init_and_new(self):
        codestr = """
            class C:
                pass

            def f(x: int) -> C:
                return C(x)
        """
        with self.assertRaisesRegex(
            TypedSyntaxError,
            escape("<module>.C() takes no arguments"),
        ):
            self.compile(codestr)

    def test_init(self):
        codestr = """
            class C:

                def __init__(self, a: int) -> None:
                    self.value = a

            def f(x: int) -> C:
                return C(x)
        """
        with self.in_module(codestr) as mod:
            f = mod.f
            self.assertEqual(f(42).value, 42)

    def test_init_primitive(self):
        codestr = """
            from __static__ import int64
            class C:

                def __init__(self, a: int64) -> None:
                    self.value: int64 = a

            def f(x: int64) -> C:
                return C(x)
        """
        with self.in_module(codestr) as mod:
            f = mod.f
            init = mod.C.__init__
            self.assertInBytecode(init, "LOAD_LOCAL")
            self.assertInBytecode(init, "STORE_FIELD")
            self.assertEqual(f(42).value, 42)

    def test_new_primitive(self):
        codestr = """
            from __static__ import int64
            class C:
                value: int64
                def __new__(cls, a: int64) -> "C":
                    res: C = object.__new__(cls)
                    res.value = a
                    return res

            def f(x: int64) -> C:
                return C(x)
        """
        with self.in_module(codestr) as mod:
            f = mod.f
            init = mod.C.__new__
            self.assertInBytecode(init, "LOAD_LOCAL")
            self.assertInBytecode(init, "STORE_FIELD")
            self.assertEqual(f(42).value, 42)

    def test_init_frozen_type(self):
        codestr = """
            class C:

                def __init__(self, a: int) -> None:
                    self.value = a

            def f(x: int) -> C:
                return C(x)
        """
        with self.in_module(codestr) as mod:
            C = mod.C
            freeze_type(C)
            f = mod.f
            self.assertEqual(f(42).value, 42)

    def test_init_unknown_base(self):
        codestr = """
            from re import Scanner
            class C(Scanner):
                pass

            def f(x: int) -> C:
                return C(x)
        """
        with self.in_module(codestr) as mod:
            f = mod.f
            # Unknown base class w/ no overrides should always be CALL_FUNCTION
            self.assertInBytecode(f, "CALL_FUNCTION")

    def test_init_wrong_type(self):
        codestr = """
            class C:

                def __init__(self, a: int) -> None:
                    self.value = a

            def f(x: str) -> C:
                return C(x)
        """
        with self.assertRaisesRegex(
            TypedSyntaxError,
            "type mismatch: str received for positional arg 'a', expected int",
        ):
            self.compile(codestr)

    def test_init_extra_arg(self):
        codestr = """
            class C:

                def __init__(self, a: int) -> None:
                    self.value = a

            def f(x: int) -> C:
                return C(x, 42)
        """
        with self.assertRaisesRegex(
            TypedSyntaxError,
            escape(
                "Mismatched number of args for function <module>.C.__init__. Expected 2, got 3"
            ),
        ):
            self.compile(codestr)

    def test_new(self):
        codestr = """
            class C:
                value: int
                def __new__(cls, a: int) -> "C":
                    res = object.__new__(cls)
                    res.value = a
                    return res

            def f(x: int) -> C:
                return C(x)
        """
        with self.in_module(codestr) as mod:
            f = mod.f
            self.assertEqual(f(42).value, 42)

    def test_new_wrong_type(self):
        codestr = """
            class C:
                value: int
                def __new__(cls, a: int) -> "C":
                    res = object.__new__(cls)
                    res.value = a
                    return res

            def f(x: str) -> C:
                return C(x)
        """
        with self.assertRaisesRegex(
            TypedSyntaxError,
            "type mismatch: str received for positional arg 'a', expected int",
        ):
            self.compile(codestr)

    def test_new_object(self):
        codestr = """
            class C:
                value: int
                def __new__(cls, a: int) -> object:
                    res = object.__new__(cls)
                    res.value = a
                    return res
                def __init__(self, a: int):
                    self.value = 100

            def f(x: int) -> object:
                return C(x)
        """
        with self.in_module(codestr) as mod:
            f = mod.f
            self.assertEqual(f(42).value, 100)

    def test_new_dynamic(self):
        codestr = """
            class C:
                value: int
                def __new__(cls, a: int):
                    res = object.__new__(cls)
                    res.value = a
                    return res
                def __init__(self, a: int):
                    self.value = 100

            def f(x: int) -> object:
                return C(x)
        """
        with self.in_module(codestr) as mod:
            f = mod.f
            self.assertEqual(f(42).value, 100)

    def test_new_odd_ret_type(self):
        codestr = """
            class C:
                value: int
                def __new__(cls, a: int) -> int:
                    return 42

            def f(x: int) -> int:
                return C(x)
        """
        with self.in_module(codestr) as mod:
            f = mod.f
            self.assertEqual(f(42), 42)

    def test_new_odd_ret_type_no_init(self):
        codestr = """
            class C:
                value: int
                def __new__(cls, a: int) -> int:
                    return 42
                def __init__(self, *args) -> None:
                    raise Exception("no way")

            def f(x: int) -> int:
                return C(x)
        """
        with self.in_module(codestr) as mod:
            f = mod.f
            self.assertEqual(f(42), 42)

    def test_new_odd_ret_type_error(self):
        codestr = """
            class C:
                value: int
                def __new__(cls, a: int) -> int:
                    return 42

            def f(x: int) -> str:
                return C(x)
        """
        with self.assertRaisesRegex(
            TypedSyntaxError, "return type must be str, not int"
        ):
            self.compile(codestr)

    def test_class_init_kw(self):
        codestr = """
            class C:
                def __init__(self, x: str):
                    self.x: str = x

            def f():
                x = C(x='abc')
                return x
        """
        with self.in_module(codestr) as mod:
            f = mod.f
            self.assertNotInBytecode(f, "CALL_FUNCTION_KW", 1)
            self.assertInBytecode(f, "TP_ALLOC")
            self.assertInBytecode(f, "INVOKE_FUNCTION")
            c = f()
            self.assertEqual(c.x, "abc")

    def test_type_subclass(self):
        codestr = """
            class C(type):
                pass

            def f() -> C:
                return C('foo', (), {})
        """
        with self.in_module(codestr) as mod:
            f = mod.f
            C = mod.C
            self.assertEqual(type(f()), C)

    def test_object_new(self):
        codestr = """
            class C(object):
                pass

            def f() -> C:
                return object.__new__(C)
        """
        with self.in_module(codestr) as mod:
            f = mod.f
            C = mod.C
            self.assertEqual(type(f()), C)

    def test_object_new_wrong_type(self):
        codestr = """
            class C(object):
                pass

            def f() -> C:
                return object.__new__(object)
        """
        with self.assertRaisesRegex(
            TypedSyntaxError,
            "return type must be <module>.C, not object",
        ):
            self.compile(codestr)

    def test_bool_call(self):
        codestr = """
            def f(x) -> bool:
                return bool(x)
        """
        with self.in_module(codestr) as mod:
            f = mod.f
            self.assertInBytecode(
                f, "INVOKE_FUNCTION", ((("builtins", "bool", "!", "__new__"), 2))
            )
            self.assertEqual(f(42), True)
            self.assertEqual(f(0), False)

    def test_bool_accepts_union_types(self):

        codestr = """
            from typing import Optional

            def f(x: Optional[int]) -> bool:
                return bool(x)
        """
        with self.in_module(codestr) as mod:
            f = mod.f
            self.assertFalse(f(None))
            self.assertTrue(f(12))

    def test_list_subclass(self):
        codestr = """
            class C(list):
                pass

            def f() -> C:
                return C()
        """
        with self.in_module(codestr) as mod:
            f = mod.f
            self.assertEqual(f(), [])
            self.assertInBytecode(f, "TP_ALLOC")

    def test_list_subclass_iterable(self):
        codestr = """
            class C(list):
                pass

            def f() -> C:
                return C('abc')
        """
        with self.in_module(codestr) as mod:
            f = mod.f
            self.assertEqual(f(), ["a", "b", "c"])
            self.assertInBytecode(f, "TP_ALLOC")

    def test_checkeddict_new(self):
        codestr = """
            from __static__ import CheckedDict

            def f() -> CheckedDict[str, int]:
                return CheckedDict[str, int]()
        """
        with self.in_module(codestr) as mod:
            f = mod.f
            self.assertEqual(f(), {})
            self.assertInBytecode(
                f,
                "TP_ALLOC",
                (
                    "__static__",
                    "chkdict",
                    (("builtins", "str"), ("builtins", "int")),
                    "!",
                ),
            )
            self.assertInBytecode(
                f,
                "INVOKE_FUNCTION",
                (
                    (
                        "__static__",
                        "chkdict",
                        (("builtins", "str"), ("builtins", "int")),
                        "!",
                        "__init__",
                    ),
                    2,
                ),
            )

    def test_checkeddict_new_2(self):
        codestr = """
            from __static__ import CheckedDict

            def f() -> CheckedDict[str, int]:
                return CheckedDict[str, int]({})
        """
        with self.in_module(codestr) as mod:
            f = mod.f
            self.assertEqual(f(), {})
            self.assertInBytecode(
                f,
                "TP_ALLOC",
                (
                    "__static__",
                    "chkdict",
                    (("builtins", "str"), ("builtins", "int")),
                    "!",
                ),
            )
            self.assertInBytecode(
                f,
                "INVOKE_FUNCTION",
                (
                    (
                        (
                            "__static__",
                            "chkdict",
                            (("builtins", "str"), ("builtins", "int")),
                            "!",
                            "__init__",
                        ),
                        2,
                    )
                ),
            )

    def test_super_init_no_obj_invoke(self):
        codestr = """
            class C:
                def __init__(self):
                    super().__init__()
        """
        with self.in_module(codestr) as mod:
            f = mod.C.__init__
            self.assertNotInBytecode(f, "INVOKE_METHOD")

    def test_super_init_no_load_attr_super(self):
        codestr = """
            x = super

            class B:
                def __init__(self, a):
                    pass


            class D(B):
                def __init__(self):
                    # force a non-optimizable super
                    try:
                        super(1, 2, 3).__init__(a=2)
                    except:
                        pass
                    # and then use the aliased super, we still
                    # have __class__ available
                    x().__init__(a=2)

            def f():
                return D()
        """
        code = self.compile(codestr)
        with self.in_module(codestr) as mod:
            f = mod.f
            D = mod.D
            # super call suppresses jit
            self.assertTrue(D.__init__.__code__.co_flags & CO_SUPPRESS_JIT)
            self.assertTrue(isinstance(f(), D))

    def test_invoke_with_freevars(self):
        codestr = """
            class C:
                def __init__(self) -> None:
                    super().__init__()


            def f() -> C:
                return C()
        """
        code = self.compile(codestr)
        with self.in_module(codestr) as mod:
            f = mod.f
            C = mod.C
            freeze_type(C)
            self.assertInBytecode(f, "INVOKE_FUNCTION")
            self.assertTrue(isinstance(f(), C))

    def test_super_redefined_uses_opt(self):
        codestr = """
            super = super

            class C:
                def __init__(self):
                    super().__init__()
        """
        with self.in_module(codestr) as mod:
            init = mod.C.__init__
            self.assertInBytecode(init, "LOAD_METHOD_SUPER")

    def test_generic_unknown_type_dict(self):
        codestr = """
            from __static__ import CheckedDict
            def make_C():
                class C: pass
                return C
            C = make_C()
            d = CheckedDict[str, C]({})
        """
        with self.in_module(codestr) as mod:
            self.assertEqual(type(mod.d), chkdict[str, object])

    def test_generic_unknown_type_list(self):
        codestr = """
            from __static__ import CheckedList
            def make_C():
                class C: pass
                return C
            C = make_C()
            l = CheckedList[C]([])
        """
        with self.in_module(codestr) as mod:
            self.assertEqual(type(mod.l), chklist[object])

    def test_class_method_call(self):
        codestr = """
            from __static__ import CheckedList
            class B:
                def __init__(self, a):
                    self.a = a

                @classmethod
                def f(cls, *args):
                    return cls(42)

            class D:
                def __init__(self, a, b):
                    self.a = a
                    self.b = b
        """
        with self.in_module(codestr) as mod:
            self.assertInBytecode(mod.B.f, "CALL_FUNCTION", 1)
            self.assertNotInBytecode(mod.B.f, "TP_ALLOC")
