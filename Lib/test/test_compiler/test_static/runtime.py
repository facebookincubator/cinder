from __static__ import chkdict, int64, is_type_static, make_generic_type, StaticGeneric

import asyncio
import cinder
import itertools
import sys
import time
from collections import UserDict
from compiler.consts import CO_STATICALLY_COMPILED
from compiler.pycodegen import PythonCodeGenerator
from compiler.static import StaticCodeGenerator
from compiler.static.types import (
    FAST_LEN_DICT,
    FAST_LEN_INEXACT,
    FAST_LEN_LIST,
    FAST_LEN_SET,
    FAST_LEN_STR,
    FAST_LEN_TUPLE,
    SEQ_LIST,
    SEQ_REPEAT_INEXACT_NUM,
    SEQ_REPEAT_INEXACT_SEQ,
    SEQ_REPEAT_PRIMITIVE_NUM,
    SEQ_REPEAT_REVERSED,
    SEQ_SUBSCR_UNCHECKED,
    SEQ_TUPLE,
    TypedSyntaxError,
)
from copy import deepcopy
from typing import Optional, TypeVar
from unittest import skip, skipIf

from .common import add_fixed_module, bad_ret_type, StaticTestBase, type_mismatch

try:
    import cinderjit
except ImportError:
    cinderjit = None


class StaticRuntimeTests(StaticTestBase):
    def test_bad_slots_qualname_conflict(self):
        with self.assertRaises(ValueError):

            class C:
                __slots__ = ("x",)
                __slot_types__ = {"x": ("__static__", "int32", "#")}
                x = 42

    def test_typed_slots_bad_inst(self):
        class C:
            __slots__ = ("a",)
            __slot_types__ = {"a": ("__static__", "int32", "#")}

        class D:
            pass

        with self.assertRaises(TypeError):
            C.a.__get__(D(), D)

    def test_typed_slots_bad_slots(self):
        with self.assertRaises(TypeError):

            class C:
                __slots__ = ("a",)
                __slot_types__ = None

    def test_typed_slots_bad_slot_dict(self):
        with self.assertRaises(TypeError):

            class C:
                __slots__ = ("__dict__",)
                __slot_types__ = {"__dict__": "object"}

    def test_typed_slots_bad_slot_weakerf(self):
        with self.assertRaises(TypeError):

            class C:
                __slots__ = ("__weakref__",)
                __slot_types__ = {"__weakref__": "object"}

    def test_typed_slots_object(self):
        codestr = """
            class C:
                __slots__ = ('a', )
                __slot_types__ = {'a': (__name__, 'C')}

            inst = C()
        """

        with self.in_module(codestr, code_gen=PythonCodeGenerator) as mod:
            inst, C = mod.inst, mod.C
            self.assertEqual(C.a.__class__.__name__, "typed_descriptor")
            with self.assertRaises(TypeError):
                # type is checked
                inst.a = 42
            with self.assertRaises(TypeError):
                inst.a = None
            with self.assertRaises(AttributeError):
                # is initially unassigned
                inst.a

            # can assign correct type
            inst.a = inst

            # __sizeof__ doesn't include GC header size
            self.assertEqual(inst.__sizeof__(), self.base_size + self.ptr_size)
            # size is +2 words for GC header, one word for reference
            self.assertEqual(sys.getsizeof(inst), self.base_size + (self.ptr_size * 3))

            # subclasses are okay
            class D(C):
                pass

            inst.a = D()

    def test_builtin_object_setattr(self):
        codestr = """
        class C:
            a: int
            def fn(self):
                object.__setattr__(self, "a", 1)
        """
        with self.in_module(codestr, name="t1") as mod:
            C = mod.C
            self.assertInBytecode(C.fn, "LOAD_METHOD", "__setattr__")

            c = C()
            c.fn()
            self.assertEqual(c.a, 1)

    def test_user_defined_class_setattr_defined(self):
        codestr = """
        class E:

            hihello: str

            def __setattr__(self, key: str, val: object):
                object.__setattr__(self, key + "hello", val)

        def fn():
            e = E()
            E.__setattr__(e, "hi", "itsme")
            return e
        """
        with self.in_module(codestr, name="t2") as mod:
            fn = mod.fn
            self.assertInBytecode(
                fn, "INVOKE_FUNCTION", (("t2", "E", "__setattr__"), 3)
            )
            res = fn()
            self.assertEqual(res.hihello, "itsme")

    def test_user_defined_class_setattr_undefined(self):
        codestr = """
        class F:
            hihello: str

        def fn():
            f = F()
            F.__setattr__(f, "hihello", "itsme")
            return f
        """
        with self.in_module(codestr, name="t3") as mod:
            fn = mod.fn
            self.assertInBytecode(fn, "LOAD_METHOD", "__setattr__")
            res = fn()
            self.assertEqual(res.hihello, "itsme")

    def test_allow_weakrefs(self):
        codestr = """
            from __static__ import allow_weakrefs
            import weakref

            @allow_weakrefs
            class C:
                pass

            def f(c: C):
                return weakref.ref(c)
        """
        with self.in_module(codestr) as mod:
            C = mod.C
            c = C()
            ref = mod.f(c)
            self.assertIs(ref(), c)
            del c
            self.assertIs(ref(), None)
            self.assertEqual(C.__slots__, ("__weakref__",))

    def test_generic_type_def_no_create(self):
        from xxclassloader import spamobj

        with self.assertRaises(TypeError):
            spamobj()

    def test_generic_type_def_bad_args(self):
        from xxclassloader import spamobj

        with self.assertRaises(TypeError):
            spamobj[str, int]

    def test_generic_type_def_non_type(self):
        from xxclassloader import spamobj

        with self.assertRaises(TypeError):
            spamobj[42]

    def test_generic_type_inst_okay(self):
        from xxclassloader import spamobj

        o = spamobj[str]()
        o.setstate("abc")

    def test_generic_type_inst_optional_okay(self):
        from xxclassloader import spamobj

        o = spamobj[Optional[str]]()
        o.setstate("abc")
        o.setstate(None)

    def test_generic_type_inst_non_optional_error(self):
        from xxclassloader import spamobj

        o = spamobj[str]()
        with self.assertRaises(TypeError):
            o.setstate(None)

    def test_generic_type_inst_bad_type(self):
        from xxclassloader import spamobj

        o = spamobj[str]()
        with self.assertRaises(TypeError):
            o.setstate(42)

    def test_generic_type_inst_name(self):
        from xxclassloader import spamobj

        self.assertEqual(spamobj[str].__name__, "spamobj[str]")

    def test_generic_type_inst_name_optional(self):
        from xxclassloader import spamobj

        self.assertEqual(spamobj[Optional[str]].__name__, "spamobj[Optional[str]]")

    def test_generic_type_inst_okay_func(self):
        from xxclassloader import spamobj

        o = spamobj[str]()
        f = o.setstate
        f("abc")

    def test_generic_type_inst_optional_okay_func(self):
        from xxclassloader import spamobj

        o = spamobj[Optional[str]]()
        f = o.setstate
        f("abc")
        f(None)

    def test_generic_type_inst_non_optional_error_func(self):
        from xxclassloader import spamobj

        o = spamobj[str]()
        f = o.setstate
        with self.assertRaises(TypeError):
            f(None)

    def test_generic_type_inst_bad_type_func(self):
        from xxclassloader import spamobj

        o = spamobj[str]()
        f = o.setstate
        with self.assertRaises(TypeError):
            f(42)

    def test_generic_int_funcs(self):
        from xxclassloader import spamobj

        o = spamobj[str]()
        o.setint(42)
        self.assertEqual(o.getint8(), 42)
        self.assertEqual(o.getint16(), 42)
        self.assertEqual(o.getint32(), 42)

    def test_generic_uint_funcs(self):
        from xxclassloader import spamobj

        o = spamobj[str]()
        o.setuint64(42)
        self.assertEqual(o.getuint8(), 42)
        self.assertEqual(o.getuint16(), 42)
        self.assertEqual(o.getuint32(), 42)
        self.assertEqual(o.getuint64(), 42)

    def test_generic_int_funcs_overflow(self):
        from xxclassloader import spamobj

        o = spamobj[str]()
        o.setuint64(42)
        for i, f in enumerate([o.setint8, o.setint16, o.setint32, o.setint]):
            with self.assertRaises(OverflowError):
                x = -(1 << ((8 << i) - 1)) - 1
                f(x)
            with self.assertRaises(OverflowError):
                x = 1 << ((8 << i) - 1)
                f(x)

    def test_generic_uint_funcs_overflow(self):
        from xxclassloader import spamobj

        o = spamobj[str]()
        o.setuint64(42)
        for f in [o.setuint8, o.setuint16, o.setuint32, o.setuint64]:
            with self.assertRaises(OverflowError):
                f(-1)
        for i, f in enumerate([o.setuint8, o.setuint16, o.setuint32, o.setuint64]):
            with self.assertRaises(OverflowError):
                x = (1 << (8 << i)) + 1
                f(x)

    def test_generic_type_int_func(self):
        from xxclassloader import spamobj

        o = spamobj[str]()
        o.setint(42)
        self.assertEqual(o.getint(), 42)
        with self.assertRaises(TypeError):
            o.setint("abc")

    def test_generic_type_str_func(self):
        from xxclassloader import spamobj

        o = spamobj[str]()
        o.setstr("abc")
        self.assertEqual(o.getstr(), "abc")
        with self.assertRaises(TypeError):
            o.setstr(42)

    def test_generic_type_bad_arg_cnt(self):
        from xxclassloader import spamobj

        o = spamobj[str]()
        with self.assertRaises(TypeError):
            o.setstr()
        with self.assertRaises(TypeError):
            o.setstr("abc", "abc")

    def test_generic_type_bad_arg_cnt(self):
        from xxclassloader import spamobj

        o = spamobj[str]()
        self.assertEqual(o.twoargs(1, 2), 3)

    def test_typed_slots_one_missing(self):
        codestr = """
            class C:
                __slots__ = ('a', 'b')
                __slot_types__ = {'a': (__name__, 'C')}

            inst = C()
        """

        with self.in_module(codestr, code_gen=PythonCodeGenerator) as mod:
            inst, C = mod.inst, mod.C
            self.assertEqual(C.a.__class__.__name__, "typed_descriptor")
            with self.assertRaises(TypeError):
                # type is checked
                inst.a = 42

    def test_typed_slots_optional_object(self):
        codestr = """
            class C:
                __slots__ = ('a', )
                __slot_types__ = {'a': (__name__, 'C', '?')}

            inst = C()
        """

        with self.in_module(codestr, code_gen=PythonCodeGenerator) as mod:
            inst = mod.inst
            inst.a = None
            self.assertEqual(inst.a, None)

    def test_typed_slots_private(self):
        codestr = """
            class C:
                __slots__ = ('__a', )
                __slot_types__ = {'__a': (__name__, 'C', '?')}
                def __init__(self):
                    self.__a = None

            inst = C()
        """

        with self.in_module(codestr, code_gen=PythonCodeGenerator) as mod:
            inst = mod.inst
            self.assertEqual(inst._C__a, None)
            inst._C__a = inst
            self.assertEqual(inst._C__a, inst)
            inst._C__a = None
            self.assertEqual(inst._C__a, None)

    def test_typed_slots_optional_not_defined(self):
        codestr = """
            class C:
                __slots__ = ('a', )
                __slot_types__ = {'a': (__name__, 'D', '?')}

                def __init__(self):
                    self.a = None

            inst = C()

            class D:
                pass
        """

        with self.in_module(codestr, code_gen=PythonCodeGenerator) as mod:
            inst = mod.inst
            inst.a = None
            self.assertEqual(inst.a, None)

    def test_typed_slots_alignment(self):
        return
        codestr = """
            class C:
                __slots__ = ('a', 'b')
                __slot_types__ {'a': ('__static__', 'int16')}

            inst = C()
        """

        with self.in_module(codestr, code_gen=PythonCodeGenerator) as mod:
            inst = mod.inst
            inst.a = None
            self.assertEqual(inst.a, None)

    def test_invoke_function(self):
        my_int = "12345"
        codestr = f"""
        def x(a: str, b: int) -> str:
            return a + str(b)

        def test() -> str:
            return x("hello", {my_int})
        """
        c = self.compile(codestr, modname="foo.py")
        test = self.find_code(c, "test")
        self.assertInBytecode(test, "INVOKE_FUNCTION", (("foo.py", "x"), 2))
        with self.in_module(codestr) as mod:
            test_callable = mod.test
            self.assertEqual(test_callable(), "hello" + my_int)

    def test_awaited_invoke_function(self):
        codestr = """
            async def f() -> int:
                return 1

            async def g() -> int:
                return await f()
        """
        with self.in_strict_module(codestr) as mod:
            self.assertInBytecode(mod.g, "INVOKE_FUNCTION", ((mod.__name__, "f"), 0))
            self.assertNotInBytecode(mod.g, "CAST")
            self.assertEqual(asyncio.run(mod.g()), 1)

    def test_awaited_invoke_function_unjitable(self):
        codestr = """
            X = 42
            async def f() -> int:
                global X; X = 42; del X
                return 1

            async def g() -> int:
                return await f()
        """
        with self.in_strict_module(codestr) as mod:
            self.assertInBytecode(
                mod.g,
                "INVOKE_FUNCTION",
                ((mod.__name__, "f"), 0),
            )
            self.assertEqual(asyncio.run(mod.g()), 1)
            self.assert_not_jitted(mod.f)

    def test_awaited_invoke_function_with_args(self):
        codestr = """
            async def f(a: int, b: int) -> int:
                return a + b

            async def g() -> int:
                return await f(1, 2)
        """
        with self.in_strict_module(codestr) as mod:
            self.assertInBytecode(
                mod.g,
                "INVOKE_FUNCTION",
                ((mod.__name__, "f"), 2),
            )
            self.assertEqual(asyncio.run(mod.g()), 3)

            # exercise shadowcode, INVOKE_FUNCTION_CACHED
            self.make_async_func_hot(mod.g)
            self.assertEqual(asyncio.run(mod.g()), 3)

    def test_awaited_invoke_function_indirect_with_args(self):
        codestr = """
            async def f(a: int, b: int) -> int:
                return a + b

            async def g() -> int:
                return await f(1, 2)
        """
        with self.in_module(codestr) as mod:
            g = mod.g
            self.assertInBytecode(
                g,
                "INVOKE_FUNCTION",
                ((mod.__name__, "f"), 2),
            )
            self.assertEqual(asyncio.run(g()), 3)

            # exercise shadowcode, INVOKE_FUNCTION_INDIRECT_CACHED
            self.make_async_func_hot(g)
            self.assertEqual(asyncio.run(g()), 3)

    def test_awaited_invoke_function_future(self):
        codestr = """
            from asyncio import ensure_future

            async def h() -> int:
                return 1

            async def g() -> None:
                await ensure_future(h())

            async def f():
                await g()
        """
        with self.in_strict_module(codestr) as mod:
            self.assertInBytecode(
                mod.f,
                "INVOKE_FUNCTION",
                ((mod.__name__, "g"), 0),
            )
            asyncio.run(mod.f())

            # exercise shadowcode
            self.make_async_func_hot(mod.f)
            asyncio.run(mod.f())

    def test_awaited_invoke_method(self):
        codestr = """
            class C:
                async def f(self) -> int:
                    return 1

                async def g(self) -> int:
                    return await self.f()
        """
        with self.in_strict_module(codestr) as mod:
            self.assertInBytecode(
                mod.C.g, "INVOKE_METHOD", ((mod.__name__, "C", "f"), 0)
            )
            self.assertEqual(asyncio.run(mod.C().g()), 1)

    def test_awaited_invoke_method_with_args(self):
        codestr = """
            class C:
                async def f(self, a: int, b: int) -> int:
                    return a + b

                async def g(self) -> int:
                    return await self.f(1, 2)
        """
        with self.in_strict_module(codestr) as mod:
            self.assertInBytecode(
                mod.C.g,
                "INVOKE_METHOD",
                ((mod.__name__, "C", "f"), 2),
            )
            self.assertEqual(asyncio.run(mod.C().g()), 3)

            # exercise shadowcode, INVOKE_METHOD_CACHED
            async def make_hot():
                c = mod.C()
                for i in range(50):
                    await c.g()

            asyncio.run(make_hot())
            self.assertEqual(asyncio.run(mod.C().g()), 3)

    def test_awaited_invoke_method_future(self):
        codestr = """
            from asyncio import ensure_future

            async def h() -> int:
                return 1

            class C:
                async def g(self) -> None:
                    await ensure_future(h())

            async def f():
                c = C()
                await c.g()
        """
        with self.in_strict_module(codestr) as mod:
            self.assertInBytecode(
                mod.f,
                "INVOKE_FUNCTION",
                ((mod.__name__, "C", "g"), 1),
            )
            asyncio.run(mod.f())

            # exercise shadowcode, INVOKE_METHOD_CACHED
            self.make_async_func_hot(mod.f)
            asyncio.run(mod.f())

    def test_generic_type_args(self):
        T = TypeVar("T")
        U = TypeVar("U")

        class C(StaticGeneric[T, U]):
            pass

        c_t = make_generic_type(C, (T, int))
        self.assertEqual(c_t.__parameters__, (T,))
        c_t_s = make_generic_type(c_t, (str,))
        self.assertEqual(c_t_s.__name__, "C[str, int]")
        c_u = make_generic_type(C, (int, U))
        self.assertEqual(c_u.__parameters__, (U,))
        c_u_t = make_generic_type(c_u, (str,))
        self.assertEqual(c_u_t.__name__, "C[int, str]")
        self.assertFalse(hasattr(c_u_t, "__parameters__"))

        c_u_t_1 = make_generic_type(c_u, (int,))
        c_u_t_2 = make_generic_type(c_t, (int,))
        self.assertEqual(c_u_t_1.__name__, "C[int, int]")
        self.assertIs(c_u_t_1, c_u_t_2)

    def test_nested_generic(self):
        S = TypeVar("S")
        T = TypeVar("T")
        U = TypeVar("U")

        class F(StaticGeneric[U]):
            pass

        class C(StaticGeneric[T]):
            pass

        A = F[S]
        self.assertEqual(A.__parameters__, (S,))
        X = C[F[T]]
        self.assertEqual(X.__parameters__, (T,))

    def test_nonarray_len(self):
        codestr = """
            class Lol:
                def __len__(self):
                    return 421

            def y():
                return len(Lol())
        """
        y = self.find_code(self.compile(codestr, modname="foo"), name="y")
        self.assertNotInBytecode(y, "FAST_LEN")
        with self.in_module(codestr) as mod:
            y = mod.y
            self.assertEqual(y(), 421)

    def test_seq_repeat_list(self):
        codestr = """
            def f():
                l = [1, 2]
                return l * 2
        """
        f = self.find_code(self.compile(codestr))
        self.assertInBytecode(f, "SEQUENCE_REPEAT", SEQ_LIST)
        with self.in_module(codestr) as mod:
            self.assertEqual(mod.f(), [1, 2, 1, 2])

    def test_seq_repeat_list_reversed(self):
        codestr = """
            def f():
                l = [1, 2]
                return 2 * l
        """
        f = self.find_code(self.compile(codestr))
        self.assertInBytecode(f, "SEQUENCE_REPEAT", SEQ_LIST | SEQ_REPEAT_REVERSED)
        with self.in_module(codestr) as mod:
            self.assertEqual(mod.f(), [1, 2, 1, 2])

    def test_seq_repeat_primitive(self):
        codestr = """
            from __static__ import int64

            def f():
                x: int64 = 2
                l = [1, 2]
                return l * x
        """
        f = self.find_code(self.compile(codestr))
        self.assertInBytecode(f, "SEQUENCE_REPEAT", SEQ_LIST | SEQ_REPEAT_PRIMITIVE_NUM)
        with self.in_module(codestr) as mod:
            self.assertEqual(mod.f(), [1, 2, 1, 2])

    def test_seq_repeat_primitive_reversed(self):
        codestr = """
            from __static__ import int64

            def f():
                x: int64 = 2
                l = [1, 2]
                return x * l
        """
        f = self.find_code(self.compile(codestr))
        self.assertInBytecode(
            f,
            "SEQUENCE_REPEAT",
            SEQ_LIST | SEQ_REPEAT_REVERSED | SEQ_REPEAT_PRIMITIVE_NUM,
        )
        with self.in_module(codestr) as mod:
            self.assertEqual(mod.f(), [1, 2, 1, 2])

    def test_seq_repeat_tuple(self):
        codestr = """
            def f():
                t = (1, 2)
                return t * 2
        """
        f = self.find_code(self.compile(codestr))
        self.assertInBytecode(f, "SEQUENCE_REPEAT", SEQ_TUPLE)
        with self.in_module(codestr) as mod:
            self.assertEqual(mod.f(), (1, 2, 1, 2))

    def test_seq_repeat_tuple_reversed(self):
        codestr = """
            def f():
                t = (1, 2)
                return 2 * t
        """
        f = self.find_code(self.compile(codestr))
        self.assertInBytecode(f, "SEQUENCE_REPEAT", SEQ_TUPLE | SEQ_REPEAT_REVERSED)
        with self.in_module(codestr) as mod:
            self.assertEqual(mod.f(), (1, 2, 1, 2))

    def test_seq_repeat_inexact_list(self):
        codestr = """
            from typing import List

            def f(l: List[int]):
                return l * 2
        """
        f = self.find_code(self.compile(codestr))
        self.assertInBytecode(f, "SEQUENCE_REPEAT", SEQ_LIST | SEQ_REPEAT_INEXACT_SEQ)
        with self.in_module(codestr) as mod:
            self.assertEqual(mod.f([1, 2]), [1, 2, 1, 2])

            class MyList(list):
                def __mul__(self, other):
                    return "RESULT"

            self.assertEqual(mod.f(MyList([1, 2])), "RESULT")

    def test_seq_repeat_inexact_tuple(self):

        codestr = """
            from typing import Tuple

            def f(t: Tuple[int]):
                return t * 2
        """
        f = self.find_code(self.compile(codestr))
        self.assertInBytecode(f, "SEQUENCE_REPEAT", SEQ_TUPLE | SEQ_REPEAT_INEXACT_SEQ)
        with self.in_module(codestr) as mod:
            self.assertEqual(mod.f((1, 2)), (1, 2, 1, 2))

            class MyTuple(tuple):
                def __mul__(self, other):
                    return "RESULT"

            self.assertEqual(mod.f(MyTuple((1, 2))), "RESULT")

    def test_seq_repeat_inexact_num(self):
        codestr = """
            def f(num: int):

                return num * [1, 2]
        """
        f = self.find_code(self.compile(codestr))
        self.assertInBytecode(
            f,
            "SEQUENCE_REPEAT",
            SEQ_LIST | SEQ_REPEAT_INEXACT_NUM | SEQ_REPEAT_REVERSED,
        )
        with self.in_module(codestr) as mod:
            self.assertEqual(mod.f(2), [1, 2, 1, 2])

            class MyInt(int):
                def __mul__(self, other):
                    return "RESULT"

            self.assertEqual(mod.f(MyInt(2)), "RESULT")

    def test_posix_clock_gettime_ns(self):
        codestr = """
        from __static__ import box, posix_clock_gettime_ns

        def test() -> int:
            x = posix_clock_gettime_ns()
            return box(x)
        """
        with self.in_module(codestr) as mod:
            test = mod.test
            expected = time.clock_gettime_ns(time.CLOCK_MONOTONIC)
            res = test()
            ten_sec_in_nanosec = 1e10
            self.assertEqual(type(res), int)
            # It is pretty reasonable to expect this test to finish within +/- 10 sec
            self.assertTrue(
                expected - ten_sec_in_nanosec <= res <= expected + ten_sec_in_nanosec
            )

    def test_fast_len_list(self):
        codestr = """
        def f():
            l = [1, 2, 3, 4, 5, 6, 7]
            return len(l)
        """
        c = self.compile(codestr, modname="foo.py")
        f = self.find_code(c, "f")
        self.assertInBytecode(f, "FAST_LEN", FAST_LEN_LIST)
        with self.in_module(codestr) as mod:
            f = mod.f
            self.assertEqual(f(), 7)

    def test_fast_len_str(self):
        codestr = """
        def f():
            l = "my str!"
            return len(l)
        """
        c = self.compile(codestr, modname="foo.py")
        f = self.find_code(c, "f")
        self.assertInBytecode(f, "FAST_LEN", FAST_LEN_STR)
        with self.in_module(codestr) as mod:
            f = mod.f
            self.assertEqual(f(), 7)

    def test_fast_len_str_unicode_chars(self):
        codestr = """
        def f():
            l = "\U0001F923"  # ROFL emoji
            return len(l)
        """
        c = self.compile(codestr, modname="foo.py")
        f = self.find_code(c, "f")
        self.assertInBytecode(f, "FAST_LEN", FAST_LEN_STR)
        with self.in_module(codestr) as mod:
            f = mod.f
            self.assertEqual(f(), 1)

    def test_fast_len_tuple(self):
        codestr = """
        def f(a, b):
            l = (a, b)
            return len(l)
        """
        c = self.compile(codestr, modname="foo.py")
        f = self.find_code(c, "f")
        self.assertInBytecode(f, "FAST_LEN", FAST_LEN_TUPLE)
        with self.in_module(codestr) as mod:
            f = mod.f
            self.assertEqual(f("a", "b"), 2)

    def test_fast_len_set(self):
        codestr = """
        def f(a, b):
            l = {a, b}
            return len(l)
        """
        c = self.compile(codestr, modname="foo.py")
        f = self.find_code(c, "f")
        self.assertInBytecode(f, "FAST_LEN", FAST_LEN_SET)
        with self.in_module(codestr) as mod:
            f = mod.f
            self.assertEqual(f("a", "b"), 2)

    def test_fast_len_dict(self):
        codestr = """
        def f():
            l = {1: 'a', 2: 'b', 3: 'c', 4: 'd'}
            return len(l)
        """
        c = self.compile(codestr, modname="foo.py")
        f = self.find_code(c, "f")
        self.assertInBytecode(f, "FAST_LEN", FAST_LEN_DICT)
        with self.in_module(codestr) as mod:
            f = mod.f
            self.assertEqual(f(), 4)

    def test_fast_len_conditional_list(self):
        codestr = """
            def f(n: int) -> bool:
                l = [i for i in range(n)]
                if l:
                    return True
                return False
            """
        c = self.compile(codestr, modname="foo.py")
        f = self.find_code(c, "f")
        self.assertInBytecode(f, "FAST_LEN", FAST_LEN_LIST)
        with self.in_module(codestr) as mod:
            f = mod.f
            for length in [0, 7]:
                self.assertEqual(f(length), length > 0)

    def test_fast_len_conditional_str(self):
        codestr = """
            def f(n: int) -> bool:
                l = f"{'a' * n}"
                if l:
                    return True
                return False
            """
        c = self.compile(codestr, modname="foo.py")
        f = self.find_code(c, "f")
        self.assertInBytecode(f, "FAST_LEN", FAST_LEN_STR)
        with self.in_module(codestr) as mod:
            f = mod.f
            for length in [0, 7]:
                self.assertEqual(f(length), length > 0)

    def test_fast_len_loop_conditional_list(self):
        codestr = """
            def f(n: int) -> bool:
                l = [i for i in range(n)]
                while l:
                    return True
                return False
            """
        c = self.compile(codestr, modname="foo.py")
        f = self.find_code(c, "f")
        self.assertInBytecode(f, "FAST_LEN", FAST_LEN_LIST)
        with self.in_module(codestr) as mod:
            f = mod.f
            for length in [0, 7]:
                self.assertEqual(f(length), length > 0)

    def test_fast_len_loop_conditional_str(self):
        codestr = """
            def f(n: int) -> bool:
                l = f"{'a' * n}"
                while l:
                    return True
                return False
            """
        c = self.compile(codestr, modname="foo.py")
        f = self.find_code(c, "f")
        self.assertInBytecode(f, "FAST_LEN", FAST_LEN_STR)
        with self.in_module(codestr) as mod:
            f = mod.f
            for length in [0, 7]:
                self.assertEqual(f(length), length > 0)

    def test_fast_len_loop_conditional_tuple(self):
        codestr = """
            def f(n: int) -> bool:
                l = tuple(i for i in range(n))
                while l:
                    return True
                return False
            """
        c = self.compile(codestr, modname="foo.py")
        f = self.find_code(c, "f")
        self.assertInBytecode(f, "FAST_LEN", FAST_LEN_TUPLE)
        with self.in_module(codestr) as mod:
            f = mod.f
            for length in [0, 7]:
                self.assertEqual(f(length), length > 0)

    def test_fast_len_loop_conditional_set(self):
        codestr = """
            def f(n: int) -> bool:
                l = {i for i in range(n)}
                while l:
                    return True
                return False
            """
        c = self.compile(codestr, modname="foo.py")
        f = self.find_code(c, "f")
        self.assertInBytecode(f, "FAST_LEN", FAST_LEN_SET)
        with self.in_module(codestr) as mod:
            f = mod.f
            for length in [0, 7]:
                self.assertEqual(f(length), length > 0)

    def test_fast_len_conditional_tuple(self):
        codestr = """
            def f(n: int) -> bool:
                l = tuple(i for i in range(n))
                if l:
                    return True
                return False
            """
        c = self.compile(codestr, modname="foo.py")
        f = self.find_code(c, "f")
        self.assertInBytecode(f, "FAST_LEN", FAST_LEN_TUPLE)
        with self.in_module(codestr) as mod:
            f = mod.f
            for length in [0, 7]:
                self.assertEqual(f(length), length > 0)

    def test_fast_len_conditional_set(self):
        codestr = """
            def f(n: int) -> bool:
                l = {i for i in range(n)}
                if l:
                    return True
                return False
            """
        c = self.compile(codestr, modname="foo.py")
        f = self.find_code(c, "f")
        self.assertInBytecode(f, "FAST_LEN", FAST_LEN_SET)
        with self.in_module(codestr) as mod:
            f = mod.f
            for length in [0, 7]:
                self.assertEqual(f(length), length > 0)

    def test_fast_len_conditional_dict(self):
        codestr = """
            def f(n: int) -> bool:
                l = {i: i for i in range(n)}
                if l:
                    return True
                return False
            """
        c = self.compile(codestr, modname="foo.py")
        f = self.find_code(c, "f")
        self.assertInBytecode(f, "FAST_LEN", FAST_LEN_DICT)
        with self.in_module(codestr) as mod:
            f = mod.f
            for length in [0, 7]:
                self.assertEqual(f(length), length > 0)

    def test_fast_len_conditional_list_subclass(self):
        codestr = """
            from typing import List

            class MyList(list):
                def __len__(self):
                    return 1729

            def f(n: int, flag: bool) -> bool:
                x: List[int] = [i for i in range(n)]
                if flag:
                    x = MyList([i for i in range(n)])
                if x:
                    return True
                return False
            """
        c = self.compile(codestr, modname="foo.py")
        f = self.find_code(c, "f")
        self.assertInBytecode(f, "FAST_LEN", FAST_LEN_LIST | FAST_LEN_INEXACT)
        with self.in_module(codestr) as mod:
            f = mod.f
            for boolean, length in itertools.product((True, False), [0, 7]):
                self.assertEqual(
                    f(length, boolean),
                    length > 0 or boolean,
                    f"length={length}, flag={boolean}",
                )

    def test_fast_len_conditional_str_subclass(self):
        codestr = """
            class MyStr(str):
                def __len__(self):
                    return 1729

            def f(n: int, flag: bool) -> bool:
                x: str = f"{'a' * n}"
                if flag:
                    x = MyStr(f"{'a' * n}")
                if x:
                    return True
                return False
            """
        c = self.compile(codestr, modname="foo.py")
        f = self.find_code(c, "f")
        self.assertInBytecode(f, "FAST_LEN", FAST_LEN_STR | FAST_LEN_INEXACT)
        with self.in_module(codestr) as mod:
            f = mod.f
            for boolean, length in itertools.product((True, False), [0, 7]):
                self.assertEqual(
                    f(length, boolean),
                    length > 0 or boolean,
                    f"length={length}, flag={boolean}",
                )

    def test_fast_len_conditional_tuple_subclass(self):
        codestr = """
            class Mytuple(tuple):
                def __len__(self):
                    return 1729

            def f(n: int, flag: bool) -> bool:
                x = tuple(i for i in range(n))
                if flag:
                    x = Mytuple([i for i in range(n)])
                if x:
                    return True
                return False
            """
        c = self.compile(codestr, modname="foo.py")
        f = self.find_code(c, "f")
        self.assertInBytecode(f, "FAST_LEN", FAST_LEN_TUPLE | FAST_LEN_INEXACT)
        with self.in_module(codestr) as mod:
            f = mod.f
            for boolean, length in itertools.product((True, False), [0, 7]):
                self.assertEqual(
                    f(length, boolean),
                    length > 0 or boolean,
                    f"length={length}, flag={boolean}",
                )

    def test_fast_len_conditional_set_subclass(self):
        codestr = """
            class Myset(set):
                def __len__(self):
                    return 1729

            def f(n: int, flag: bool) -> bool:
                x = set(i for i in range(n))
                if flag:
                    x = Myset([i for i in range(n)])
                if x:
                    return True
                return False
            """
        c = self.compile(codestr, modname="foo.py")
        f = self.find_code(c, "f")
        self.assertInBytecode(f, "FAST_LEN", FAST_LEN_SET | FAST_LEN_INEXACT)
        with self.in_module(codestr) as mod:
            f = mod.f
            for boolean, length in itertools.product((True, False), [0, 7]):
                self.assertEqual(
                    f(length, boolean),
                    length > 0 or boolean,
                    f"length={length}, flag={boolean}",
                )

    def test_fast_len_conditional_list_funcarg(self):
        codestr = """
            def z(b: object) -> bool:
                return bool(b)

            def f(n: int) -> bool:
                l = [i for i in range(n)]
                if z(l):
                    return True
                return False
            """
        c = self.compile(codestr, modname="foo.py")
        f = self.find_code(c, "f")
        # Since the list is given to z(), do not optimize the check
        # with FAST_LEN
        self.assertNotInBytecode(f, "FAST_LEN")
        with self.in_module(codestr) as mod:
            f = mod.f
            for length in [0, 7]:
                self.assertEqual(f(length), length > 0)

    def test_fast_len_conditional_str_funcarg(self):
        codestr = """
            def z(b: object) -> bool:
                return bool(b)

            def f(n: int) -> bool:
                l = f"{'a' * n}"
                if z(l):
                    return True
                return False
            """
        c = self.compile(codestr, modname="foo.py")
        f = self.find_code(c, "f")
        # Since the list is given to z(), do not optimize the check
        # with FAST_LEN
        self.assertNotInBytecode(f, "FAST_LEN")
        with self.in_module(codestr) as mod:
            f = mod.f
            for length in [0, 7]:
                self.assertEqual(f(length), length > 0)

    def test_fast_len_conditional_tuple_funcarg(self):
        codestr = """
            def z(b: object) -> bool:
                return bool(b)

            def f(n: int) -> bool:
                l = tuple(i for i in range(n))
                if z(l):
                    return True
                return False
            """
        c = self.compile(codestr, modname="foo.py")
        f = self.find_code(c, "f")
        # Since the tuple is given to z(), do not optimize the check
        # with FAST_LEN
        self.assertNotInBytecode(f, "FAST_LEN")
        with self.in_module(codestr) as mod:
            f = mod.f
            for length in [0, 7]:
                self.assertEqual(f(length), length > 0)

    def test_fast_len_conditional_set_funcarg(self):
        codestr = """
            def z(b: object) -> bool:
                return bool(b)

            def f(n: int) -> bool:
                l = set(i for i in range(n))
                if z(l):
                    return True
                return False
            """
        c = self.compile(codestr, modname="foo.py")
        f = self.find_code(c, "f")
        # Since the set is given to z(), do not optimize the check
        # with FAST_LEN
        self.assertNotInBytecode(f, "FAST_LEN")
        with self.in_module(codestr) as mod:
            f = mod.f
            for length in [0, 7]:
                self.assertEqual(f(length), length > 0)

    def test_fast_len_conditional_dict_funcarg(self):
        codestr = """
            def z(b) -> bool:
                return bool(b)

            def f(n: int) -> bool:
                l = {i: i for i in range(n)}
                if z(l):
                    return True
                return False
            """
        c = self.compile(codestr, modname="foo.py")
        f = self.find_code(c, "f")
        # Since the dict is given to z(), do not optimize the check
        # with FAST_LEN
        self.assertNotInBytecode(f, "FAST_LEN")
        with self.in_module(codestr) as mod:
            f = mod.f
            for length in [0, 7]:
                self.assertEqual(f(length), length > 0)

    def test_fast_len_list_subclass(self):
        codestr = """
        class mylist(list):
            def __len__(self):
                return 1111

        def f():
            l = mylist([1, 2])
            return len(l)
        """
        c = self.compile(codestr, modname="foo.py")
        f = self.find_code(c, "f")
        self.assertNotInBytecode(f, "FAST_LEN")
        with self.in_module(codestr) as mod:
            f = mod.f
            self.assertEqual(f(), 1111)

    def test_fast_len_str_subclass(self):
        codestr = """
        class mystr(str):
            def __len__(self):
                return 1111

        def f():
            s = mystr("a")
            return len(s)
        """
        c = self.compile(codestr, modname="foo.py")
        f = self.find_code(c, "f")
        self.assertNotInBytecode(f, "FAST_LEN")
        with self.in_module(codestr) as mod:
            f = mod.f
            self.assertEqual(f(), 1111)

    def test_fast_len_tuple_subclass(self):
        codestr = """
        class mytuple(tuple):
            def __len__(self):
                return 1111

        def f():
            l = mytuple([1, 2])
            return len(l)
        """
        c = self.compile(codestr, modname="foo.py")
        f = self.find_code(c, "f")
        self.assertNotInBytecode(f, "FAST_LEN")
        with self.in_module(codestr) as mod:
            f = mod.f
            self.assertEqual(f(), 1111)

    def test_fast_len_set_subclass(self):
        codestr = """
        class myset(set):
            def __len__(self):
                return 1111

        def f():
            l = myset([1, 2])
            return len(l)
        """
        c = self.compile(codestr, modname="foo.py")
        f = self.find_code(c, "f")
        self.assertNotInBytecode(f, "FAST_LEN")
        with self.in_module(codestr) as mod:
            f = mod.f
            self.assertEqual(f(), 1111)

    def test_fast_len_dict_subclass(self):
        codestr = """
        from typing import Dict

        class mydict(Dict[str, int]):
            def __len__(self):
                return 1111

        def f():
            l = mydict(a=1, b=2)
            return len(l)
        """
        c = self.compile(codestr, modname="foo.py")
        f = self.find_code(c, "f")
        self.assertNotInBytecode(f, "FAST_LEN")
        with self.in_module(codestr) as mod:
            f = mod.f
            self.assertEqual(f(), 1111)

    def test_fast_len_list_subclass_2(self):
        codestr = """
        class mylist(list):
            def __len__(self):
                return 1111

        def f(x):
            l = [1, 2]
            if x:
                l = mylist([1, 2])
            return len(l)
        """
        c = self.compile(codestr, modname="foo.py")
        f = self.find_code(c, "f")
        self.assertInBytecode(f, "FAST_LEN", FAST_LEN_LIST | FAST_LEN_INEXACT)
        with self.in_module(codestr) as mod:
            f = mod.f
            self.assertEqual(f(True), 1111)

    def test_fast_len_str_subclass_2(self):
        codestr = """
        class mystr(str):
            def __len__(self):
                return 1111

        def f(x):
            s = "abc"
            if x:
                s = mystr("pqr")
            return len(s)
        """
        c = self.compile(codestr, modname="foo.py")
        f = self.find_code(c, "f")
        self.assertInBytecode(f, "FAST_LEN", FAST_LEN_STR | FAST_LEN_INEXACT)
        with self.in_module(codestr) as mod:
            f = mod.f
            self.assertEqual(f(True), 1111)
            self.assertEqual(f(False), 3)

    def test_fast_len_tuple_subclass_2(self):
        codestr = """
        class mytuple(tuple):
            def __len__(self):
                return 1111

        def f(x, a, b):
            l = (a, b)
            if x:
                l = mytuple([a, b])
            return len(l)
        """
        c = self.compile(codestr, modname="foo.py")
        f = self.find_code(c, "f")
        self.assertInBytecode(f, "FAST_LEN", FAST_LEN_TUPLE | FAST_LEN_INEXACT)
        with self.in_module(codestr) as mod:
            f = mod.f
            self.assertEqual(f(True, 1, 2), 1111)

    def test_fast_len_dict_subclass_2(self):
        codestr = """
        from typing import Dict

        class mydict(Dict[str, int]):
            def __len__(self):
                return 1111

        def f(x, a, b):
            l: Dict[str, int] = {'c': 3}
            if x:
                l = mydict(a=1, b=2)
            return len(l)
        """
        c = self.compile(codestr, modname="foo.py")
        f = self.find_code(c, "f")
        self.assertInBytecode(f, "FAST_LEN", FAST_LEN_DICT | FAST_LEN_INEXACT)
        with self.in_module(codestr) as mod:
            f = mod.f
            self.assertEqual(f(True, 1, 2), 1111)

    def test_fast_len_set_subclass_2(self):
        codestr = """
        class myset(set):
            def __len__(self):
                return 1111

        def f(x, a, b):
            l = {a, b}
            if x:
                l = myset([a, b])
            return len(l)
        """
        c = self.compile(codestr, modname="foo.py")
        f = self.find_code(c, "f")
        self.assertInBytecode(f, "FAST_LEN", FAST_LEN_SET | FAST_LEN_INEXACT)
        with self.in_module(codestr) as mod:
            f = mod.f
            self.assertEqual(f(True, 1, 2), 1111)

    def test_dynamic_type_param(self):
        """DYNAMIC as type param of generic doesn't render the whole type DYNAMIC."""
        codestr = """
            from __static__ import int64, clen
            from nonstatic import Foo
            from typing import Dict

            def f(d: Dict[Foo, int]):
                x: int64 = clen(d)
        """
        self.compile(codestr)

    def test_checked_dict(self):
        x = chkdict[str, str]()
        x["abc"] = "foo"
        self.assertEqual(repr(x), "{'abc': 'foo'}")
        x = chkdict[str, int]()
        x["abc"] = 42
        x = chkdict[int, str]()
        x[42] = "abc"

    def test_checked_dict_type_name(self):
        self.assertEqual(chkdict.__name__, "chkdict[K, V]")
        x = chkdict[str, str]
        self.assertEqual(x.__name__, "chkdict[str, str]")

    def test_checked_dict_optional(self):
        x = chkdict[str, Optional[str]]()
        x["abc"] = None
        x = chkdict[Optional[str], str]()
        x[None] = "abc"

    def test_checked_dict_nonoptional(self):
        x = chkdict[str, Optional[str]]()
        with self.assertRaises(TypeError):
            x[None] = "abc"
        x = chkdict[Optional[str], str]()
        with self.assertRaises(TypeError):
            x["abc"] = None

    def test_checked_dict_types_enforced(self):
        x = chkdict[str, str]()
        with self.assertRaises(TypeError):
            x[42] = "abc"
        self.assertEqual(x, {})
        with self.assertRaises(TypeError):
            x["abc"] = 42
        self.assertEqual(x, {})

        x = chkdict[str, int]()
        with self.assertRaises(TypeError):
            x[42] = 42
        self.assertEqual(x, {})
        with self.assertRaises(TypeError):
            x["abc"] = "abc"
        self.assertEqual(x, {})

    def test_checked_dict_ctor(self):
        self.assertEqual(chkdict[str, str](x="abc"), {"x": "abc"})
        self.assertEqual(chkdict[str, int](x=42), {"x": 42})
        self.assertEqual(chkdict[str, str]({"x": "abc"}), {"x": "abc"})
        self.assertEqual(chkdict[str, str]([("a", "b")]), {"a": "b"})
        self.assertEqual(chkdict[str, str]([("a", "b")]), {"a": "b"})
        self.assertEqual(chkdict[str, str](chkdict[str, str](x="abc")), {"x": "abc"})
        self.assertEqual(chkdict[str, str](chkdict[str, object](x="abc")), {"x": "abc"})
        self.assertEqual(chkdict[str, str](UserDict(x="abc")), {"x": "abc"})
        self.assertEqual(chkdict[str, str](UserDict(x="abc"), x="foo"), {"x": "foo"})

    def test_checked_dict_bad_ctor(self):
        with self.assertRaises(TypeError):
            chkdict[str, str](None)

    def test_checked_dict_setdefault(self):
        x = chkdict[str, str]()
        x.setdefault("abc", "foo")
        self.assertEqual(x, {"abc": "foo"})

    def test_checked_dict___module__(self):
        class Lol:
            pass

        x = chkdict[int, Lol]()
        self.assertEqual(type(x).__module__, "__static__")

    def test_checked_dict_setdefault_bad_values(self):
        x = chkdict[str, int]()
        with self.assertRaises(TypeError):
            x.setdefault("abc", "abc")
        self.assertEqual(x, {})
        with self.assertRaises(TypeError):
            x.setdefault(42, 42)
        self.assertEqual(x, {})

    def test_checked_dict_fromkeys(self):
        x = chkdict[str, int].fromkeys("abc", 42)
        self.assertEqual(x, {"a": 42, "b": 42, "c": 42})

    def test_checked_dict_fromkeys_optional(self):
        x = chkdict[Optional[str], int].fromkeys(["a", "b", "c", None], 42)
        self.assertEqual(x, {"a": 42, "b": 42, "c": 42, None: 42})

        x = chkdict[str, Optional[int]].fromkeys("abc", None)
        self.assertEqual(x, {"a": None, "b": None, "c": None})

    def test_checked_dict_fromkeys_bad_types(self):
        with self.assertRaises(TypeError):
            chkdict[str, int].fromkeys([2], 42)

        with self.assertRaises(TypeError):
            chkdict[str, int].fromkeys("abc", object())

        with self.assertRaises(TypeError):
            chkdict[str, int].fromkeys("abc")

    def test_checked_dict_copy(self):
        x = chkdict[str, str](x="abc")
        self.assertEqual(type(x), chkdict[str, str])
        self.assertEqual(x, {"x": "abc"})

    def test_checked_dict_clear(self):
        x = chkdict[str, str](x="abc")
        x.clear()
        self.assertEqual(x, {})

    def test_checked_dict_update(self):
        x = chkdict[str, str](x="abc")
        x.update(y="foo")
        self.assertEqual(x, {"x": "abc", "y": "foo"})
        x.update({"z": "bar"})
        self.assertEqual(x, {"x": "abc", "y": "foo", "z": "bar"})

    def test_checked_dict_update_bad_type(self):
        x = chkdict[str, int]()
        with self.assertRaises(TypeError):
            x.update(x="abc")
        self.assertEqual(x, {})
        with self.assertRaises(TypeError):
            x.update({"x": "abc"})
        with self.assertRaises(TypeError):
            x.update({24: 42})
        self.assertEqual(x, {})

    def test_checked_dict_keys(self):
        x = chkdict[str, int](x=2)
        self.assertEqual(list(x.keys()), ["x"])
        x = chkdict[str, int](x=2, y=3)
        self.assertEqual(list(x.keys()), ["x", "y"])

    def test_checked_dict_values(self):
        x = chkdict[str, int](x=2, y=3)
        self.assertEqual(list(x.values()), [2, 3])

    def test_checked_dict_items(self):
        x = chkdict[str, int](x=2)
        self.assertEqual(
            list(x.items()),
            [
                ("x", 2),
            ],
        )
        x = chkdict[str, int](x=2, y=3)
        self.assertEqual(list(x.items()), [("x", 2), ("y", 3)])

    def test_checked_dict_pop(self):
        x = chkdict[str, int](x=2)
        y = x.pop("x")
        self.assertEqual(y, 2)
        with self.assertRaises(KeyError):
            x.pop("z")

    def test_checked_dict_popitem(self):
        x = chkdict[str, int](x=2)
        y = x.popitem()
        self.assertEqual(y, ("x", 2))
        with self.assertRaises(KeyError):
            x.popitem()

    def test_checked_dict_get(self):
        x = chkdict[str, int](x=2)
        self.assertEqual(x.get("x"), 2)
        self.assertEqual(x.get("y", 100), 100)

    def test_checked_dict_errors(self):
        x = chkdict[str, int](x=2)
        with self.assertRaises(TypeError):
            x.get(100)
        with self.assertRaises(TypeError):
            x.get("x", "abc")

    def test_checked_dict_sizeof(self):
        x = chkdict[str, int](x=2).__sizeof__()
        self.assertEqual(type(x), int)

    def test_checked_dict_getitem(self):
        x = chkdict[str, int](x=2)
        self.assertEqual(x.__getitem__("x"), 2)

    def test_checked_dict_free_list(self):
        t1 = chkdict[str, int]
        t2 = chkdict[str, str]
        x = t1()
        x_id1 = id(x)
        del x
        x = t2()
        x_id2 = id(x)
        self.assertEqual(x_id1, x_id2)

    def test_check_args(self):
        """
        Tests whether CHECK_ARGS can handle variables which are in a Cell,
        and are a positional arg at index 0.
        """

        codestr = """
            def use(i: object) -> object:
                return i

            def outer(x: int) -> object:

                def inner() -> None:
                    use(x)

                return use(x)
        """
        with self.in_module(codestr) as mod:
            outer = mod.outer
            self.assertEqual(outer(1), 1)

    def test_check_args_2(self):
        """
        Tests whether CHECK_ARGS can handle multiple variables which are in a Cell,
        and are positional args.
        """

        codestr = """
            def use(i: object) -> object:
                return i

            def outer(x: int, y: str) -> object:

                def inner() -> None:
                    use(x)
                    use(y)

                use(x)
                return use(y)
        """
        with self.in_module(codestr) as mod:
            outer = mod.outer
            self.assertEqual(outer(1, "yo"), "yo")
            # Force JIT-compiled code to go through argument checks after
            # keyword arg binding
            self.assertEqual(outer(1, y="yo"), "yo")

    def test_check_args_3(self):
        """
        Tests whether CHECK_ARGS can handle variables which are in a Cell,
        and are a positional arg at index > 0.
        """

        codestr = """
            def use(i: object) -> object:
                return i

            def outer(x: int, y: str) -> object:

                def inner() -> None:
                    use(y)

                use(x)
                return use(y)
        """
        with self.in_module(codestr) as mod:
            outer = mod.outer
            self.assertEqual(outer(1, "yo"), "yo")
            # Force JIT-compiled code to go through argument checks after
            # keyword arg binding
            self.assertEqual(outer(1, y="yo"), "yo")

    def test_check_args_4(self):
        """
        Tests whether CHECK_ARGS can handle variables which are in a Cell,
        and are a kwarg at index 0.
        """

        codestr = """
            def use(i: object) -> object:
                return i

            def outer(x: int = 0) -> object:

                def inner() -> None:
                    use(x)

                return use(x)
        """
        with self.in_module(codestr) as mod:
            outer = mod.outer
            self.assertEqual(outer(1), 1)

    def test_check_args_5(self):
        """
        Tests whether CHECK_ARGS can handle variables which are in a Cell,
        and are a kw-only arg.
        """
        codestr = """
            def use(i: object) -> object:
                return i

            def outer(x: int, *, y: str = "lol") -> object:

                def inner() -> None:
                    use(y)

                return use(y)

        """
        with self.in_module(codestr) as mod:
            outer = mod.outer
            self.assertEqual(outer(1, y="hi"), "hi")

    def test_check_args_6(self):
        """
        Tests whether CHECK_ARGS can handle variables which are in a Cell,
        and are a pos-only arg.
        """
        codestr = """
            def use(i: object) -> object:
                return i

            def outer(x: int, /, y: str) -> object:

                def inner() -> None:
                    use(y)

                return use(y)

        """
        with self.in_module(codestr) as mod:
            outer = mod.outer
            self.assertEqual(outer(1, "hi"), "hi")

    def test_check_args_7(self):
        """
        Tests whether CHECK_ARGS can handle multiple variables which are in a Cell,
        and are a mix of positional, pos-only and kw-only args.
        """

        codestr = """
            def use(i: object) -> object:
                return i

            def outer(x: int, /, y: int, *, z: str = "lol") -> object:

                def inner() -> None:
                    use(x)
                    use(y)
                    use(z)

                return use(x), use(y), use(z)
        """
        with self.in_module(codestr) as mod:
            outer = mod.outer
            self.assertEqual(outer(3, 2, z="hi"), (3, 2, "hi"))

    def test_str_split(self):
        codestr = """
            def get_str() -> str:
                return "something here"

            def test() -> str:
                a, b = get_str().split(None, 1)
                return b
        """
        with self.in_module(codestr) as mod:
            test = mod.test
            self.assertEqual(test(), "here")

    def test_for_iter_list(self):
        codestr = """
            from typing import List

            def f(n: int) -> List:
                acc = []
                l = [i for i in range(n)]
                for i in l:
                    acc.append(i + 1)
                return acc
        """
        with self.in_module(codestr) as mod:
            f = mod.f
            self.assertNotInBytecode(f, "FOR_ITER")
            self.assertEqual(f(4), [i + 1 for i in range(4)])

    def test_for_iter_tuple(self):
        codestr = """
            from typing import List

            def f(n: int) -> List:
                acc = []
                l = tuple([i for i in range(n)])
                for i in l:
                    acc.append(i + 1)
                return acc
        """
        with self.in_module(codestr) as mod:
            f = mod.f
            self.assertNotInBytecode(f, "FOR_ITER")
            self.assertEqual(f(4), [i + 1 for i in range(4)])

    def test_fast_for_iter_global(self):
        codestr = """
            for i in [1,2,3]:
                X = i
        """
        code = self.compile(codestr)
        self.assertInBytecode(code, "FAST_LEN")
        self.assertEqual(code.co_nlocals, 1)
        with self.in_module(codestr) as mod:
            self.assertEqual(mod.X, 3)

    def test_for_iter_sequence_orelse(self):
        codestr = """
            from typing import List

            def f(n: int) -> List:
                acc = []
                l = [i for i in range(n)]
                for i in l:
                    acc.append(i + 1)
                else:
                    acc.append(999)
                return acc
        """
        with self.in_module(codestr) as mod:
            f = mod.f
            self.assertNotInBytecode(f, "FOR_ITER")
            self.assertEqual(f(4), [i + 1 for i in range(4)] + [999])

    def test_for_iter_sequence_break(self):
        codestr = """
            from typing import List

            def f(n: int) -> List:
                acc = []
                l = [i for i in range(n)]
                for i in l:
                    if i == 3:
                        break
                    acc.append(i + 1)
                return acc
        """
        with self.in_module(codestr) as mod:
            f = mod.f
            self.assertNotInBytecode(f, "FOR_ITER")
            self.assertEqual(f(5), [1, 2, 3])

    def test_for_iter_sequence_orelse_break(self):
        codestr = """
            from typing import List

            def f(n: int) -> List:
                acc = []
                l = [i for i in range(n)]
                for i in l:
                    if i == 2:
                        break
                    acc.append(i + 1)
                else:
                    acc.append(999)
                return acc
        """
        with self.in_module(codestr) as mod:
            f = mod.f
            self.assertNotInBytecode(f, "FOR_ITER")
            self.assertEqual(f(4), [1, 2])

    def test_for_iter_sequence_return(self):
        codestr = """
            from typing import List

            def f(n: int) -> List:
                acc = []
                l = [i for i in range(n)]
                for i in l:
                    if i == 3:
                        return acc
                    acc.append(i + 1)
                return acc
        """
        with self.in_module(codestr) as mod:
            f = mod.f
            self.assertNotInBytecode(f, "FOR_ITER")
            self.assertEqual(f(6), [1, 2, 3])

    def test_nested_for_iter_sequence(self):
        codestr = """
            from typing import List

            def f(n: int) -> List:
                acc = []
                l = [i for i in range(n)]
                for i in l:
                    for j in l:
                        acc.append(i + j)
                return acc
        """
        with self.in_module(codestr) as mod:
            f = mod.f
            self.assertNotInBytecode(f, "FOR_ITER")
            self.assertEqual(f(3), [0, 1, 2, 1, 2, 3, 2, 3, 4])

    def test_nested_for_iter_sequence_break(self):
        codestr = """
            from typing import List

            def f(n: int) -> List:
                acc = []
                l = [i for i in range(n)]
                for i in l:
                    for j in l:
                        if j == 2:
                            break
                        acc.append(i + j)
                return acc
        """
        with self.in_module(codestr) as mod:
            f = mod.f
            self.assertNotInBytecode(f, "FOR_ITER")
            self.assertEqual(f(3), [0, 1, 1, 2, 2, 3])

    def test_nested_for_iter_sequence_return(self):
        codestr = """
            from typing import List

            def f(n: int) -> List:
                acc = []
                l = [i for i in range(n)]
                for i in l:
                    for j in l:
                        if j == 1:
                            return acc
                        acc.append(i + j)
                return acc
        """
        with self.in_module(codestr) as mod:
            f = mod.f
            self.assertNotInBytecode(f, "FOR_ITER")
            self.assertEqual(f(3), [0])

    def test_for_iter_unchecked_get(self):
        """We don't need to check sequence bounds when we've just compared with the list size."""
        codestr = """
            def f():
                l = [1, 2, 3]
                acc = []
                for x in l:
                    acc.append(x)
                return acc
        """
        with self.in_module(codestr) as mod:
            f = mod.f
            self.assertInBytecode(f, "SEQUENCE_GET", SEQ_LIST | SEQ_SUBSCR_UNCHECKED)
            self.assertEqual(f(), [1, 2, 3])

    def test_for_iter_list_modified(self):
        codestr = """
            def f():
                l = [1, 2, 3, 4, 5]
                acc = []
                for x in l:
                    acc.append(x)
                    l[2:] = []
                return acc
        """
        with self.in_module(codestr) as mod:
            f = mod.f
            self.assertNotInBytecode(f, "FOR_ITER")
            self.assertEqual(f(), [1, 2])

    def test_sorted(self):
        """sorted() builtin returns an Exact[List]."""
        codestr = """
            from typing import Iterable

            def f(l: Iterable[int]):
                for x in sorted(l):
                    pass
        """
        with self.in_module(codestr) as mod:
            f = mod.f
            self.assertNotInBytecode(f, "FOR_ITER")
            self.assertInBytecode(f, "REFINE_TYPE", ("builtins", "list", "!"))

    def test_min(self):
        codestr = """
            def f(a: int, b: int) -> int:
                return min(a, b)
        """
        with self.in_module(codestr) as mod:
            f = mod.f
            self.assertInBytecode(f, "COMPARE_OP", "<=")
            self.assertInBytecode(f, "POP_JUMP_IF_FALSE")
            self.assertEqual(f(1, 3), 1)
            self.assertEqual(f(3, 1), 1)

    def test_min_stability(self):
        codestr = """
            def f(a: int, b: int) -> int:
                return min(a, b)
        """
        with self.in_module(codestr) as mod:
            f = mod.f
            self.assertInBytecode(f, "COMPARE_OP", "<=")
            self.assertInBytecode(f, "POP_JUMP_IF_FALSE")
            # p & q should be different objects, but with same value
            p = int("11334455667")
            q = int("11334455667")
            self.assertNotEqual(id(p), id(q))
            # Since p and q are equal, the returned value should be the first arg
            self.assertEqual(id(f(p, q)), id(p))
            self.assertEqual(id(f(q, p)), id(q))

    def test_max(self):
        codestr = """
            def f(a: int, b: int) -> int:
                return max(a, b)
        """
        with self.in_module(codestr) as mod:
            f = mod.f
            self.assertInBytecode(f, "COMPARE_OP", ">=")
            self.assertInBytecode(f, "POP_JUMP_IF_FALSE")
            self.assertEqual(f(1, 3), 3)
            self.assertEqual(f(3, 1), 3)

    def test_max_stability(self):
        codestr = """
            def f(a: int, b: int) -> int:
                return max(a, b)
        """
        with self.in_module(codestr) as mod:
            f = mod.f
            self.assertInBytecode(f, "COMPARE_OP", ">=")
            self.assertInBytecode(f, "POP_JUMP_IF_FALSE")
            # p & q should be different objects, but with same value
            p = int("11334455667")
            q = int("11334455667")
            self.assertNotEqual(id(p), id(q))
            # Since p and q are equal, the returned value should be the first arg
            self.assertEqual(id(f(p, q)), id(p))
            self.assertEqual(id(f(q, p)), id(q))

    def test_extremum_primitive(self):
        codestr = """
            from __static__ import int8

            def f() -> None:
                a: int8 = 4
                b: int8 = 5
                min(a, b)
        """
        with self.assertRaisesRegex(
            TypedSyntaxError, "Call argument cannot be a primitive"
        ):
            self.compile(codestr, modname="foo.py")

    def test_extremum_non_specialization_kwarg(self):
        codestr = """
            def f() -> None:
                a = "4"
                b = "5"
                min(a, b, key=int)
        """
        with self.in_module(codestr) as mod:
            f = mod.f
            self.assertNotInBytecode(f, "COMPARE_OP")
            self.assertNotInBytecode(f, "POP_JUMP_IF_FALSE")

    def test_extremum_non_specialization_stararg(self):
        codestr = """
            def f() -> None:
                a = [3, 4]
                min(*a)
        """
        with self.in_module(codestr) as mod:
            f = mod.f
            self.assertNotInBytecode(f, "COMPARE_OP")
            self.assertNotInBytecode(f, "POP_JUMP_IF_FALSE")

    def test_extremum_non_specialization_dstararg(self):
        codestr = """
            def f() -> None:
                k = {
                    "default": 5
                }
                min(3, 4, **k)
        """
        with self.in_module(codestr) as mod:
            f = mod.f
            self.assertNotInBytecode(f, "COMPARE_OP")
            self.assertNotInBytecode(f, "POP_JUMP_IF_FALSE")

    def test_try_return_finally(self):
        codestr = """
        from typing import List

        def f1(x: List):
            try:
                return
            finally:
                x.append("hi")
        """
        with self.in_module(codestr) as mod:
            f1 = mod.f1
            l = []
            f1(l)
            self.assertEqual(l, ["hi"])

    def test_chkdict_del(self):
        codestr = """
        def f():
            x = {}
            x[1] = "a"
            x[2] = "b"
            del x[1]
            return x
        """
        with self.in_module(codestr) as mod:
            f = mod.f
            ret = f()
            self.assertNotIn(1, ret)
            self.assertIn(2, ret)

    def test_none_not(self):
        codestr = """
        def t() -> bool:
            x = None
            if not x:
                return True
            else:
                return False
        """
        with self.in_module(codestr) as mod:
            t = mod.t
            self.assertInBytecode(t, "POP_JUMP_IF_TRUE")
            self.assertTrue(t())

    def test_qualname(self):
        codestr = """
        def f():
            pass


        class C:
            def x(self):
                pass

            @staticmethod
            def sm():
                pass

            @classmethod
            def cm():
                pass
        """
        with self.in_module(codestr) as mod:
            f = mod.f
            C = mod.C
            self.assertEqual(cinder._get_qualname(f.__code__), "f")

            self.assertEqual(cinder._get_qualname(C.x.__code__), "C.x")
            self.assertEqual(cinder._get_qualname(C.sm.__code__), "C.sm")
            self.assertEqual(cinder._get_qualname(C.cm.__code__), "C.cm")

    def test_refine_optional_name(self):
        codestr = """
        from typing import Optional

        def f(s: Optional[str]) -> bytes:
            return s.encode("utf-8") if s else b""
        """
        with self.in_module(codestr) as mod:
            f = mod.f
            self.assertEqual(f("A"), b"A")
            self.assertEqual(f(None), b"")

    def test_refine_or_expression(self):
        codestr = """
        from typing import Optional

        def f(s: Optional[str]) -> str:
            return s or "hi"
        """
        with self.in_module(codestr) as mod:
            f = mod.f
            self.assertEqual(f("A"), "A")
            self.assertEqual(f(None), "hi")

    def test_refine_or_expression_with_multiple_optionals(self):
        codestr = """
        from typing import Optional

        def f(s1: Optional[str], s2: Optional[str]) -> str:
            return s1 or s2 or "hi"
        """
        with self.in_module(codestr) as mod:
            f = mod.f
            self.assertEqual(f("A", None), "A")
            self.assertEqual(f(None, "B"), "B")
            self.assertEqual(f("A", "B"), "A")
            self.assertEqual(f(None, None), "hi")

    def test_or_expression_with_multiple_optionals_type_error(self):
        codestr = """
        from typing import Optional

        def f(s1: Optional[str], s2: Optional[str]) -> str:
            return s1 or s2
        """
        self.type_error(codestr, bad_ret_type("Optional[str]", "str"))

    def test_donotcompile_fn(self):
        codestr = """
        from __static__ import _donotcompile

        def a() -> int:
            return 1

        @_donotcompile
        def fn() -> None:
            a() + 2

        def fn2() -> None:
            a() + 2
        """
        with self.in_module(codestr) as mod:
            fn = mod.fn
            self.assertInBytecode(fn, "CALL_FUNCTION")
            self.assertNotInBytecode(fn, "INVOKE_FUNCTION")
            self.assertFalse(fn.__code__.co_flags & CO_STATICALLY_COMPILED)
            self.assertEqual(fn(), None)

            fn2 = mod.fn2
            self.assertNotInBytecode(fn2, "CALL_FUNCTION")
            self.assertInBytecode(fn2, "INVOKE_FUNCTION")
            self.assertTrue(fn2.__code__.co_flags & CO_STATICALLY_COMPILED)
            self.assertEqual(fn2(), None)

    def test_donotcompile_method(self):
        codestr = """
        from __static__ import _donotcompile

        def a() -> int:
            return 1

        class C:
            @_donotcompile
            def fn() -> None:
                a() + 2

            def fn2() -> None:
                a() + 2

        c = C()
        """
        with self.in_module(codestr) as mod:
            C = mod.C

            fn2 = C.fn2
            self.assertNotInBytecode(fn2, "CALL_FUNCTION")
            self.assertInBytecode(fn2, "INVOKE_FUNCTION")
            self.assertTrue(fn2.__code__.co_flags & CO_STATICALLY_COMPILED)
            self.assertEqual(fn2(), None)

    def test_donotcompile_class(self):
        codestr = """
        from __static__ import _donotcompile

        def a() -> int:
            return 1

        @_donotcompile
        class C:
            def fn() -> None:
                a() + 2

        @_donotcompile
        class D:
            a()

        c = C()
        """
        with self.in_module(codestr) as mod:
            C = mod.C
            fn = C.fn
            self.assertInBytecode(fn, "CALL_FUNCTION")
            self.assertNotInBytecode(fn, "INVOKE_FUNCTION")
            self.assertFalse(fn.__code__.co_flags & CO_STATICALLY_COMPILED)
            self.assertEqual(fn(), None)

    def test_donotcompile_lambda(self):
        codestr = """
        from __static__ import _donotcompile

        def a() -> int:
            return 1

        class C:
            @_donotcompile
            def fn() -> None:
                z = lambda: a() + 2
                z()

            def fn2() -> None:
                z = lambda: a() + 2
                z()

        c = C()
        """
        with self.in_module(codestr) as mod:
            C = mod.C
            fn = C.fn
            lambda_code = self.find_code(fn.__code__)
            self.assertNotInBytecode(lambda_code, "INVOKE_FUNCTION")
            self.assertFalse(lambda_code.co_flags & CO_STATICALLY_COMPILED)
            self.assertEqual(fn(), None)

            fn2 = C.fn2
            lambda_code2 = self.find_code(fn2.__code__)
            self.assertInBytecode(lambda_code2, "INVOKE_FUNCTION")
            self.assertTrue(lambda_code2.co_flags & CO_STATICALLY_COMPILED)
            self.assertEqual(fn2(), None)

    def test_class_static_tpflag(self):
        codestr = """
        class A:
            pass
        """
        with self.in_module(codestr) as mod:
            A = mod.A
            self.assertTrue(is_type_static(A))

            class B:
                pass

            self.assertFalse(is_type_static(B))

    def test_assert_narrowing_type_error(self):
        codestr = """
        def foo(x: int | str) -> str:
            assert isinstance(x, int)
            return x
        """
        self.type_error(codestr, bad_ret_type("int", "str"))

    def test_assert_narrowing_debug(self):
        codestr = """
        def foo(x: int | str) -> int:
            assert isinstance(x, int)
            return x + 1
        """
        with self.in_module(codestr) as mod:
            foo = mod.foo
            self.assertEqual(foo(1), 2)
            with self.assertRaises(AssertionError):
                foo("a")

    def test_assert_narrowing_optimized(self):
        # We ensure that the code without the assert would work in the runtime.
        codestr = """
        def foo(x: int | str) -> object:
            assert isinstance(x, int)
            return x
        """

        with self.in_module(codestr, optimize=1) as mod:
            foo = mod.foo
            self.assertEqual(foo(1), 1)
            with self.assertRaises(TypeError):
                foo("a")

    def test_assert_narrowing_not_isinstance_optimized(self):
        # We ensure that the code without the assert would work in the runtime.
        codestr = """
        def foo(x: int | str) -> str:
            assert not isinstance(x, int)
            return x
        """

        with self.in_module(codestr, optimize=1) as mod:
            foo = mod.foo
            self.assertEqual(foo("abc"), "abc")

    def test_prod_assert(self):
        codestr = """
        from typing import Optional
        from __static__ import prod_assert

        def foo(x: Optional[int]) -> int:
            prod_assert(x)
            return x
        """
        with self.in_module(codestr) as mod:
            foo = mod.foo
            self.assertEqual(foo(1), 1)

    def test_prod_assert_static_error(self):
        codestr = """
        from typing import Optional
        from __static__ import prod_assert

        def foo(x: Optional[int]) -> str:
            prod_assert(x)
            return x
        """
        self.type_error(codestr, "return type must be str, not int")

    def test_prod_assert_raises(self):
        codestr = """
        from typing import Optional
        from __static__ import prod_assert

        def foo(x: Optional[int]) -> int:
            prod_assert(x)
            return x
        """
        with self.in_module(codestr) as mod:
            foo = mod.foo
            with self.assertRaises(AssertionError):
                foo(None)

    def test_prod_assert_raises_with_message(self):
        codestr = """
        from typing import Optional
        from __static__ import prod_assert

        def foo(x: Optional[int]) -> int:
            prod_assert(x, "x must be int")
            return x
        """
        with self.in_module(codestr) as mod:
            foo = mod.foo
            with self.assertRaisesRegex(AssertionError, "x must be int"):
                foo(None)

    def test_prod_assert_message_type(self):
        codestr = """
        from typing import Optional
        from __static__ import prod_assert

        def foo(x: Optional[int]) -> int:
            prod_assert(x, 3)
            return x
        """
        self.type_error(
            codestr, r"type mismatch: Literal\[3\] cannot be assigned to str"
        )

    def test_prod_assert_argcount_type_error(self):
        codestr = """
        from typing import Optional
        from __static__ import prod_assert

        def foo(x: Optional[int]) -> int:
            prod_assert(x, 3, 2)
            return x
        """
        self.type_error(
            codestr, r"prod_assert\(\) must be called with one or two arguments"
        )

    def test_prod_assert_keywords_type_error(self):
        codestr = """
        from typing import Optional
        from __static__ import prod_assert

        def foo(x: Optional[int]) -> int:
            prod_assert(x, message="x must be int")
            return x
        """
        self.type_error(codestr, r"prod_assert\(\) does not accept keyword arguments")

    def test_list_comprehension_with_if(self):
        codestr = """
        from typing import List
        def foo() -> List[int]:
             a = [1, 2, 3, 4]
             return [x for x in a if x > 2]

        """
        with self.in_module(codestr) as mod:
            f = mod.foo
            self.assertEqual(f(), [3, 4])

    def test_nested_list_comprehensions_with_if(self):
        codestr = """
        from typing import List
        def foo() -> List[int]:
             a = [1, 2, 3, 4]
             b = [1, 2]
             return [x * y for x in a for y in b if x > 2]

        """
        with self.in_module(codestr) as mod:
            f = mod.foo
            self.assertEqual(f(), [3, 6, 4, 8])

    def test_nested_fn_type_error(self):
        codestr = """
        def f(i: int, j: str, l: int, m: int, n: int, o: int) -> bool:
            def g(k: int) -> bool:
                return k > 0 if j == "gt" else k <= 0
            return g(i)
        """
        with self.in_module(codestr) as mod:
            f = mod.f
            with self.assertRaisesRegex(
                TypeError, r"f expected 'int' for argument n, got 'str'"
            ):
                f(1, "a", 2, 3, "4", 5)

    def test_nested_fn_type_error_2(self):
        codestr = """
        def f(i: int, j: str, k: int) -> bool:
            def g(k: int) -> bool:
                return k > 0 if j == "gt" else k <= 0
            return g(i)
        """
        with self.in_module(codestr) as mod:
            f = mod.f
            with self.assertRaisesRegex(
                TypeError, r"f expected 'str' for argument j, got 'int'"
            ):
                f(1, 2, 3)

    def test_nested_fn_type_error_kwarg(self):
        codestr = """
        def f(i: int, j: str = "yo") -> bool:
            def g(k: int) -> bool:
                return k > 0 if j == "gt" else k <= 0
            return g(i)
        """
        with self.in_module(codestr) as mod:
            f = mod.f
            with self.assertRaisesRegex(
                TypeError, r"f expected 'str' for argument j, got 'int'"
            ):
                f(1, j=2)
