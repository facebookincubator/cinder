import ast
import asyncio
import dis
import gc
import inspect
import itertools
import re
import string
import sys
import unittest
import warnings
from array import array
from collections import UserDict
from compiler import consts, walk
from compiler.consts38 import CO_NO_FRAME, CO_TINY_FRAME, CO_STATICALLY_COMPILED
from compiler.optimizer import AstOptimizer
from compiler.pycodegen import PythonCodeGenerator, make_compiler
from compiler.static import (
    prim_name_to_type,
    BOOL_TYPE,
    BYTES_TYPE,
    COMPLEX_EXACT_TYPE,
    Class,
    DICT_TYPE,
    DYNAMIC,
    FLOAT_TYPE,
    INT_TYPE,
    LIST_EXACT_TYPE,
    LIST_TYPE,
    NONE_TYPE,
    OBJECT_TYPE,
    PRIM_OP_ADD_INT,
    PRIM_OP_DIV_INT,
    PRIM_OP_GT_INT,
    PRIM_OP_LT_INT,
    STR_EXACT_TYPE,
    STR_TYPE,
    DeclarationVisitor,
    Function,
    StaticCodeGenerator,
    SymbolTable,
    TypeBinder,
    TypedSyntaxError,
    Value,
    TUPLE_EXACT_TYPE,
    TUPLE_TYPE,
    INT_EXACT_TYPE,
    FLOAT_EXACT_TYPE,
    SET_EXACT_TYPE,
    ELLIPSIS_TYPE,
    FAST_LEN_ARRAY,
    FAST_LEN_LIST,
    FAST_LEN_TUPLE,
    FAST_LEN_INEXACT,
    FAST_LEN_DICT,
    FAST_LEN_SET,
    SEQ_ARRAY_INT8,
    SEQ_ARRAY_INT16,
    SEQ_ARRAY_INT32,
    SEQ_ARRAY_INT64,
    SEQ_ARRAY_UINT8,
    SEQ_ARRAY_UINT16,
    SEQ_ARRAY_UINT32,
    SEQ_ARRAY_UINT64,
    SEQ_LIST,
    SEQ_LIST_INEXACT,
    SEQ_TUPLE,
    SEQ_REPEAT_INEXACT_SEQ,
    SEQ_REPEAT_INEXACT_NUM,
    SEQ_REPEAT_PRIMITIVE_NUM,
    SEQ_REPEAT_REVERSED,
    SEQ_SUBSCR_UNCHECKED,
    TYPED_BOOL,
    TYPED_INT8,
    TYPED_INT16,
    TYPED_INT32,
    TYPED_INT64,
    TYPED_UINT8,
    TYPED_UINT16,
    TYPED_UINT32,
    TYPED_UINT64,
    FAST_LEN_STR,
    DICT_EXACT_TYPE,
    TYPED_DOUBLE,
)
from compiler.symbols import SymbolVisitor
from contextlib import contextmanager
from copy import deepcopy
from io import StringIO
from os import path
from test.support import maybe_get_event_loop_policy
from textwrap import dedent
from types import CodeType, MemberDescriptorType, ModuleType
from typing import Generic, Optional, Tuple, TypeVar
from unittest import TestCase, skip, skipIf
from unittest.mock import Mock, patch

import cinder
import xxclassloader
from __static__ import (
    Array,
    Vector,
    chkdict,
    int32,
    int64,
    int8,
    make_generic_type,
    StaticGeneric,
)
from cinder import StrictModule

from .common import CompilerTest

try:
    import cinderjit
except ImportError:
    cinderjit = None


RICHARDS_PATH = path.join(
    path.dirname(__file__),
    "..",
    "..",
    "..",
    "Tools",
    "benchmarks",
    "richards_static.py",
)


PRIM_NAME_TO_TYPE = {
    "cbool": TYPED_BOOL,
    "int8": TYPED_INT8,
    "int16": TYPED_INT16,
    "int32": TYPED_INT32,
    "int64": TYPED_INT64,
    "uint8": TYPED_UINT8,
    "uint16": TYPED_UINT16,
    "uint32": TYPED_UINT32,
    "uint64": TYPED_UINT64,
}


def type_mismatch(from_type: str, to_type: str) -> str:
    return re.escape(f"type mismatch: {from_type} cannot be assigned to {to_type}")


def optional(type: str) -> str:
    return f"Optional[{type}]"


def init_xxclassloader():
    codestr = """
        from typing import Generic, TypeVar, _tp_cache
        from __static__.compiler_flags import nonchecked_dicts
        # Setup a test for typing
        T = TypeVar('T')
        U = TypeVar('U')


        class XXGeneric(Generic[T, U]):
            d = {}

            def foo(self, t: T, u: U) -> str:
                return str(t) + str(u)

            @classmethod
            def __class_getitem__(cls, elem_type):
                if elem_type in XXGeneric.d:
                    return XXGeneric.d[elem_type]

                XXGeneric.d[elem_type] = type(
                    f"XXGeneric[{elem_type[0].__name__}, {elem_type[1].__name__}]",
                    (object, ),
                    {
                        "foo": XXGeneric.foo,
                        "__slots__":(),
                    }
                )
                return XXGeneric.d[elem_type]
    """

    code = make_compiler(
        inspect.cleandoc(codestr),
        "",
        "exec",
        generator=StaticCodeGenerator,
        modname="xxclassloader",
    ).getCode()
    d = {}
    exec(code, d, d)

    xxclassloader.XXGeneric = d["XXGeneric"]


class StaticTestBase(CompilerTest):
    def compile(
        self,
        code,
        generator=StaticCodeGenerator,
        modname="<module>",
        optimize=0,
        peephole_enabled=True,
        ast_optimizer_enabled=True,
    ):
        if (
            not peephole_enabled
            or not ast_optimizer_enabled
            or generator is not StaticCodeGenerator
        ):
            return super().compile(
                code,
                generator,
                modname,
                optimize,
                peephole_enabled,
                ast_optimizer_enabled,
            )

        symtable = SymbolTable()
        code = inspect.cleandoc("\n" + code)
        tree = ast.parse(code)
        return symtable.compile(modname, f"{modname}.py", tree, optimize)

    def type_error(self, code, pattern):
        with self.assertRaisesRegex(TypedSyntaxError, pattern):
            self.compile(code)

    @contextmanager
    def in_module(self, code, name=None, code_gen=StaticCodeGenerator, optimize=0):
        if name is None:
            # get our callers name to avoid duplicating names
            name = sys._getframe().f_back.f_back.f_code.co_name

        try:
            compiled = self.compile(code, code_gen, name, optimize)
            m = type(sys)(name)
            d = m.__dict__
            sys.modules[name] = m
            exec(compiled, d)

            yield d
        finally:
            # don't throw a new exception if we failed to compile
            if name in sys.modules:
                del sys.modules[name]
                d.clear()
                gc.collect()

    @contextmanager
    def in_strict_module(
        self,
        code,
        name=None,
        code_gen=StaticCodeGenerator,
        optimize=0,
        enable_patching=False,
    ):
        if name is None:
            # get our callers name to avoid duplicating names
            name = sys._getframe().f_back.f_back.f_code.co_name

        try:
            compiled = self.compile(code, code_gen, name, optimize)
            d = {"__name__": name}
            m = StrictModule(d, enable_patching)
            sys.modules[name] = m
            exec(compiled, d)

            yield m
        finally:
            # don't throw a new exception if we failed to compile
            if name in sys.modules:
                del sys.modules[name]
                d.clear()
                gc.collect()

    @property
    def base_size(self):
        class C:
            __slots__ = ()

        return sys.getsizeof(C())

    @property
    def ptr_size(self):
        return 8 if sys.maxsize > 2 ** 32 else 4

    def assert_jitted(self, func):
        if cinderjit is None:
            return

        self.assertTrue(cinderjit.is_jit_compiled(func), func.__name__)

    def assert_not_jitted(self, func):
        if cinderjit is None:
            return

        self.assertFalse(cinderjit.is_jit_compiled(func))

    def assert_not_jitted(self, func):
        if cinderjit is None:
            return

        self.assertFalse(cinderjit.is_jit_compiled(func))

    def setUp(self):
        # ensure clean classloader/vtable slate for all tests
        cinder.clear_classloader_caches()
        # ensure our async tests don't change the event loop policy
        policy = maybe_get_event_loop_policy()
        self.addCleanup(lambda: asyncio.set_event_loop_policy(policy))

    def subTest(self, **kwargs):
        cinder.clear_classloader_caches()
        return super().subTest(**kwargs)

    def make_async_func_hot(self, func):
        async def make_hot():
            for i in range(50):
                await func()

        asyncio.run(make_hot())


class StaticCompilationTests(StaticTestBase):
    @classmethod
    def setUpClass(cls):
        init_xxclassloader()

    @classmethod
    def tearDownClass(cls):
        del xxclassloader.XXGeneric

    def test_static_import_unknown(self) -> None:
        codestr = """
            from __static__ import does_not_exist
        """
        with self.assertRaises(TypedSyntaxError):
            self.compile(codestr, StaticCodeGenerator, modname="foo")

    def test_static_import_star(self) -> None:
        codestr = """
            from __static__ import *
        """
        with self.assertRaises(TypedSyntaxError):
            self.compile(codestr, StaticCodeGenerator, modname="foo")

    def test_reveal_type(self) -> None:
        codestr = """
            def f(x: int):
                reveal_type(x or None)
        """
        with self.assertRaisesRegex(
            TypedSyntaxError,
            r"reveal_type\(x or None\): 'Optional\[int\]'",
        ):
            self.compile(codestr)

    def test_reveal_type_local(self) -> None:
        codestr = """
            def f(x: int | None):
                if x is not None:
                    reveal_type(x)
        """
        with self.assertRaisesRegex(
            TypedSyntaxError,
            r"reveal_type\(x\): 'int', 'x' has declared type 'Optional\[int\]' and local type 'int'",
        ):
            self.compile(codestr)

    def test_redefine_local_type(self) -> None:
        codestr = """
            class C: pass
            class D: pass

            def f():
                x: C = C()
                x: D = D()
        """
        with self.assertRaises(TypedSyntaxError):
            self.compile(codestr, StaticCodeGenerator, modname="foo")

    def test_mixed_chain_assign(self) -> None:
        codestr = """
            class C: pass
            class D: pass

            def f():
                x: C = C()
                y: D = D()
                x = y = D()
        """
        with self.assertRaisesRegex(TypedSyntaxError, type_mismatch("foo.D", "foo.C")):
            self.compile(codestr, StaticCodeGenerator, modname="foo")

    def test_bool_cast(self) -> None:
        codestr = """
            from __static__ import cast
            class D: pass

            def f(x) -> bool:
                y: bool = cast(bool, x)
                return y
        """
        self.compile(codestr, StaticCodeGenerator, modname="foo")

    def test_typing_overload(self) -> None:
        """Typing overloads are ignored, don't cause member name conflict."""
        codestr = """
            from typing import Optional, overload

            class C:
                @overload
                def foo(self, x: int) -> int:
                    ...

                def foo(self, x: Optional[int]) -> Optional[int]:
                    return x

            def f(x: int) -> Optional[int]:
                return C().foo(x)
        """
        self.assertReturns(codestr, "Optional[int]")

    def test_mixed_binop(self):
        with self.assertRaisesRegex(
            TypedSyntaxError, "cannot add int64 and Exact\\[int\\]"
        ):
            self.bind_module(
                """
                from __static__ import ssize_t

                def f():
                    x: ssize_t = 1
                    y = 1
                    x + y
            """
            )

        with self.assertRaisesRegex(
            TypedSyntaxError, "cannot add Exact\\[int\\] and int64"
        ):
            self.bind_module(
                """
                from __static__ import ssize_t

                def f():
                    x: ssize_t = 1
                    y = 1
                    y + x
            """
            )

    def test_mixed_binop_okay(self):
        codestr = """
            from __static__ import ssize_t, box

            def f():
                x: ssize_t = 1
                y = x + 1
                return box(y)
        """
        with self.in_module(codestr) as mod:
            f = mod["f"]
            self.assertEqual(f(), 2)

    def test_mixed_binop_okay_1(self):
        codestr = """
            from __static__ import ssize_t, box

            def f():
                x: ssize_t = 1
                y = 1 + x
                return box(y)
        """
        with self.in_module(codestr) as mod:
            f = mod["f"]
            self.assertEqual(f(), 2)

    def test_inferred_primitive_type(self):
        codestr = """
        from __static__ import ssize_t, box

        def f():
            x: ssize_t = 1
            y = x
            return box(y)
        """
        with self.in_module(codestr) as mod:
            f = mod["f"]
            self.assertEqual(f(), 1)

    @skipIf(cinderjit is None, "not jitting")
    def test_tiny_frame(self):
        codestr = """
        from __static__.compiler_flags import tinyframe
        import sys

        def f():
            return 123
        """
        with self.in_module(codestr) as mod:
            f = mod["f"]
            self.assertTrue(f.__code__.co_flags & CO_TINY_FRAME)
            self.assertEqual(f(), 123)
            self.assert_jitted(f)

    @skipIf(cinderjit is None, "not jitting")
    def test_no_frame(self):
        codestr = """
        from __static__.compiler_flags import noframe

        def f():
            return 456
        """
        with self.in_module(codestr) as mod:
            f = mod["f"]
            self.assertTrue(f.__code__.co_flags & CO_NO_FRAME)
            self.assertEqual(f(), 456)
            self.assert_jitted(f)

    @skipIf(cinderjit is None, "not jitting")
    def test_no_frame_generator(self):
        codestr = """
        from __static__.compiler_flags import noframe

        def g():
            for i in range(10):
                yield i
        def f():
            return list(g())
        """
        with self.in_module(codestr) as mod:
            f = mod["f"]
            self.assertTrue(f.__code__.co_flags & CO_NO_FRAME)
            self.assertEqual(f(), list(range(10)))
            self.assert_jitted(f)

    @skipIf(cinderjit is None, "not jitting")
    def test_tiny_frame_generator(self):
        codestr = """
        from __static__.compiler_flags import tinyframe

        def g():
            for i in range(10):
                yield i
        def f():
            return list(g())
        """
        with self.in_module(codestr) as mod:
            f = mod["f"]
            self.assertTrue(f.__code__.co_flags & CO_TINY_FRAME)
            self.assertEqual(f(), list(range(10)))
            self.assert_jitted(f)

    def test_subclass_binop(self):
        codestr = """
            class C: pass
            class D(C): pass

            def f(x: C, y: D):
                return x + y
        """
        code = self.compile(codestr, StaticCodeGenerator, modname="foo")
        f = self.find_code(code, "f")
        self.assertInBytecode(f, "BINARY_ADD")

    def test_exact_invoke_function(self):
        codestr = """
            def f() -> str:
                return ", ".join(['1','2','3'])
        """
        f = self.find_code(self.compile(codestr))
        with self.in_module(codestr) as mod:
            f = mod["f"]
            self.assertInBytecode(
                f, "INVOKE_FUNCTION", (("builtins", "str", "join"), 2)
            )
            f()

    def test_multiply_list_exact_by_int(self):
        codestr = """
            def f() -> int:
                l = [1, 2, 3] * 2
                return len(l)
        """
        f = self.find_code(self.compile(codestr))
        self.assertInBytecode(f, "FAST_LEN", FAST_LEN_LIST)
        with self.in_module(codestr) as mod:
            self.assertEqual(mod["f"](), 6)

    def test_multiply_list_exact_by_int_reverse(self):
        codestr = """
            def f() -> int:
                l = 2 * [1, 2, 3]
                return len(l)
        """
        f = self.find_code(self.compile(codestr))
        self.assertInBytecode(f, "FAST_LEN", FAST_LEN_LIST)
        with self.in_module(codestr) as mod:
            self.assertEqual(mod["f"](), 6)

    def test_int_bad_assign(self):
        with self.assertRaisesRegex(
            TypedSyntaxError, "str cannot be used in a context where an int is expected"
        ):
            code = self.compile(
                """
            from __static__ import ssize_t
            def f():
                x: ssize_t = 'abc'
            """,
                StaticCodeGenerator,
            )

    def test_sign_extend(self):
        codestr = f"""
            from __static__ import int16, int64, box
            def testfunc():
                x: int16 = -40
                y: int64 = x
                return box(y)
            """
        code = self.compile(codestr, StaticCodeGenerator)
        f = self.run_code(codestr, StaticCodeGenerator)["testfunc"]
        self.assertEqual(f(), -40)

    def test_field_size(self):
        for type in [
            "int8",
            "int16",
            "int32",
            "int64",
            "uint8",
            "uint16",
            "uint32",
            "uint64",
        ]:
            codestr = f"""
                from __static__ import {type}, box
                class C{type}:
                    def __init__(self):
                        self.a: {type} = 1
                        self.b: {type} = 1

                def testfunc(c: C{type}):
                    c.a = 2
                    c.b = 3
                    return box(c.a + c.b)
                """
            with self.subTest(type=type):
                with self.in_module(codestr) as mod:
                    C = mod["C" + type]
                    f = mod["testfunc"]
                    self.assertEqual(f(C()), 5)

    def test_field_sign_ext(self):
        """tests that we do the correct sign extension when loading from a field"""
        for type, val in [
            ("int32", 65537),
            ("int16", 256),
            ("int8", 0x7F),
            ("uint32", 65537),
        ]:
            codestr = f"""
                from __static__ import {type}, box
                class C{type}:
                    def __init__(self):
                        self.value: {type} = {val}

                def testfunc(c: C{type}):
                    return box(c.value)
                """
            with self.subTest(type=type, val=val):
                with self.in_module(codestr) as mod:
                    C = mod["C" + type]
                    f = mod["testfunc"]
                    self.assertEqual(f(C()), val)

    def test_field_unsign_ext(self):
        """tests that we do the correct sign extension when loading from a field"""
        for type, val, test in [("uint32", 65537, -1)]:
            codestr = f"""
                from __static__ import {type}, int64, box
                class C{type}:
                    def __init__(self):
                        self.value: {type} = {val}

                def testfunc(c: C{type}):
                    z: int64 = {test}
                    if c.value < z:
                        return True
                    return False
                """
            with self.subTest(type=type, val=val, test=test):
                with self.in_module(codestr) as mod:
                    C = mod["C" + type]
                    f = mod["testfunc"]
                    self.assertEqual(f(C()), False)

    def test_field_sign_compare(self):
        for type, val, test in [("int32", -1, -1)]:
            codestr = f"""
                from __static__ import {type}, box
                class C{type}:
                    def __init__(self):
                        self.value: {type} = {val}

                def testfunc(c: C{type}):
                    if c.value == {test}:
                        return True
                    return False
                """
            with self.subTest(type=type, val=val, test=test):
                with self.in_module(codestr) as mod:
                    C = mod["C" + type]
                    f = mod["testfunc"]
                    self.assertTrue(f(C()))

    def test_mixed_binop_sign(self):
        """mixed signed/unsigned ops should be promoted to signed"""
        codestr = """
            from __static__ import int8, uint8, box
            def testfunc():
                x: uint8 = 42
                y: int8 = 2
                return box(x / y)
        """
        code = self.compile(codestr, StaticCodeGenerator)
        f = self.find_code(code)
        self.assertInBytecode(f, "PRIMITIVE_BINARY_OP", PRIM_OP_DIV_INT)
        with self.in_module(codestr) as mod:
            f = mod["testfunc"]
            self.assertEqual(f(), 21)

        codestr = """
            from __static__ import int8, uint8, box
            def testfunc():
                x: int8 = 42
                y: uint8 = 2
                return box(x / y)
        """
        code = self.compile(codestr, StaticCodeGenerator)
        f = self.find_code(code)
        self.assertInBytecode(f, "PRIMITIVE_BINARY_OP", PRIM_OP_DIV_INT)
        with self.in_module(codestr) as mod:
            f = mod["testfunc"]
            self.assertEqual(f(), 21)

    def test_mixed_cmpop_sign(self):
        """mixed signed/unsigned ops should be promoted to signed"""
        codestr = """
            from __static__ import int8, uint8, box
            def testfunc(tst=False):
                x: uint8 = 42
                y: int8 = 2
                if tst:
                    x += 1
                    y += 1

                if x < y:
                    return True
                return False
        """
        code = self.compile(codestr, StaticCodeGenerator)
        f = self.find_code(code)
        self.assertInBytecode(f, "INT_COMPARE_OP", PRIM_OP_LT_INT)
        with self.in_module(codestr) as mod:
            f = mod["testfunc"]
            self.assertEqual(f(), False)

        codestr = """
            from __static__ import int8, uint8, box
            def testfunc(tst=False):
                x: int8 = 42
                y: uint8 = 2
                if tst:
                    x += 1
                    y += 1

                if x < y:
                    return True
                return False
        """
        code = self.compile(codestr, StaticCodeGenerator)
        f = self.find_code(code)
        self.assertInBytecode(f, "INT_COMPARE_OP", PRIM_OP_LT_INT)
        with self.in_module(codestr) as mod:
            f = mod["testfunc"]
            self.assertEqual(f(), False)

    def test_mixed_add_reversed(self):
        codestr = """
            from __static__ import int8, uint8, int64, box, int16
            def testfunc(tst=False):
                x: int8 = 42
                y: int16 = 2
                if tst:
                    x += 1
                    y += 1

                return box(y + x)
        """
        code = self.compile(codestr, StaticCodeGenerator)
        f = self.find_code(code)
        self.assertInBytecode(f, "PRIMITIVE_BINARY_OP", PRIM_OP_ADD_INT)
        with self.in_module(codestr) as mod:
            f = mod["testfunc"]
            self.assertEqual(f(), 44)

    def test_mixed_tri_add(self):
        codestr = """
            from __static__ import int8, uint8, int64, box
            def testfunc(tst=False):
                x: uint8 = 42
                y: int8 = 2
                z: int64 = 3
                if tst:
                    x += 1
                    y += 1

                return box(x + y + z)
        """
        code = self.compile(codestr, StaticCodeGenerator)
        f = self.find_code(code)
        self.assertInBytecode(f, "PRIMITIVE_BINARY_OP", PRIM_OP_ADD_INT)
        with self.in_module(codestr) as mod:
            f = mod["testfunc"]
            self.assertEqual(f(), 47)

    def test_mixed_tri_add_unsigned(self):
        """promote int/uint to int, can't add to uint64"""

        codestr = """
            from __static__ import int8, uint8, uint64, box
            def testfunc(tst=False):
                x: uint8 = 42
                y: int8 = 2
                z: uint64 = 3

                return box(x + y + z)
        """

        with self.assertRaisesRegex(TypedSyntaxError, "cannot add int16 and uint64"):
            self.compile(codestr, StaticCodeGenerator)

    def test_store_signed_to_unsigned(self):

        codestr = """
            from __static__ import int8, uint8, uint64, box
            def testfunc(tst=False):
                x: uint8 = 42
                y: int8 = 2
                x = y
        """
        with self.assertRaisesRegex(TypedSyntaxError, type_mismatch("int8", "uint8")):
            self.compile(codestr, StaticCodeGenerator)

    def test_store_unsigned_to_signed(self):
        """promote int/uint to int, can't add to uint64"""

        codestr = """
            from __static__ import int8, uint8, uint64, box
            def testfunc(tst=False):
                x: uint8 = 42
                y: int8 = 2
                y = x
        """
        with self.assertRaisesRegex(TypedSyntaxError, type_mismatch("uint8", "int8")):
            self.compile(codestr, StaticCodeGenerator)

    def test_mixed_assign_larger(self):
        """promote int/uint to int16"""

        codestr = """
            from __static__ import int8, uint8, int16, box
            def testfunc(tst=False):
                x: uint8 = 42
                y: int8 = 2
                z: int16 = x + y

                return box(z)
        """
        code = self.compile(codestr, StaticCodeGenerator)
        f = self.find_code(code)
        self.assertInBytecode(f, "PRIMITIVE_BINARY_OP", PRIM_OP_ADD_INT)
        with self.in_module(codestr) as mod:
            f = mod["testfunc"]
            self.assertEqual(f(), 44)

    def test_mixed_assign_larger_2(self):
        """promote int/uint to int16"""

        codestr = """
            from __static__ import int8, uint8, int16, box
            def testfunc(tst=False):
                x: uint8 = 42
                y: int8 = 2
                z: int16
                z = x + y

                return box(z)
        """
        code = self.compile(codestr, StaticCodeGenerator)
        f = self.find_code(code)
        self.assertInBytecode(f, "PRIMITIVE_BINARY_OP", PRIM_OP_ADD_INT)
        with self.in_module(codestr) as mod:
            f = mod["testfunc"]
            self.assertEqual(f(), 44)

    @skipIf(True, "this isn't implemented yet")
    def test_unwind(self):
        codestr = f"""
            from __static__ import int32
            def raises():
                raise IndexError()

            def testfunc():
                x: int32 = 1
                raises()
                print(x)
            """

        with self.in_module(codestr) as mod:
            f = mod["testfunc"]
            with self.assertRaises(IndexError):
                f()

    def test_int_constant_range(self):
        for type, val, low, high in [
            ("int8", 128, -128, 127),
            ("int8", -129, -128, 127),
            ("int16", 32768, -32768, 32767),
            ("int16", -32769, -32768, 32767),
            ("int32", 2147483648, -2147483648, 2147483647),
            ("int32", -2147483649, -2147483648, 2147483647),
            ("int64", 9223372036854775808, -9223372036854775808, 9223372036854775807),
            ("int64", -9223372036854775809, -9223372036854775808, 9223372036854775807),
            ("uint8", 257, 0, 255),
            ("uint8", -1, 0, 255),
            ("uint16", 65537, 0, 65535),
            ("uint16", -1, 0, 65535),
            ("uint32", 4294967297, 0, 4294967295),
            ("uint32", -1, 0, 4294967295),
            ("uint64", 18446744073709551617, 0, 18446744073709551615),
            ("uint64", -1, 0, 18446744073709551615),
        ]:
            codestr = f"""
                from __static__ import {type}
                def testfunc(tst):
                    x: {type} = {val}
            """
            with self.subTest(type=type, val=val, low=low, high=high):
                with self.assertRaisesRegex(
                    TypedSyntaxError,
                    f"constant {val} is outside of the range {low} to {high} for {type}",
                ):
                    self.compile(codestr, StaticCodeGenerator)

    def test_int_assign_float(self):
        codestr = """
            from __static__ import int8
            def testfunc(tst):
                x: int8 = 1.0
        """
        with self.assertRaisesRegex(
            TypedSyntaxError,
            f"float cannot be used in a context where an int is expected",
        ):
            self.compile(codestr, StaticCodeGenerator)

    def test_int_assign_str_constant(self):
        codestr = """
            from __static__ import int8
            def testfunc(tst):
                x: int8 = 'abc' + 'def'
        """
        with self.assertRaisesRegex(
            TypedSyntaxError,
            f"str cannot be used in a context where an int is expected",
        ):
            self.compile(codestr, StaticCodeGenerator)

    def test_int_large_int_constant(self):
        codestr = """
            from __static__ import int64
            def testfunc(tst):
                x: int64 = 0x7FFFFFFF + 1
        """
        code = self.compile(codestr, StaticCodeGenerator)
        f = self.find_code(code)
        self.assertInBytecode(f, "PRIMITIVE_LOAD_CONST", (0x80000000, TYPED_INT64))

    def test_int_int_constant(self):
        codestr = """
            from __static__ import int64
            def testfunc(tst):
                x: int64 = 0x7FFFFFFE + 1
        """
        code = self.compile(codestr, StaticCodeGenerator)
        f = self.find_code(code)
        self.assertInBytecode(f, "PRIMITIVE_LOAD_CONST", (0x7FFFFFFF, TYPED_INT64))

    def test_int_add_mixed_64(self):
        codestr = """
            from __static__ import uint64, int64, box
            def testfunc(tst):
                x: uint64 = 0
                y: int64 = 1
                if tst:
                    x = x + 1
                    y = y + 2

                return box(x + y)
        """
        with self.assertRaisesRegex(TypedSyntaxError, "cannot add uint64 and int64"):
            self.compile(codestr, StaticCodeGenerator)

    def test_int_overflow_add(self):
        tests = [
            ("int8", 100, 100, -56),
            ("int16", 200, 200, 400),
            ("int32", 200, 200, 400),
            ("int64", 200, 200, 400),
            ("int16", 20000, 20000, -25536),
            ("int32", 40000, 40000, 80000),
            ("int64", 40000, 40000, 80000),
            ("int32", 2000000000, 2000000000, -294967296),
            ("int64", 2000000000, 2000000000, 4000000000),
            ("int8", 127, 127, -2),
            ("int16", 32767, 32767, -2),
            ("int32", 2147483647, 2147483647, -2),
            ("int64", 9223372036854775807, 9223372036854775807, -2),
            ("uint8", 200, 200, 144),
            ("uint16", 200, 200, 400),
            ("uint32", 200, 200, 400),
            ("uint64", 200, 200, 400),
            ("uint16", 40000, 40000, 14464),
            ("uint32", 40000, 40000, 80000),
            ("uint64", 40000, 40000, 80000),
            ("uint32", 2000000000, 2000000000, 4000000000),
            ("uint64", 2000000000, 2000000000, 4000000000),
            ("uint8", 1 << 7, 1 << 7, 0),
            ("uint16", 1 << 15, 1 << 15, 0),
            ("uint32", 1 << 31, 1 << 31, 0),
            ("uint64", 1 << 63, 1 << 63, 0),
            ("uint8", 1 << 6, 1 << 6, 128),
            ("uint16", 1 << 14, 1 << 14, 32768),
            ("uint32", 1 << 30, 1 << 30, 2147483648),
            ("uint64", 1 << 62, 1 << 62, 9223372036854775808),
        ]

        for type, x, y, res in tests:
            codestr = f"""
            from __static__ import {type}, box
            def f():
                x: {type} = {x}
                y: {type} = {y}
                z: {type} = x + y
                return box(z)
            """
            with self.subTest(type=type, x=x, y=y, res=res):
                code = self.compile(codestr, StaticCodeGenerator)
                f = self.run_code(codestr, StaticCodeGenerator)["f"]
                self.assertEqual(f(), res, f"{type} {x} {y} {res}")

    def test_int_unary(self):
        tests = [
            ("int8", "-", 1, -1),
            ("uint8", "-", 1, (1 << 8) - 1),
            ("int16", "-", 1, -1),
            ("int16", "-", 256, -256),
            ("uint16", "-", 1, (1 << 16) - 1),
            ("int32", "-", 1, -1),
            ("int32", "-", 65536, -65536),
            ("uint32", "-", 1, (1 << 32) - 1),
            ("int64", "-", 1, -1),
            ("int64", "-", 1 << 32, -(1 << 32)),
            ("uint64", "-", 1, (1 << 64) - 1),
            ("int8", "~", 1, -2),
            ("uint8", "~", 1, (1 << 8) - 2),
            ("int16", "~", 1, -2),
            ("uint16", "~", 1, (1 << 16) - 2),
            ("int32", "~", 1, -2),
            ("uint32", "~", 1, (1 << 32) - 2),
            ("int64", "~", 1, -2),
            ("uint64", "~", 1, (1 << 64) - 2),
        ]
        for type, op, x, res in tests:
            codestr = f"""
            from __static__ import {type}, box
            def testfunc(tst):
                x: {type} = {x}
                if tst:
                    x = x + 1
                x = {op}x
                return box(x)
            """
            with self.subTest(type=type, op=op, x=x, res=res):
                code = self.compile(codestr, StaticCodeGenerator)
                f = self.run_code(codestr, StaticCodeGenerator)["testfunc"]
                self.assertEqual(f(False), res, f"{type} {op} {x} {res}")

    def test_int_compare(self):
        tests = [
            ("int8", 1, 2, "==", False),
            ("int8", 1, 2, "!=", True),
            ("int8", 1, 2, "<", True),
            ("int8", 1, 2, "<=", True),
            ("int8", 2, 1, "<", False),
            ("int8", 2, 1, "<=", False),
            ("int8", -1, 2, "==", False),
            ("int8", -1, 2, "!=", True),
            ("int8", -1, 2, "<", True),
            ("int8", -1, 2, "<=", True),
            ("int8", 2, -1, "<", False),
            ("int8", 2, -1, "<=", False),
            ("uint8", 1, 2, "==", False),
            ("uint8", 1, 2, "!=", True),
            ("uint8", 1, 2, "<", True),
            ("uint8", 1, 2, "<=", True),
            ("uint8", 2, 1, "<", False),
            ("uint8", 2, 1, "<=", False),
            ("uint8", 255, 2, "==", False),
            ("uint8", 255, 2, "!=", True),
            ("uint8", 255, 2, "<", False),
            ("uint8", 255, 2, "<=", False),
            ("uint8", 2, 255, "<", True),
            ("uint8", 2, 255, "<=", True),
            ("int16", 1, 2, "==", False),
            ("int16", 1, 2, "!=", True),
            ("int16", 1, 2, "<", True),
            ("int16", 1, 2, "<=", True),
            ("int16", 2, 1, "<", False),
            ("int16", 2, 1, "<=", False),
            ("int16", -1, 2, "==", False),
            ("int16", -1, 2, "!=", True),
            ("int16", -1, 2, "<", True),
            ("int16", -1, 2, "<=", True),
            ("int16", 2, -1, "<", False),
            ("int16", 2, -1, "<=", False),
            ("uint16", 1, 2, "==", False),
            ("uint16", 1, 2, "!=", True),
            ("uint16", 1, 2, "<", True),
            ("uint16", 1, 2, "<=", True),
            ("uint16", 2, 1, "<", False),
            ("uint16", 2, 1, "<=", False),
            ("uint16", 65535, 2, "==", False),
            ("uint16", 65535, 2, "!=", True),
            ("uint16", 65535, 2, "<", False),
            ("uint16", 65535, 2, "<=", False),
            ("uint16", 2, 65535, "<", True),
            ("uint16", 2, 65535, "<=", True),
            ("int32", 1, 2, "==", False),
            ("int32", 1, 2, "!=", True),
            ("int32", 1, 2, "<", True),
            ("int32", 1, 2, "<=", True),
            ("int32", 2, 1, "<", False),
            ("int32", 2, 1, "<=", False),
            ("int32", -1, 2, "==", False),
            ("int32", -1, 2, "!=", True),
            ("int32", -1, 2, "<", True),
            ("int32", -1, 2, "<=", True),
            ("int32", 2, -1, "<", False),
            ("int32", 2, -1, "<=", False),
            ("uint32", 1, 2, "==", False),
            ("uint32", 1, 2, "!=", True),
            ("uint32", 1, 2, "<", True),
            ("uint32", 1, 2, "<=", True),
            ("uint32", 2, 1, "<", False),
            ("uint32", 2, 1, "<=", False),
            ("uint32", 4294967295, 2, "!=", True),
            ("uint32", 4294967295, 2, "<", False),
            ("uint32", 4294967295, 2, "<=", False),
            ("uint32", 2, 4294967295, "<", True),
            ("uint32", 2, 4294967295, "<=", True),
            ("int64", 1, 2, "==", False),
            ("int64", 1, 2, "!=", True),
            ("int64", 1, 2, "<", True),
            ("int64", 1, 2, "<=", True),
            ("int64", 2, 1, "<", False),
            ("int64", 2, 1, "<=", False),
            ("int64", -1, 2, "==", False),
            ("int64", -1, 2, "!=", True),
            ("int64", -1, 2, "<", True),
            ("int64", -1, 2, "<=", True),
            ("int64", 2, -1, "<", False),
            ("int64", 2, -1, "<=", False),
            ("uint64", 1, 2, "==", False),
            ("uint64", 1, 2, "!=", True),
            ("uint64", 1, 2, "<", True),
            ("uint64", 1, 2, "<=", True),
            ("uint64", 2, 1, "<", False),
            ("uint64", 2, 1, "<=", False),
            ("int64", 2, -1, ">", True),
            ("uint64", 2, 18446744073709551615, ">", False),
            ("int64", 2, -1, "<", False),
            ("uint64", 2, 18446744073709551615, "<", True),
            ("int64", 2, -1, ">=", True),
            ("uint64", 2, 18446744073709551615, ">=", False),
            ("int64", 2, -1, "<=", False),
            ("uint64", 2, 18446744073709551615, "<=", True),
        ]
        for type, x, y, op, res in tests:
            codestr = f"""
            from __static__ import {type}, box
            def testfunc(tst):
                x: {type} = {x}
                y: {type} = {y}
                if tst:
                    x = x + 1
                    y = y + 2

                if x {op} y:
                    return True
                return False
            """
            with self.subTest(type=type, x=x, y=y, op=op, res=res):
                code = self.compile(codestr, StaticCodeGenerator)
                f = self.run_code(codestr, StaticCodeGenerator)["testfunc"]
                self.assertEqual(f(False), res, f"{type} {x} {op} {y} {res}")

    def test_int_compare_unboxed(self):
        codestr = f"""
        from __static__ import ssize_t, unbox
        def testfunc(x, y):
            x1: ssize_t = unbox(x)
            y1: ssize_t = unbox(y)

            if x1 > y1:
                return True
            return False
        """
        code = self.compile(codestr, StaticCodeGenerator)
        f = self.run_code(codestr, StaticCodeGenerator)["testfunc"]
        self.assertInBytecode(f, "POP_JUMP_IF_ZERO")
        self.assertEqual(f(1, 2), False)

    def test_int_compare_mixed(self):
        codestr = """
        from __static__ import box, ssize_t
        x = 1

        def testfunc():
            i: ssize_t = 0
            j = 0
            while box(i < 100) and x:
                i = i + 1
                j = j + 1
            return j
        """

        code = self.compile(codestr, StaticCodeGenerator)
        f = self.run_code(codestr, StaticCodeGenerator)["testfunc"]
        self.assertEqual(f(), 100)
        self.assert_jitted(f)

    def test_int_compare_or(self):
        codestr = """
        from __static__ import box, ssize_t

        def testfunc():
            i: ssize_t = 0
            j = i > 2 or i < -2
            return box(j)
        """

        with self.in_module(codestr, code_gen=StaticCodeGenerator) as mod:
            f = mod["testfunc"]
            self.assertInBytecode(f, "JUMP_IF_NONZERO_OR_POP")
            self.assertIs(f(), False)

    def test_int_compare_and(self):
        codestr = """
        from __static__ import box, ssize_t

        def testfunc():
            i: ssize_t = 0
            j = i > 2 and i > 3
            return box(j)
        """

        with self.in_module(codestr, code_gen=StaticCodeGenerator) as mod:
            f = mod["testfunc"]
            self.assertInBytecode(f, "JUMP_IF_ZERO_OR_POP")
            self.assertIs(f(), False)

    def test_disallow_prim_nonprim_union(self):
        codestr = """
            from __static__ import int32

            def f(y: int):
                x: int32 = 2
                z = x or y
                return z
        """
        with self.assertRaisesRegex(
            TypedSyntaxError,
            r"invalid union type Union\[int32, int\]; unions cannot include primitive types",
        ):
            self.compile(codestr)

    def test_int_binop(self):
        tests = [
            ("int8", 1, 2, "/", 0),
            ("int8", 4, 2, "/", 2),
            ("int8", 4, -2, "/", -2),
            ("uint8", 0xFF, 0x7F, "/", 2),
            ("int16", 4, -2, "/", -2),
            ("uint16", 0xFF, 0x7F, "/", 2),
            ("uint32", 0xFFFF, 0x7FFF, "/", 2),
            ("int32", 4, -2, "/", -2),
            ("uint32", 0xFF, 0x7F, "/", 2),
            ("uint32", 0xFFFFFFFF, 0x7FFFFFFF, "/", 2),
            ("int64", 4, -2, "/", -2),
            ("uint64", 0xFF, 0x7F, "/", 2),
            ("uint64", 0xFFFFFFFFFFFFFFFF, 0x7FFFFFFFFFFFFFFF, "/", 2),
            ("int8", 1, -2, "-", 3),
            ("int8", 1, 2, "-", -1),
            ("int16", 1, -2, "-", 3),
            ("int16", 1, 2, "-", -1),
            ("int32", 1, -2, "-", 3),
            ("int32", 1, 2, "-", -1),
            ("int64", 1, -2, "-", 3),
            ("int64", 1, 2, "-", -1),
            ("int8", 1, -2, "*", -2),
            ("int8", 1, 2, "*", 2),
            ("int16", 1, -2, "*", -2),
            ("int16", 1, 2, "*", 2),
            ("int32", 1, -2, "*", -2),
            ("int32", 1, 2, "*", 2),
            ("int64", 1, -2, "*", -2),
            ("int64", 1, 2, "*", 2),
            ("int8", 1, -2, "&", 0),
            ("int8", 1, 3, "&", 1),
            ("int16", 1, 3, "&", 1),
            ("int16", 1, 3, "&", 1),
            ("int32", 1, 3, "&", 1),
            ("int32", 1, 3, "&", 1),
            ("int64", 1, 3, "&", 1),
            ("int64", 1, 3, "&", 1),
            ("int8", 1, 2, "|", 3),
            ("uint8", 1, 2, "|", 3),
            ("int16", 1, 2, "|", 3),
            ("uint16", 1, 2, "|", 3),
            ("int32", 1, 2, "|", 3),
            ("uint32", 1, 2, "|", 3),
            ("int64", 1, 2, "|", 3),
            ("uint64", 1, 2, "|", 3),
            ("int8", 1, 3, "^", 2),
            ("uint8", 1, 3, "^", 2),
            ("int16", 1, 3, "^", 2),
            ("uint16", 1, 3, "^", 2),
            ("int32", 1, 3, "^", 2),
            ("uint32", 1, 3, "^", 2),
            ("int64", 1, 3, "^", 2),
            ("uint64", 1, 3, "^", 2),
            ("int8", 1, 3, "%", 1),
            ("uint8", 1, 3, "%", 1),
            ("int16", 1, 3, "%", 1),
            ("uint16", 1, 3, "%", 1),
            ("int32", 1, 3, "%", 1),
            ("uint32", 1, 3, "%", 1),
            ("int64", 1, 3, "%", 1),
            ("uint64", 1, 3, "%", 1),
            ("int8", 1, -3, "%", 1),
            ("uint8", 1, 0xFF, "%", 1),
            ("int16", 1, -3, "%", 1),
            ("uint16", 1, 0xFFFF, "%", 1),
            ("int32", 1, -3, "%", 1),
            ("uint32", 1, 0xFFFFFFFF, "%", 1),
            ("int64", 1, -3, "%", 1),
            ("uint64", 1, 0xFFFFFFFFFFFFFFFF, "%", 1),
            ("int8", 1, 2, "<<", 4),
            ("uint8", 1, 2, "<<", 4),
            ("int16", 1, 2, "<<", 4),
            ("uint16", 1, 2, "<<", 4),
            ("int32", 1, 2, "<<", 4),
            ("uint32", 1, 2, "<<", 4),
            ("int64", 1, 2, "<<", 4),
            ("uint64", 1, 2, "<<", 4),
            ("int8", 4, 1, ">>", 2),
            ("int8", -1, 1, ">>", -1),
            ("uint8", 0xFF, 1, ">>", 127),
            ("int16", 4, 1, ">>", 2),
            ("int16", -1, 1, ">>", -1),
            ("uint16", 0xFFFF, 1, ">>", 32767),
            ("int32", 4, 1, ">>", 2),
            ("int32", -1, 1, ">>", -1),
            ("uint32", 0xFFFFFFFF, 1, ">>", 2147483647),
            ("int64", 4, 1, ">>", 2),
            ("int64", -1, 1, ">>", -1),
            ("uint64", 0xFFFFFFFFFFFFFFFF, 1, ">>", 9223372036854775807),
        ]
        for type, x, y, op, res in tests:
            codestr = f"""
            from __static__ import {type}, box
            def testfunc(tst):
                x: {type} = {x}
                y: {type} = {y}
                if tst:
                    x = x + 1
                    y = y + 2

                z: {type} = x {op} y
                return box(z), box(x {op} y)
            """
            with self.subTest(type=type, x=x, y=y, op=op, res=res):
                with self.in_module(codestr) as mod:
                    f = mod["testfunc"]
                    self.assertEqual(f(False), (res, res), f"{type} {x} {op} {y} {res}")

    def test_primitive_arithmetic(self):
        cases = [
            ("int8", 127, "*", 1, 127),
            ("int8", -64, "*", 2, -128),
            ("int8", 0, "*", 4, 0),
            ("uint8", 51, "*", 5, 255),
            ("uint8", 5, "*", 0, 0),
            ("int16", 3123, "*", -10, -31230),
            ("int16", -32767, "*", -1, 32767),
            ("int16", -32768, "*", 1, -32768),
            ("int16", 3, "*", 0, 0),
            ("uint16", 65535, "*", 1, 65535),
            ("uint16", 0, "*", 4, 0),
            ("int32", (1 << 31) - 1, "*", 1, (1 << 31) - 1),
            ("int32", -(1 << 30), "*", 2, -(1 << 31)),
            ("int32", 0, "*", 1, 0),
            ("uint32", (1 << 32) - 1, "*", 1, (1 << 32) - 1),
            ("uint32", 0, "*", 4, 0),
            ("int64", (1 << 63) - 1, "*", 1, (1 << 63) - 1),
            ("int64", -(1 << 62), "*", 2, -(1 << 63)),
            ("int64", 0, "*", 1, 0),
            ("uint64", (1 << 64) - 1, "*", 1, (1 << 64) - 1),
            ("uint64", 0, "*", 4, 0),
            ("int8", 127, "//", 4, 31),
            ("int8", -128, "//", 4, -32),
            ("int8", 0, "//", 4, 0),
            ("uint8", 255, "//", 5, 51),
            ("uint8", 0, "//", 5, 0),
            ("int16", 32767, "//", -1000, -32),
            ("int16", -32768, "//", -1000, 32),
            ("int16", 0, "//", 4, 0),
            ("uint16", 65535, "//", 5, 13107),
            ("uint16", 0, "//", 4, 0),
            ("int32", (1 << 31) - 1, "//", (1 << 31) - 1, 1),
            ("int32", -(1 << 31), "//", 1, -(1 << 31)),
            ("int32", 0, "//", 1, 0),
            ("uint32", (1 << 32) - 1, "//", 500, 8589934),
            ("uint32", 0, "//", 4, 0),
            ("int64", (1 << 63) - 1, "//", 2, (1 << 62) - 1),
            ("int64", -(1 << 63), "//", 2, -(1 << 62)),
            ("int64", 0, "//", 1, 0),
            ("uint64", (1 << 64) - 1, "//", (1 << 64) - 1, 1),
            ("uint64", 0, "//", 4, 0),
            ("int8", 127, "%", 4, 3),
            ("int8", -128, "%", 4, 0),
            ("int8", 0, "%", 4, 0),
            ("uint8", 255, "%", 6, 3),
            ("uint8", 0, "%", 5, 0),
            ("int16", 32767, "%", -1000, 767),
            ("int16", -32768, "%", -1000, -768),
            ("int16", 0, "%", 4, 0),
            ("uint16", 65535, "%", 7, 1),
            ("uint16", 0, "%", 4, 0),
            ("int32", (1 << 31) - 1, "%", (1 << 31) - 1, 0),
            ("int32", -(1 << 31), "%", 1, 0),
            ("int32", 0, "%", 1, 0),
            ("uint32", (1 << 32) - 1, "%", 500, 295),
            ("uint32", 0, "%", 4, 0),
            ("int64", (1 << 63) - 1, "%", 2, 1),
            ("int64", -(1 << 63), "%", 2, 0),
            ("int64", 0, "%", 1, 0),
            ("uint64", (1 << 64) - 1, "%", (1 << 64) - 1, 0),
            ("uint64", 0, "%", 4, 0),
        ]
        for typ, a, op, b, res in cases:
            for const in ["noconst", "constfirst", "constsecond"]:
                if const == "noconst":
                    codestr = f"""
                        from __static__ import {typ}

                        def f(a: {typ}, b: {typ}) -> {typ}:
                            return a {op} b
                    """
                elif const == "constfirst":
                    codestr = f"""
                        from __static__ import {typ}

                        def f(b: {typ}) -> {typ}:
                            return {a} {op} b
                    """
                elif const == "constsecond":
                    codestr = f"""
                        from __static__ import {typ}

                        def f(a: {typ}) -> {typ}:
                            return a {op} {b}
                    """

                with self.subTest(typ=typ, a=a, op=op, b=b, res=res, const=const):
                    with self.in_module(codestr) as mod:
                        f = mod["f"]
                        act = None
                        if const == "noconst":
                            act = f(a, b)
                        elif const == "constfirst":
                            act = f(b)
                        elif const == "constsecond":
                            act = f(a)
                        self.assertEqual(act, res)

    def test_int_binop_type_context(self):
        codestr = f"""
            from __static__ import box, int8, int16

            def f(x: int8, y: int8) -> int:
                z: int16 = x * y
                return box(z)
        """
        with self.in_module(codestr) as mod:
            f = mod["f"]
            self.assertInBytecode(
                f, "CONVERT_PRIMITIVE", TYPED_INT8 | (TYPED_INT16 << 4)
            )
            self.assertEqual(f(120, 120), 14400)

    def test_int_compare_mixed_sign(self):
        tests = [
            ("uint16", 10000, "int16", -1, "<", False),
            ("uint16", 10000, "int16", -1, "<=", False),
            ("int16", -1, "uint16", 10000, ">", False),
            ("int16", -1, "uint16", 10000, ">=", False),
            ("uint32", 10000, "int16", -1, "<", False),
        ]
        for type1, x, type2, y, op, res in tests:
            codestr = f"""
            from __static__ import {type1}, {type2}, box
            def testfunc(tst):
                x: {type1} = {x}
                y: {type2} = {y}
                if tst:
                    x = x + 1
                    y = y + 2

                if x {op} y:
                    return True
                return False
            """
            with self.subTest(type1=type1, x=x, type2=type2, y=y, op=op, res=res):
                code = self.compile(codestr, StaticCodeGenerator)
                f = self.run_code(codestr, StaticCodeGenerator)["testfunc"]
                self.assertEqual(f(False), res, f"{type} {x} {op} {y} {res}")

    def test_int_compare64_mixed_sign(self):
        codestr = """
            from __static__ import uint64, int64
            def testfunc(tst):
                x: uint64 = 0
                y: int64 = 1
                if tst:
                    x = x + 1
                    y = y + 2

                if x < y:
                    return True
                return False
        """
        with self.assertRaises(TypedSyntaxError):
            self.compile(codestr, StaticCodeGenerator)

    def test_compile_method(self):
        code = self.compile(
            """
        from __static__ import ssize_t
        def f():
            x: ssize_t = 42
        """,
            StaticCodeGenerator,
        )

        f = self.find_code(code)
        self.assertInBytecode(f, "PRIMITIVE_LOAD_CONST", (42, TYPED_INT64))

    def test_mixed_compare(self):
        codestr = """
        from __static__ import ssize_t, box, unbox
        def f(a):
            x: ssize_t = 0
            while x != a:
                pass
        """
        with self.assertRaisesRegex(TypedSyntaxError, "can't compare int64 to dynamic"):
            self.compile(codestr, StaticCodeGenerator)

    def test_unbox(self):
        for size, val in [
            ("int8", 126),
            ("int8", -128),
            ("int16", 32766),
            ("int16", -32768),
            ("int32", 2147483646),
            ("int32", -2147483648),
            ("int64", 9223372036854775806),
            ("int64", -9223372036854775808),
            ("uint8", 254),
            ("uint16", 65534),
            ("uint32", 4294967294),
            ("uint64", 18446744073709551614),
        ]:
            codestr = f"""
            from __static__ import {size}, box, unbox
            def f(x):
                y: {size} = unbox(x)
                y = y + 1
                return box(y)
            """

            code = self.compile(codestr, StaticCodeGenerator)
            f = self.find_code(code)
            f = self.run_code(codestr, StaticCodeGenerator)["f"]
            self.assertEqual(f(val), val + 1)

    def test_int_loop_inplace(self):
        codestr = """
        from __static__ import ssize_t, box
        def f():
            i: ssize_t = 0
            while i < 100:
                i += 1
            return box(i)
        """

        code = self.compile(codestr, StaticCodeGenerator)
        f = self.find_code(code)
        f = self.run_code(codestr, StaticCodeGenerator)["f"]
        self.assertEqual(f(), 100)

    def test_int_loop(self):
        codestr = """
        from __static__ import ssize_t, box
        def testfunc():
            i: ssize_t = 0
            while i < 100:
                i = i + 1
            return box(i)
        """

        code = self.compile(codestr, StaticCodeGenerator)
        f = self.find_code(code)

        f = self.run_code(codestr, StaticCodeGenerator)["testfunc"]
        self.assertEqual(f(), 100)

        self.assertInBytecode(f, "PRIMITIVE_LOAD_CONST", (0, TYPED_INT64))
        self.assertInBytecode(f, "LOAD_LOCAL", (0, ("__static__", "int64")))
        self.assertInBytecode(f, "PRIMITIVE_BINARY_OP", PRIM_OP_ADD_INT)
        self.assertInBytecode(f, "INT_COMPARE_OP", PRIM_OP_LT_INT)
        self.assertInBytecode(f, "POP_JUMP_IF_ZERO")

    def test_int_assert(self):
        codestr = """
        from __static__ import ssize_t, box
        def testfunc():
            i: ssize_t = 0
            assert i == 0, "hello there"
        """

        code = self.compile(codestr, StaticCodeGenerator)
        f = self.find_code(code)
        self.assertInBytecode(f, "POP_JUMP_IF_NONZERO")

        f = self.run_code(codestr, StaticCodeGenerator)["testfunc"]
        self.assertEqual(f(), None)

    def test_int_assert_raises(self):
        codestr = """
        from __static__ import ssize_t, box
        def testfunc():
            i: ssize_t = 0
            assert i != 0, "hello there"
        """

        code = self.compile(codestr, StaticCodeGenerator)
        f = self.find_code(code)
        self.assertInBytecode(f, "POP_JUMP_IF_NONZERO")

        with self.assertRaises(AssertionError):
            f = self.run_code(codestr, StaticCodeGenerator)["testfunc"]
            self.assertEqual(f(), None)

    def test_int_loop_reversed(self):
        codestr = """
        from __static__ import ssize_t, box
        def testfunc():
            i: ssize_t = 0
            while 100 > i:
                i = i + 1
            return box(i)
        """

        code = self.compile(codestr, StaticCodeGenerator)
        f = self.find_code(code)
        f = self.run_code(codestr, StaticCodeGenerator)["testfunc"]
        self.assertEqual(f(), 100)

        self.assertInBytecode(f, "PRIMITIVE_LOAD_CONST", (0, TYPED_INT64))
        self.assertInBytecode(f, "LOAD_LOCAL", (0, ("__static__", "int64")))
        self.assertInBytecode(f, "PRIMITIVE_BINARY_OP", PRIM_OP_ADD_INT)
        self.assertInBytecode(f, "INT_COMPARE_OP", PRIM_OP_GT_INT)
        self.assertInBytecode(f, "POP_JUMP_IF_ZERO")

    def test_int_loop_chained(self):
        codestr = """
        from __static__ import ssize_t, box
        def testfunc():
            i: ssize_t = 0
            while -1 < i < 100:
                i = i + 1
            return box(i)
        """

        code = self.compile(codestr, StaticCodeGenerator)
        f = self.find_code(code)
        f = self.run_code(codestr, StaticCodeGenerator)["testfunc"]
        self.assertEqual(f(), 100)

        self.assertInBytecode(f, "PRIMITIVE_LOAD_CONST", (0, TYPED_INT64))
        self.assertInBytecode(f, "LOAD_LOCAL", (0, ("__static__", "int64")))
        self.assertInBytecode(f, "PRIMITIVE_BINARY_OP", PRIM_OP_ADD_INT)
        self.assertInBytecode(f, "INT_COMPARE_OP", PRIM_OP_LT_INT)
        self.assertInBytecode(f, "POP_JUMP_IF_ZERO")

    def test_compare_subclass(self):
        codestr = """
        class C: pass
        class D(C): pass

        x = C() > D()
        """
        code = self.compile(codestr, StaticCodeGenerator)
        self.assertInBytecode(code, "COMPARE_OP")

    def test_compat_int_math(self):
        codestr = """
        from __static__ import ssize_t, box
        def f():
            x: ssize_t = 42
            z: ssize_t = 1 + x
            return box(z)
        """

        code = self.compile(codestr, StaticCodeGenerator)
        f = self.find_code(code)
        self.assertInBytecode(f, "PRIMITIVE_LOAD_CONST", (42, TYPED_INT64))
        self.assertInBytecode(f, "LOAD_LOCAL", (0, ("__static__", "int64")))
        self.assertInBytecode(f, "PRIMITIVE_BINARY_OP", PRIM_OP_ADD_INT)
        f = self.run_code(codestr, StaticCodeGenerator)["f"]
        self.assertEqual(f(), 43)

    def test_unbox_long(self):
        codestr = """
        from __static__ import unbox, int64
        def f():
            x:int64 = unbox(1)
        """

        self.compile(codestr, StaticCodeGenerator)

    def test_unbox_str(self):
        codestr = """
        from __static__ import unbox, int64
        def f():
            x:int64 = unbox('abc')
        """

        with self.in_module(codestr) as mod:
            f = mod["f"]
            with self.assertRaisesRegex(
                TypeError, "(expected int, got str)|(an integer is required)"
            ):
                f()

    def test_unbox_typed(self):
        codestr = """
        from __static__ import int64, box
        def f(i: object):
            x = int64(i)
            return box(x)
        """

        with self.in_module(codestr) as mod:
            f = mod["f"]
            self.assertEqual(f(42), 42)
            self.assertInBytecode(f, "INT_UNBOX")
            with self.assertRaisesRegex(
                TypeError, "(expected int, got str)|(an integer is required)"
            ):
                self.assertEqual(f("abc"), 42)

    def test_unbox_typed_bool(self):
        codestr = """
        from __static__ import int64, box
        def f(i: object):
            x = int64(i)
            return box(x)
        """

        with self.in_module(codestr) as mod:
            f = mod["f"]
            self.assertEqual(f(42), 42)
            self.assertInBytecode(f, "INT_UNBOX")
            self.assertEqual(f(True), 1)
            self.assertEqual(f(False), 0)

    def test_unbox_incompat_type(self):
        codestr = """
        from __static__ import int64, box
        def f(i: str):
            x:int64 = int64(i)
            return box(x)
        """

        with self.assertRaisesRegex(TypedSyntaxError, type_mismatch("str", "int64")):
            self.compile(codestr)

    def test_uninit_value(self):
        codestr = """
        from __static__ import box, int64
        def f():
            x:int64
            return box(x)
            x = 0
        """
        f = self.run_code(codestr, StaticCodeGenerator)["f"]
        self.assertEqual(f(), 0)

    def test_uninit_value_2(self):
        codestr = """
        from __static__ import box, int64
        def testfunc(x):
            if x:
                y:int64 = 42
            return box(y)
        """
        f = self.run_code(codestr, StaticCodeGenerator)["testfunc"]
        self.assertEqual(f(False), 0)

    def test_bad_box(self):
        codestr = """
        from __static__ import box
        box('abc')
        """

        with self.assertRaisesRegex(
            TypedSyntaxError, "can't box non-primitive: Exact\\[str\\]"
        ):
            self.compile(codestr, StaticCodeGenerator)

    def test_bad_unbox(self):
        codestr = """
        from __static__ import unbox, int64
        def f():
            x:int64 = 42
            unbox(x)
        """

        with self.assertRaisesRegex(
            TypedSyntaxError, type_mismatch("int64", "dynamic")
        ):
            self.compile(codestr, StaticCodeGenerator)

    def test_bad_box_2(self):
        codestr = """
        from __static__ import box
        box('abc', 'foo')
        """

        with self.assertRaisesRegex(
            TypedSyntaxError, "box only accepts a single argument"
        ):
            self.compile(codestr, StaticCodeGenerator)

    def test_bad_unbox_2(self):
        codestr = """
        from __static__ import unbox, int64
        def f():
            x:int64 = 42
            unbox(x, y)
        """

        with self.assertRaisesRegex(
            TypedSyntaxError, "unbox only accepts a single argument"
        ):
            self.compile(codestr, StaticCodeGenerator)

    def test_int_reassign(self):
        codestr = """
        from __static__ import ssize_t, box
        def f():
            x: ssize_t = 42
            z: ssize_t = 1 + x
            x = 100
            x = x + x
            return box(z)
        """

        code = self.compile(codestr, StaticCodeGenerator)
        f = self.find_code(code)
        self.assertInBytecode(f, "PRIMITIVE_LOAD_CONST", (42, TYPED_INT64))
        self.assertInBytecode(f, "LOAD_LOCAL", (0, ("__static__", "int64")))
        self.assertInBytecode(f, "PRIMITIVE_BINARY_OP", PRIM_OP_ADD_INT)
        f = self.run_code(codestr, StaticCodeGenerator)["f"]
        self.assertEqual(f(), 43)

    def test_assign_to_object(self):
        codestr = """
        def f():
            x: object
            x = None
            x = 1
            x = 'abc'
            x = []
            x = {}
            x = {1, 2}
            x = ()
            x = 1.0
            x = 1j
            x = b'foo'
            x = int
            x = True
            x = NotImplemented
            x = ...
        """

        self.compile(codestr, StaticCodeGenerator)

    def test_global_call_add(self) -> None:
        codestr = """
            X = ord(42)
            def f():
                y = X + 1
        """
        code = self.compile(codestr, StaticCodeGenerator, modname="foo")

    def test_type_binder(self) -> None:
        self.assertEqual(self.bind_expr("42"), INT_EXACT_TYPE.instance)
        self.assertEqual(self.bind_expr("42.0"), FLOAT_EXACT_TYPE.instance)
        self.assertEqual(self.bind_expr("'abc'"), STR_EXACT_TYPE.instance)
        self.assertEqual(self.bind_expr("b'abc'"), BYTES_TYPE.instance)
        self.assertEqual(self.bind_expr("3j"), COMPLEX_EXACT_TYPE.instance)
        self.assertEqual(self.bind_expr("None"), NONE_TYPE.instance)
        self.assertEqual(self.bind_expr("True"), BOOL_TYPE.instance)
        self.assertEqual(self.bind_expr("False"), BOOL_TYPE.instance)
        self.assertEqual(self.bind_expr("..."), ELLIPSIS_TYPE.instance)
        self.assertEqual(self.bind_expr("f''"), STR_EXACT_TYPE.instance)
        self.assertEqual(self.bind_expr("f'{x}'"), STR_EXACT_TYPE.instance)

        self.assertEqual(self.bind_expr("a"), DYNAMIC)
        self.assertEqual(self.bind_expr("a.b"), DYNAMIC)
        self.assertEqual(self.bind_expr("a + b"), DYNAMIC)

        self.assertEqual(self.bind_expr("1 + 2"), INT_EXACT_TYPE.instance)
        self.assertEqual(self.bind_expr("1 - 2"), INT_EXACT_TYPE.instance)
        self.assertEqual(self.bind_expr("1 // 2"), INT_EXACT_TYPE.instance)
        self.assertEqual(self.bind_expr("1 * 2"), INT_EXACT_TYPE.instance)
        self.assertEqual(self.bind_expr("1 / 2"), FLOAT_EXACT_TYPE.instance)
        self.assertEqual(self.bind_expr("1 % 2"), INT_EXACT_TYPE.instance)
        self.assertEqual(self.bind_expr("1 & 2"), INT_EXACT_TYPE.instance)
        self.assertEqual(self.bind_expr("1 | 2"), INT_EXACT_TYPE.instance)
        self.assertEqual(self.bind_expr("1 ^ 2"), INT_EXACT_TYPE.instance)
        self.assertEqual(self.bind_expr("1 << 2"), INT_EXACT_TYPE.instance)
        self.assertEqual(self.bind_expr("100 >> 2"), INT_EXACT_TYPE.instance)

        self.assertEqual(self.bind_stmt("x = 1"), INT_EXACT_TYPE.instance)
        # self.assertEqual(self.bind_stmt("x: foo = 1").target.comp_type, DYNAMIC)
        self.assertEqual(self.bind_stmt("x += 1"), DYNAMIC)
        self.assertEqual(self.bind_expr("a or b"), DYNAMIC)
        self.assertEqual(self.bind_expr("+a"), DYNAMIC)
        self.assertEqual(self.bind_expr("not a"), BOOL_TYPE.instance)
        self.assertEqual(self.bind_expr("lambda: 42"), DYNAMIC)
        self.assertEqual(self.bind_expr("a if b else c"), DYNAMIC)
        self.assertEqual(self.bind_expr("x > y"), DYNAMIC)
        self.assertEqual(self.bind_expr("x()"), DYNAMIC)
        self.assertEqual(self.bind_expr("x(y)"), DYNAMIC)
        self.assertEqual(self.bind_expr("x[y]"), DYNAMIC)
        self.assertEqual(self.bind_expr("x[1:2]"), DYNAMIC)
        self.assertEqual(self.bind_expr("x[1:2:3]"), DYNAMIC)
        self.assertEqual(self.bind_expr("x[:]"), DYNAMIC)
        self.assertEqual(self.bind_expr("{}"), DICT_EXACT_TYPE.instance)
        self.assertEqual(self.bind_expr("{2:3}"), DICT_EXACT_TYPE.instance)
        self.assertEqual(self.bind_expr("{1,2}"), SET_EXACT_TYPE.instance)
        self.assertEqual(self.bind_expr("[]"), LIST_EXACT_TYPE.instance)
        self.assertEqual(self.bind_expr("[1,2]"), LIST_EXACT_TYPE.instance)
        self.assertEqual(self.bind_expr("(1,2)"), TUPLE_EXACT_TYPE.instance)

        self.assertEqual(self.bind_expr("[x for x in y]"), LIST_EXACT_TYPE.instance)
        self.assertEqual(self.bind_expr("{x for x in y}"), SET_EXACT_TYPE.instance)
        self.assertEqual(self.bind_expr("{x:y for x in y}"), DICT_EXACT_TYPE.instance)
        self.assertEqual(self.bind_expr("(x for x in y)"), DYNAMIC)

        def body_get(stmt):
            return stmt.body[0].value

        self.assertEqual(
            self.bind_stmt("def f(): return 42", getter=body_get),
            INT_EXACT_TYPE.instance,
        )
        self.assertEqual(self.bind_stmt("def f(): yield 42", getter=body_get), DYNAMIC)
        self.assertEqual(
            self.bind_stmt("def f(): yield from x", getter=body_get), DYNAMIC
        )
        self.assertEqual(
            self.bind_stmt("async def f(): await x", getter=body_get), DYNAMIC
        )

        self.assertEqual(self.bind_expr("object"), OBJECT_TYPE)

        self.assertEqual(
            self.bind_expr("1 + 2", optimize=True), INT_EXACT_TYPE.instance
        )

    def test_if_exp(self) -> None:
        mod, syms = self.bind_module(
            """
            class C: pass
            class D: pass

            x = C() if a else D()
        """
        )
        node = mod.body[-1]
        types = syms.modules["foo"].types
        self.assertEqual(types[node], DYNAMIC)

        mod, syms = self.bind_module(
            """
            class C: pass

            x = C() if a else C()
        """
        )
        node = mod.body[-1]
        types = syms.modules["foo"].types
        self.assertEqual(types[node.value].name, "foo.C")

    def test_cmpop(self):
        codestr = """
            from __static__ import int32
            def f():
                i: int32 = 0
                j: int = 0

                if i == 0:
                    return 0
                if j == 0:
                    return 1
        """
        code = self.compile(codestr, StaticCodeGenerator, modname="foo")
        x = self.find_code(code, "f")
        self.assertInBytecode(x, "INT_COMPARE_OP", 0)
        self.assertInBytecode(x, "COMPARE_OP", "==")

    def test_bind_instance(self) -> None:
        mod, syms = self.bind_module("class C: pass\na: C = C()")
        assign = mod.body[1]
        types = syms.modules["foo"].types
        self.assertEqual(types[assign.target].name, "foo.C")
        self.assertEqual(repr(types[assign.target]), "<foo.C>")

    def test_bind_func_def(self) -> None:
        mod, syms = self.bind_module(
            """
            def f(x: object = None, y: object = None):
                pass
        """
        )
        modtable = syms.modules["foo"]
        self.assertTrue(isinstance(modtable.children["f"], Function))

    def assertReturns(self, code: str, typename: str) -> None:
        actual = self.bind_final_return(code).name
        self.assertEqual(actual, typename)

    def bind_final_return(self, code: str) -> Value:
        mod, syms = self.bind_module(code)
        types = syms.modules["foo"].types
        node = mod.body[-1].body[-1].value
        return types[node]

    def bind_stmt(
        self, code: str, optimize: bool = False, getter=lambda stmt: stmt
    ) -> ast.stmt:
        mod, syms = self.bind_module(code, optimize)
        assert len(mod.body) == 1
        types = syms.modules["foo"].types
        return types[getter(mod.body[0])]

    def bind_expr(self, code: str, optimize: bool = False) -> Value:
        mod, syms = self.bind_module(code, optimize)
        assert len(mod.body) == 1
        types = syms.modules["foo"].types
        return types[mod.body[0].value]

    def bind_module(
        self, code: str, optimize: bool = False
    ) -> Tuple[ast.Module, SymbolTable]:
        tree = ast.parse(dedent(code))
        if optimize:
            tree = AstOptimizer().visit(tree)

        symtable = SymbolTable()
        decl_visit = DeclarationVisitor("foo", "foo.py", symtable)
        decl_visit.visit(tree)
        decl_visit.module.finish_bind()

        s = SymbolVisitor()
        walk(tree, s)

        type_binder = TypeBinder(s, "foo.py", symtable, "foo")
        type_binder.visit(tree)

        # Make sure we can compile the code, just verifying all nodes are
        # visited.
        graph = StaticCodeGenerator.flow_graph("foo", "foo.py", s.scopes[tree])
        code_gen = StaticCodeGenerator(None, tree, s, graph, symtable, "foo", optimize)
        code_gen.visit(tree)

        return tree, symtable

    def test_cross_module(self) -> None:
        acode = """
            class C:
                def f(self):
                    return 42
        """
        bcode = """
            from a import C

            def f():
                x = C()
                return x.f()
        """
        symtable = SymbolTable()
        acomp = symtable.compile("a", "a.py", ast.parse(dedent(acode)))
        bcomp = symtable.compile("b", "b.py", ast.parse(dedent(bcode)))
        x = self.find_code(bcomp, "f")
        self.assertInBytecode(x, "INVOKE_METHOD", (("a", "C", "f"), 0))

    def test_cross_module_import_time_resolution(self) -> None:
        class TestSymbolTable(SymbolTable):
            def import_module(self, name):
                if name == "a":
                    symtable.add_module("a", "a.py", ast.parse(dedent(acode)))

        acode = """
            class C:
                def f(self):
                    return 42
        """
        bcode = """
            from a import C

            def f():
                x = C()
                return x.f()
        """
        symtable = TestSymbolTable()
        bcomp = symtable.compile("b", "b.py", ast.parse(dedent(bcode)))
        x = self.find_code(bcomp, "f")
        self.assertInBytecode(x, "INVOKE_METHOD", (("a", "C", "f"), 0))

    def test_cross_module_type_checking(self) -> None:
        acode = """
            class C:
                def f(self):
                    return 42
        """
        bcode = """
            from typing import TYPE_CHECKING

            if TYPE_CHECKING:
                from a import C

            def f(x: C):
                return x.f()
        """
        symtable = SymbolTable()
        symtable.add_module("a", "a.py", ast.parse(dedent(acode)))
        symtable.add_module("b", "b.py", ast.parse(dedent(bcode)))
        acomp = symtable.compile("a", "a.py", ast.parse(dedent(acode)))
        bcomp = symtable.compile("b", "b.py", ast.parse(dedent(bcode)))
        x = self.find_code(bcomp, "f")
        self.assertInBytecode(x, "INVOKE_METHOD", (("a", "C", "f"), 0))

    def test_primitive_invoke(self) -> None:
        codestr = """
            from __static__ import int8
            def f():
                x: int8 = 42
                print(x.__str__())
        """
        with self.assertRaisesRegex(
            TypedSyntaxError, "cannot load attribute from int8"
        ):
            self.compile(codestr, StaticCodeGenerator)

    def test_primitive_call(self) -> None:
        codestr = """
            from __static__ import int8
            def f():
                x: int8 = 42
                print(x())
        """
        with self.assertRaisesRegex(TypedSyntaxError, "cannot call int8"):
            self.compile(codestr, StaticCodeGenerator)

    def test_primitive_subscr(self) -> None:
        codestr = """
            from __static__ import int8
            def f():
                x: int8 = 42
                print(x[42])
        """
        with self.assertRaisesRegex(TypedSyntaxError, "cannot index int8"):
            self.compile(codestr, StaticCodeGenerator)

    def test_primitive_iter(self) -> None:
        codestr = """
            from __static__ import int8
            def f():
                x: int8 = 42
                for a in x:
                    pass
        """
        with self.assertRaisesRegex(TypedSyntaxError, "cannot iterate over int8"):
            self.compile(codestr, StaticCodeGenerator)

    def test_pseudo_strict_module(self) -> None:
        # simulate strict modules where the builtins come from <builtins>
        code = """
            def f(a):
                x: bool = a
        """
        builtins = ast.Assign(
            [ast.Name("bool", ast.Store())],
            ast.Subscript(
                ast.Name("<builtins>", ast.Load()),
                ast.Index(ast.Str("bool")),
                ast.Load(),
            ),
            None,
        )
        tree = ast.parse(dedent(code))
        tree.body.insert(0, builtins)

        symtable = SymbolTable()
        symtable.add_module("a", "a.py", tree)
        acomp = symtable.compile("a", "a.py", tree)
        x = self.find_code(acomp, "f")
        self.assertInBytecode(x, "CAST", ("builtins", "bool"))

    def test_aug_assign(self) -> None:
        codestr = """
        def f(l):
            l[0] += 1
        """
        with self.in_module(codestr) as mod:
            f = mod["f"]
            l = [1]
            f(l)
            self.assertEqual(l[0], 2)

    def test_pseudo_strict_module_constant(self) -> None:
        # simulate strict modules where the builtins come from <builtins>
        code = """
            def f(a):
                x: bool = a
        """
        builtins = ast.Assign(
            [ast.Name("bool", ast.Store())],
            ast.Subscript(
                ast.Name("<builtins>", ast.Load()),
                ast.Index(ast.Constant("bool")),
                ast.Load(),
            ),
            None,
        )
        tree = ast.parse(dedent(code))
        tree.body.insert(0, builtins)

        symtable = SymbolTable()
        symtable.add_module("a", "a.py", tree)
        acomp = symtable.compile("a", "a.py", tree)
        x = self.find_code(acomp, "f")
        self.assertInBytecode(x, "CAST", ("builtins", "bool"))

    def test_cross_module_inheritance(self) -> None:
        acode = """
            class C:
                def f(self):
                    return 42
        """
        bcode = """
            from a import C

            class D(C):
                def f(self):
                    return 'abc'

            def f(y):
                x: C
                if y:
                    x = D()
                else:
                    x = C()
                return x.f()
        """
        symtable = SymbolTable()
        symtable.add_module("a", "a.py", ast.parse(dedent(acode)))
        symtable.add_module("b", "b.py", ast.parse(dedent(bcode)))
        acomp = symtable.compile("a", "a.py", ast.parse(dedent(acode)))
        bcomp = symtable.compile("b", "b.py", ast.parse(dedent(bcode)))
        x = self.find_code(bcomp, "f")
        self.assertInBytecode(x, "INVOKE_METHOD", (("a", "C", "f"), 0))

    def test_annotated_function(self):
        codestr = """
        class C:
            def f(self) -> int:
                return 1

        def x(c: C):
            x = c.f()
            x += c.f()
            return x
        """

        code = self.compile(codestr, StaticCodeGenerator, modname="foo")
        x = self.find_code(code, "x")
        self.assertInBytecode(x, "INVOKE_METHOD", (("foo", "C", "f"), 0))

        with self.in_module(codestr) as mod:
            x, C = mod["x"], mod["C"]
            c = C()
            self.assertEqual(x(c), 2)

    def test_invoke_new_derived(self):
        codestr = """
            class C:
                def f(self):
                    return 1

            def x(c: C):
                x = c.f()
                x += c.f()
                return x

            a = x(C())

            class D(C):
                def f(self):
                    return 2

            b = x(D())
        """

        code = self.compile(codestr, StaticCodeGenerator, modname="foo")
        x = self.find_code(code, "x")
        self.assertInBytecode(x, "INVOKE_METHOD", (("foo", "C", "f"), 0))

        with self.in_module(codestr) as mod:
            a, b = mod["a"], mod["b"]
            self.assertEqual(a, 2)
            self.assertEqual(b, 4)

    def test_invoke_explicit_slots(self):
        codestr = """
            class C:
                __slots__ = ()
                def f(self):
                    return 1

            def x(c: C):
                x = c.f()
                x += c.f()
                return x

            a = x(C())
        """

        code = self.compile(codestr, StaticCodeGenerator, modname="foo")
        x = self.find_code(code, "x")
        self.assertInBytecode(x, "INVOKE_METHOD", (("foo", "C", "f"), 0))

        with self.in_module(codestr) as mod:
            a = mod["a"]
            self.assertEqual(a, 2)

    def test_invoke_new_derived_nonfunc(self):
        codestr = """
            class C:
                def f(self):
                    return 1

            def x(c: C):
                x = c.f()
                x += c.f()
                return x
        """

        code = self.compile(codestr, StaticCodeGenerator, modname="foo")
        x = self.find_code(code, "x")
        self.assertInBytecode(x, "INVOKE_METHOD", (("foo", "C", "f"), 0))

        with self.in_module(codestr) as mod:
            x, C = mod["x"], mod["C"]
            self.assertEqual(x(C()), 2)

            class Callable:
                def __call__(self_, obj):
                    self.assertTrue(isinstance(obj, D))
                    return 42

            class D(C):
                f = Callable()

            d = D()
            self.assertEqual(x(d), 84)

    def test_invoke_new_derived_nonfunc_slots(self):
        codestr = """
            class C:
                def f(self):
                    return 1

            def x(c: C):
                x = c.f()
                x += c.f()
                return x
        """

        code = self.compile(codestr, StaticCodeGenerator, modname="foo")
        x = self.find_code(code, "x")
        self.assertInBytecode(x, "INVOKE_METHOD", (("foo", "C", "f"), 0))

        with self.in_module(codestr) as mod:
            x, C = mod["x"], mod["C"]
            self.assertEqual(x(C()), 2)

            class Callable:
                def __call__(self_, obj):
                    self.assertTrue(isinstance(obj, D))
                    return 42

            class D(C):
                __slots__ = ()
                f = Callable()

            d = D()
            self.assertEqual(x(d), 84)

    def test_invoke_new_derived_nonfunc_descriptor(self):
        codestr = """
            class C:
                def f(self):
                    return 1

            def x(c: C):
                x = c.f()
                x += c.f()
                return x
        """

        code = self.compile(codestr, StaticCodeGenerator, modname="foo")
        x = self.find_code(code, "x")
        self.assertInBytecode(x, "INVOKE_METHOD", (("foo", "C", "f"), 0))

        with self.in_module(codestr) as mod:
            x, C = mod["x"], mod["C"]
            self.assertEqual(x(C()), 2)

            class Callable:
                def __call__(self):
                    return 42

            class Descr:
                def __get__(self, inst, ctx):
                    return Callable()

            class D(C):
                f = Descr()

            d = D()
            self.assertEqual(x(d), 84)

    def test_invoke_new_derived_nonfunc_data_descriptor(self):
        codestr = """
            class C:
                def f(self):
                    return 1

            def x(c: C):
                x = c.f()
                x += c.f()
                return x
        """

        code = self.compile(codestr, StaticCodeGenerator, modname="foo")
        x = self.find_code(code, "x")
        self.assertInBytecode(x, "INVOKE_METHOD", (("foo", "C", "f"), 0))

        with self.in_module(codestr) as mod:
            x, C = mod["x"], mod["C"]
            self.assertEqual(x(C()), 2)

            class Callable:
                def __call__(self):
                    return 42

            class Descr:
                def __get__(self, inst, ctx):
                    return Callable()

                def __set__(self, inst, value):
                    raise ValueError("no way")

            class D(C):
                f = Descr()

            d = D()
            self.assertEqual(x(d), 84)

    def test_invoke_new_derived_nonfunc_descriptor_inst_override(self):
        codestr = """
            class C:
                def f(self):
                    return 1

            def x(c: C):
                x = c.f()
                x += c.f()
                return x
        """

        code = self.compile(codestr, StaticCodeGenerator, modname="foo")
        x = self.find_code(code, "x")
        self.assertInBytecode(x, "INVOKE_METHOD", (("foo", "C", "f"), 0))

        with self.in_module(codestr) as mod:
            x, C = mod["x"], mod["C"]
            self.assertEqual(x(C()), 2)

            class Callable:
                def __call__(self):
                    return 42

            class Descr:
                def __get__(self, inst, ctx):
                    return Callable()

            class D(C):
                f = Descr()

            d = D()
            self.assertEqual(x(d), 84)
            d.__dict__["f"] = lambda x: 100
            self.assertEqual(x(d), 200)

    def test_invoke_new_derived_nonfunc_descriptor_modified(self):
        codestr = """
            class C:
                def f(self):
                    return 1

            def x(c: C):
                x = c.f()
                x += c.f()
                return x
        """

        code = self.compile(codestr, StaticCodeGenerator, modname="foo")
        x = self.find_code(code, "x")
        self.assertInBytecode(x, "INVOKE_METHOD", (("foo", "C", "f"), 0))

        with self.in_module(codestr) as mod:
            x, C = mod["x"], mod["C"]
            self.assertEqual(x(C()), 2)

            class Callable:
                def __call__(self):
                    return 42

            class Descr:
                def __get__(self, inst, ctx):
                    return Callable()

                def __call__(self, arg):
                    return 23

            class D(C):
                f = Descr()

            d = D()
            self.assertEqual(x(d), 84)
            del Descr.__get__
            self.assertEqual(x(d), 46)

    def test_invoke_dict_override(self):
        codestr = """
            class C:
                def f(self):
                    return 1

            def x(c: C):
                x = c.f()
                x += c.f()
                return x
        """

        code = self.compile(codestr, StaticCodeGenerator, modname="foo")
        x = self.find_code(code, "x")
        self.assertInBytecode(x, "INVOKE_METHOD", (("foo", "C", "f"), 0))

        with self.in_module(codestr) as mod:
            x, C = mod["x"], mod["C"]
            self.assertEqual(x(C()), 2)

            class D(C):
                def __init__(self):
                    self.f = lambda: 42

            d = D()
            self.assertEqual(x(d), 84)

    def test_invoke_type_modified(self):
        codestr = """
            class C:
                def f(self):
                    return 1

            def x(c: C):
                x = c.f()
                x += c.f()
                return x
        """

        code = self.compile(codestr, StaticCodeGenerator, modname="foo")
        x = self.find_code(code, "x")
        self.assertInBytecode(x, "INVOKE_METHOD", (("foo", "C", "f"), 0))

        with self.in_module(codestr) as mod:
            x, C = mod["x"], mod["C"]
            self.assertEqual(x(C()), 2)
            C.f = lambda self: 42
            self.assertEqual(x(C()), 84)

    def test_annotated_function_derived(self):
        codestr = """
            class C:
                def f(self) -> int:
                    return 1

            class D(C):
                def f(self) -> int:
                    return 2

            class E(C):
                pass

            def x(c: C,):
                x = c.f()
                x += c.f()
                return x
        """

        code = self.compile(
            codestr, StaticCodeGenerator, modname="test_annotated_function_derived"
        )
        x = self.find_code(code, "x")
        self.assertInBytecode(
            x, "INVOKE_METHOD", (("test_annotated_function_derived", "C", "f"), 0)
        )

        with self.in_module(codestr) as mod:
            x = mod["x"]
            self.assertEqual(x(mod["C"]()), 2)
            self.assertEqual(x(mod["D"]()), 4)
            self.assertEqual(x(mod["E"]()), 2)

    def test_conditional_init(self):
        codestr = f"""
            from __static__ import box, int64

            class C:
                def __init__(self, init=True):
                    if init:
                        self.value: int64 = 1

                def f(self) -> int:
                    return box(self.value)
        """

        with self.in_module(codestr) as mod:
            C = mod["C"]
            x = C()
            self.assertEqual(x.f(), 1)
            x = C(False)
            self.assertEqual(x.f(), 0)
            self.assertInBytecode(
                C.f, "LOAD_FIELD", ("test_conditional_init", "C", "value")
            )

    def test_error_incompat_assign_local(self):
        codestr = """
            class C:
                def __init__(self):
                    self.x = None

                def f(self):
                    x: "C" = self.x
        """
        with self.in_module(codestr) as mod:
            C = mod["C"]
            with self.assertRaisesRegex(TypeError, "expected 'C', got 'NoneType'"):
                C().f()

    def test_error_incompat_field_non_dynamic(self):
        codestr = """
            class C:
                def __init__(self):
                    self.x: int = 'abc'
        """
        with self.assertRaises(TypedSyntaxError):
            self.compile(codestr, StaticCodeGenerator)

    def test_error_incompat_field(self):
        codestr = """
            class C:
                def __init__(self):
                    self.x: int = 100

                def f(self, x):
                    self.x = x
        """
        with self.in_module(codestr) as mod:
            C = mod["C"]
            C().f(42)
            with self.assertRaises(TypeError):
                C().f("abc")

    def test_error_incompat_assign_dynamic(self):
        with self.assertRaises(TypedSyntaxError):
            code = self.compile(
                """
            class C:
                x: "C"
                def __init__(self):
                    self.x = None
            """,
                StaticCodeGenerator,
            )

    def test_annotated_class_var(self):
        codestr = """
            class C:
                x: int
        """
        code = self.compile(
            codestr, StaticCodeGenerator, modname="test_annotated_class_var"
        )

    def test_annotated_instance_var(self):
        codestr = """
            class C:
                def __init__(self):
                    self.x: str = 'abc'
        """
        code = self.compile(
            codestr, StaticCodeGenerator, modname="test_annotated_instance_var"
        )
        # get C from module, and then get __init__ from C
        code = self.find_code(self.find_code(code))
        self.assertInBytecode(code, "STORE_FIELD")

    def test_load_store_attr_value(self):
        codestr = """
            class C:
                x: int

                def __init__(self, value: int):
                    self.x = value

                def f(self):
                    return self.x
        """
        code = self.compile(codestr, StaticCodeGenerator, modname="foo")
        init = self.find_code(self.find_code(code), "__init__")
        self.assertInBytecode(init, "STORE_FIELD")
        f = self.find_code(self.find_code(code), "f")
        self.assertInBytecode(f, "LOAD_FIELD")
        with self.in_module(codestr) as mod:
            C = mod["C"]
            a = C(42)
            self.assertEqual(a.f(), 42)

    def test_load_store_attr(self):
        codestr = """
            class C:
                x: "C"

                def __init__(self):
                    self.x = self

                def g(self):
                    return 42

                def f(self):
                    return self.x.g()
        """
        with self.in_module(codestr) as mod:
            C = mod["C"]
            a = C()
            self.assertEqual(a.f(), 42)

    def test_load_store_attr_init(self):
        codestr = """
            class C:
                def __init__(self):
                    self.x: C = self

                def g(self):
                    return 42

                def f(self):
                    return self.x.g()
        """
        code = self.compile(codestr, StaticCodeGenerator, modname="foo")

        with self.in_module(codestr) as mod:
            C = mod["C"]
            a = C()
            self.assertEqual(a.f(), 42)

    def test_load_store_attr_init_no_ann(self):
        codestr = """
            class C:
                def __init__(self):
                    self.x = self

                def g(self):
                    return 42

                def f(self):
                    return self.x.g()
        """
        code = self.compile(codestr, StaticCodeGenerator, modname="foo")

        with self.in_module(codestr) as mod:
            C = mod["C"]
            a = C()
            self.assertEqual(a.f(), 42)

    def test_unknown_annotation(self):
        codestr = """
            def f(a):
                x: foo = a
                return x.bar
        """
        code = self.compile(codestr, StaticCodeGenerator, modname="foo")

        class C:
            bar = 42

        f = self.run_code(codestr, StaticCodeGenerator)["f"]
        self.assertEqual(f(C()), 42)

    def test_class_method_invoke(self):
        codestr = """
            class B:
                def __init__(self, value):
                    self.value = value

            class D(B):
                def __init__(self, value):
                    B.__init__(self, value)

                def f(self):
                    return self.value
        """
        code = self.compile(codestr, StaticCodeGenerator, modname="foo")

        b_init = self.find_code(self.find_code(code, "B"), "__init__")
        self.assertInBytecode(b_init, "STORE_FIELD", ("foo", "B", "value"))

        f = self.find_code(self.find_code(code, "D"), "f")
        self.assertInBytecode(f, "LOAD_FIELD", ("foo", "B", "value"))

        with self.in_module(codestr) as mod:
            D = mod["D"]
            d = D(42)
            self.assertEqual(d.f(), 42)

    def test_slotification(self):
        codestr = """
            class C:
                x: "unknown_type"
        """
        code = self.compile(codestr, StaticCodeGenerator, modname="foo")
        C = self.run_code(codestr, StaticCodeGenerator)["C"]
        self.assertEqual(type(C.x), MemberDescriptorType)

    def test_slotification_init(self):
        codestr = """
            class C:
                x: "unknown_type"
                def __init__(self):
                    self.x = 42
        """
        code = self.compile(codestr, StaticCodeGenerator, modname="foo")
        C = self.run_code(codestr, StaticCodeGenerator)["C"]
        self.assertEqual(type(C.x), MemberDescriptorType)

    def test_slotification_ann_init(self):
        codestr = """
            class C:
                x: "unknown_type"
                def __init__(self):
                    self.x: "unknown_type" = 42
        """
        code = self.compile(codestr, StaticCodeGenerator, modname="foo")
        C = self.run_code(codestr, StaticCodeGenerator)["C"]
        self.assertEqual(type(C.x), MemberDescriptorType)

    def test_slotification_typed(self):
        codestr = """
            class C:
                x: int
        """
        C = self.run_code(codestr, StaticCodeGenerator)["C"]
        self.assertNotEqual(type(C.x), MemberDescriptorType)

    def test_slotification_init_typed(self):
        codestr = """
            class C:
                x: int
                def __init__(self):
                    self.x = 42
        """
        with self.in_module(codestr) as mod:
            C = mod["C"]
            self.assertNotEqual(type(C.x), MemberDescriptorType)
            x = C()
            self.assertEqual(x.x, 42)
            with self.assertRaisesRegex(
                TypeError, "expected 'int', got 'str' for attribute 'x'"
            ) as e:
                x.x = "abc"

    def test_slotification_ann_init_typed(self):
        codestr = """
            class C:
                x: int
                def __init__(self):
                    self.x: int = 42
        """
        C = self.run_code(codestr, StaticCodeGenerator)["C"]
        self.assertNotEqual(type(C.x), MemberDescriptorType)

    def test_slotification_conflicting_types(self):
        codestr = """
            class C:
                x: object
                def __init__(self):
                    self.x: int = 42
        """
        with self.assertRaisesRegex(
            TypedSyntaxError,
            r"conflicting type definitions for slot x in Type\[foo.C\]",
        ):
            self.compile(codestr, StaticCodeGenerator, modname="foo")

    def test_slotification_conflicting_types_imported(self):
        self.type_error(
            """
            from typing import Optional

            class C:
                x: Optional[int]
                def __init__(self):
                    self.x: Optional[str] = "foo"
            """,
            r"conflicting type definitions for slot x in Type\[<module>.C\]",
        )

    def test_slotification_conflicting_members(self):
        codestr = """
            class C:
                def x(self): pass
                x: object
        """
        with self.assertRaisesRegex(
            TypedSyntaxError, r"slot conflicts with other member x in Type\[foo.C\]"
        ):
            self.compile(codestr, StaticCodeGenerator, modname="foo")

    def test_slotification_conflicting_function(self):
        codestr = """
            class C:
                x: object
                def x(self): pass
        """
        with self.assertRaisesRegex(
            TypedSyntaxError, r"function conflicts with other member x in Type\[foo.C\]"
        ):
            self.compile(codestr, StaticCodeGenerator, modname="foo")

    def test_slot_inheritance(self):
        codestr = """
            class B:
                def __init__(self):
                    self.x = 42

                def f(self):
                    return self.x

            class D(B):
                def __init__(self):
                    self.x = 100
        """
        code = self.compile(codestr, StaticCodeGenerator, modname="foo")
        with self.in_module(codestr) as mod:
            D = mod["D"]
            inst = D()
            self.assertEqual(inst.f(), 100)

    def test_del_slot(self):
        codestr = """
        class C:
            x: object

        def f(a: C):
            del a.x
        """
        code = self.compile(codestr, StaticCodeGenerator, modname="foo")
        code = self.find_code(code, name="f")
        self.assertInBytecode(code, "DELETE_ATTR", "x")

    def test_uninit_slot(self):
        codestr = """
        class C:
            x: object

        def f(a: C):
            return a.x
        """
        code = self.compile(codestr, StaticCodeGenerator, modname="foo")
        code = self.find_code(code, name="f")
        with self.in_module(codestr) as mod:
            with self.assertRaises(AttributeError) as e:
                f, C = mod["f"], mod["C"]
                f(C())

        self.assertEqual(e.exception.args[0], "x")

    def test_aug_add(self):
        codestr = """
        class C:
            def __init__(self):
                self.x = 1

        def f(a: C):
            a.x += 1
        """
        code = self.compile(codestr, StaticCodeGenerator, modname="foo")
        code = self.find_code(code, name="f")
        self.assertInBytecode(code, "LOAD_FIELD", ("foo", "C", "x"))
        self.assertInBytecode(code, "STORE_FIELD", ("foo", "C", "x"))

    def test_untyped_attr(self):
        codestr = """
        y = x.load
        x.store = 42
        del x.delete
        """
        code = self.compile(codestr, StaticCodeGenerator, modname="foo")
        self.assertInBytecode(code, "LOAD_ATTR", "load")
        self.assertInBytecode(code, "STORE_ATTR", "store")
        self.assertInBytecode(code, "DELETE_ATTR", "delete")

    def test_incompat_override(self):
        codestr = """
        class C:
            x: int

        class D(C):
            def x(self): pass
        """
        with self.assertRaises(TypedSyntaxError):
            self.compile(codestr, StaticCodeGenerator, modname="foo")

    def test_redefine_type(self):
        codestr = """
            class C: pass
            class D: pass

            def f(a):
                x: C = C()
                x: D = D()
        """
        with self.assertRaises(TypedSyntaxError):
            self.compile(codestr, StaticCodeGenerator, modname="foo")

    def test_optional_error(self):
        codestr = """
            from typing import Optional
            class C:
                x: Optional["C"]
                def __init__(self, set):
                    if set:
                        self.x = self
                    else:
                        self.x = None

                def f(self) -> Optional["C"]:
                    return self.x.x
        """
        with self.assertRaisesRegex(
            TypedSyntaxError,
            re.escape("Optional[foo.C]: 'NoneType' object has no attribute 'x'"),
        ):
            self.compile(codestr, StaticCodeGenerator, modname="foo")

    def test_optional_subscript_error(self) -> None:
        codestr = """
            from typing import Optional

            def f(a: Optional[int]):
                a[1]
        """
        with self.assertRaisesRegex(
            TypedSyntaxError,
            re.escape("Optional[int]: 'NoneType' object is not subscriptable"),
        ):
            self.compile(codestr, StaticCodeGenerator)

    def test_optional_unary_error(self) -> None:
        codestr = """
            from typing import Optional

            def f(a: Optional[int]):
                -a
        """
        with self.assertRaisesRegex(
            TypedSyntaxError,
            re.escape("Optional[int]: bad operand type for unary -: 'NoneType'"),
        ):
            self.compile(codestr, StaticCodeGenerator)

    def test_optional_assign(self):
        codestr = """
            from typing import Optional
            class C:
                def f(self, x: Optional["C"]):
                    if x is None:
                        return self
                    else:
                        p: Optional["C"] = x
        """
        self.compile(codestr, StaticCodeGenerator, modname="foo")

    def test_nonoptional_load(self):
        codestr = """
            class C:
                def __init__(self, y: int):
                    self.y = y

            def f(c: C) -> int:
                return c.y
        """
        code = self.compile(codestr, StaticCodeGenerator, modname="foo")
        f = self.find_code(code, "f")
        self.assertInBytecode(f, "LOAD_FIELD", ("foo", "C", "y"))

    def test_optional_assign_subclass(self):
        codestr = """
            from typing import Optional
            class B: pass
            class D(B): pass

            def f(x: D):
                a: Optional[B] = x
        """
        self.compile(codestr, StaticCodeGenerator, modname="foo")

    def test_optional_assign_subclass_opt(self):
        codestr = """
            from typing import Optional
            class B: pass
            class D(B): pass

            def f(x: Optional[D]):
                a: Optional[B] = x
        """
        self.compile(codestr, StaticCodeGenerator, modname="foo")

    def test_optional_assign_none(self):
        codestr = """
            from typing import Optional
            class B: pass

            def f(x: Optional[B]):
                a: Optional[B] = None
        """
        self.compile(codestr, StaticCodeGenerator, modname="foo")

    def test_optional_union_syntax(self):
        self.assertReturns(
            """
            from typing import Union
            class B: pass
            class C(B): pass

            def f(x: Union[int, None]) -> int:
                # can assign None
                y: Optional[int] = None
                # can assign subclass
                z: Optional[B] = C()
                # can narrow
                if x is None:
                    return 1
                return x
            """,
            "int",
        )

    def test_optional_union_syntax_error(self):
        self.type_error(
            """
            from typing import Union

            def f(x: Union[int, None]) -> int:
                return x
            """,
            type_mismatch("Optional[int]", "int"),
        )

    def test_union_can_assign_to_broader_union(self):
        self.assertReturns(
            """
            from typing import Union
            class B:
                pass

            def f(x: Union[int, str]) -> Union[int, str, B]:
                return x
            """,
            "Union[int, str]",
        )

    def test_union_can_assign_to_same_union(self):
        self.assertReturns(
            """
            from typing import Union

            def f(x: Union[int, str]) -> Union[int, str]:
                return x
            """,
            "Union[int, str]",
        )

    def test_union_can_assign_from_individual_element(self):
        self.assertReturns(
            """
            from typing import Union

            def f(x: int) -> Union[int, str]:
                return x
            """,
            "int",
        )

    def test_union_cannot_assign_from_broader_union(self):
        self.type_error(
            """
            from typing import Union
            class B: pass

            def f(x: Union[int, str, B]) -> Union[int, str]:
                return x
            """,
            type_mismatch(
                "Union[int, str, <module>.B]",
                "Union[int, str]",
            ),
        )

    def test_union_simplify_to_single_type(self):
        self.assertReturns(
            """
            from typing import Union

            def f(x: Union[int]) -> int:
                return x
            """,
            "int",
        )

    def test_union_simplify_related(self):
        self.assertReturns(
            """
            from typing import Union
            class B: pass
            class C(B): pass

            def f(x: Union[B, C]) -> B:
                return x
            """,
            "foo.B",
        )

    def test_union_simplify_identical(self):
        self.assertReturns(
            """
            from typing import Union

            def f(x: Union[int, int]) -> int:
                return x
            """,
            "int",
        )

    def test_union_flatten_nested(self):
        self.assertReturns(
            """
            from typing import Union
            class B: pass

            def f(x: Union[int, Union[str, B]]):
                return x
            """,
            "Union[int, str, foo.B]",
        )

    def test_union_deep_simplify(self):
        self.assertReturns(
            """
            from typing import Union

            def f(x: Union[Union[int, int], Union[Union[None, None], Union[int, int]]]) -> int:
                if x is None:
                    return 1
                return x
            """,
            "int",
        )

    def test_union_dynamic_element(self):
        self.assertReturns(
            """
            from somewhere import unknown

            def f(x: Union[int, unknown]):
                return x
            """,
            "dynamic",
        )

    def test_union_or_syntax(self):
        self.type_error(
            """
            def f(x) -> int:
                if isinstance(x, int|str):
                    return x
                return 1
            """,
            type_mismatch("Union[int, str]", "int"),
        )

    def test_union_or_syntax_none(self):
        self.type_error(
            """
            def f(x) -> int:
                if isinstance(x, int|None):
                    return x
                return 1
            """,
            type_mismatch("Optional[int]", "int"),
        )

    def test_union_or_syntax_builtin_type(self):
        self.compile(
            """
            from typing import Iterator
            def f(x) -> int:
                if isinstance(x, bytes | Iterator[bytes]):
                    return 1
                return 2
            """,
            StaticCodeGenerator,
            modname="foo.py",
        )

    def test_union_or_syntax_none_first(self):
        self.type_error(
            """
            def f(x) -> int:
                if isinstance(x, None|int):
                    return x
                return 1
            """,
            type_mismatch("Optional[int]", "int"),
        )

    def test_union_or_syntax_annotation(self):
        self.type_error(
            """
            def f(x: int|str) -> int:
                return x
            """,
            type_mismatch("Union[int, str]", "int"),
        )

    def test_union_or_syntax_error(self):
        self.type_error(
            """
            def f():
                x = int | "foo"
            """,
            r"unsupported operand type(s) for |: Type\[Exact\[int\]\] and Exact\[str\]",
        )

    def test_union_or_syntax_annotation_bad_type(self):
        # TODO given that len is not unknown/dynamic, but is a known object
        # with type that is invalid in this position, this should really be an
        # error. But the current form of `resolve_annotations` doesn't let us
        # distinguish between unknown/dynamic and bad type. So for now we just
        # let this go as dynamic.
        self.assertReturns(
            """
            def f(x: len | int) -> int:
                return x
            """,
            "dynamic",
        )

    def test_union_attr(self):
        self.assertReturns(
            """
            class A:
                attr: int

            class B:
                attr: str

            def f(x: A | B):
                return x.attr
            """,
            "Union[int, str]",
        )

    def test_union_attr_error(self):
        self.type_error(
            """
            class A:
                attr: int

            def f(x: A | None):
                return x.attr
            """,
            re.escape(
                "Optional[<module>.A]: 'NoneType' object has no attribute 'attr'"
            ),
        )

    # TODO add test_union_call when we have Type[] or Callable[] or
    # __call__ support. Right now we have no way to construct a Union of
    # callables that return different types.

    def test_union_call_error(self):
        self.type_error(
            """
            def f(x: int | None):
                return x()
            """,
            re.escape("Optional[int]: 'NoneType' object is not callable"),
        )

    def test_union_subscr(self):
        self.assertReturns(
            """
            from __static__ import CheckedDict

            def f(x: CheckedDict[int, int] | CheckedDict[int, str]):
                return x[0]
            """,
            "Union[int, str]",
        )

    def test_union_unaryop(self):
        self.assertReturns(
            """
            def f(x: int | complex):
                return -x
            """,
            "Union[int, complex]",
        )

    def test_union_isinstance_reverse_narrow(self):
        self.assertReturns(
            """
            def f(x: int | str):
                if isinstance(x, str):
                    return 1
                return x
            """,
            "int",
        )

    def test_union_isinstance_reverse_narrow_supertype(self):
        self.assertReturns(
            """
            class A: pass
            class B(A): pass

            def f(x: int | B):
                if isinstance(x, A):
                    return 1
                return x
            """,
            "int",
        )

    def test_union_isinstance_reverse_narrow_other_union(self):
        self.assertReturns(
            """
            class A: pass
            class B: pass
            class C: pass

            def f(x: A | B | C):
                if isinstance(x, A | B):
                    return 1
                return x
            """,
            "foo.C",
        )

    def test_union_not_isinstance_narrow(self):
        self.assertReturns(
            """
            def f(x: int | str):
                if not isinstance(x, int):
                    return 1
                return x
            """,
            "int",
        )

    def test_union_isinstance_tuple(self):
        self.assertReturns(
            """
            class A: pass
            class B: pass
            class C: pass

            def f(x: A | B | C):
                if isinstance(x, (A, B)):
                    return 1
                return x
            """,
            "foo.C",
        )

    def test_error_return_int(self):
        with self.assertRaisesRegex(
            TypedSyntaxError, "type mismatch: int64 cannot be assigned to dynamic"
        ):
            code = self.compile(
                """
            from __static__ import ssize_t
            def f():
                y: ssize_t = 1
                return y
            """,
                StaticCodeGenerator,
            )

    def test_error_mixed_math(self):
        with self.assertRaises(TypedSyntaxError):
            code = self.compile(
                """
            from __static__ import ssize_t
            def f():
                y = 1
                x: ssize_t = 42 + y
            """,
                StaticCodeGenerator,
            )

    def test_error_incompat_return(self):
        with self.assertRaises(TypedSyntaxError):
            code = self.compile(
                """
            class D: pass
            class C:
                def __init__(self):
                    self.x = None

                def f(self) -> "C":
                    return D()
            """,
                StaticCodeGenerator,
            )

    def test_cast(self):
        for code_gen in (StaticCodeGenerator, PythonCodeGenerator):
            codestr = """
                from __static__ import cast
                class C:
                    pass

                a = C()

                def f() -> C:
                    return cast(C, a)
            """
            code = self.compile(codestr, code_gen)
            f = self.find_code(code, "f")
            if code_gen is StaticCodeGenerator:
                self.assertInBytecode(f, "CAST", ("<module>", "C"))
            with self.in_module(codestr, code_gen=code_gen) as mod:
                C = mod["C"]
                f = mod["f"]
                self.assertTrue(isinstance(f(), C))
                self.assert_jitted(f)

    def test_cast_fail(self):
        for code_gen in (StaticCodeGenerator, PythonCodeGenerator):
            codestr = """
                from __static__ import cast
                class C:
                    pass

                def f() -> C:
                    return cast(C, 42)
            """
            code = self.compile(codestr, code_gen)
            f = self.find_code(code, "f")
            if code_gen is StaticCodeGenerator:
                self.assertInBytecode(f, "CAST", ("<module>", "C"))
            with self.in_module(codestr) as mod:
                with self.assertRaises(TypeError):
                    f = mod["f"]
                    f()
                    self.assert_jitted(f)

    def test_cast_wrong_args(self):
        codestr = """
            from __static__ import cast
            def f():
                cast(42)
        """
        with self.assertRaises(TypedSyntaxError):
            self.compile(codestr, StaticCodeGenerator)

    def test_cast_unknown_type(self):
        codestr = """
            from __static__ import cast
            def f():
                cast(abc, 42)
        """
        with self.assertRaises(TypedSyntaxError):
            self.compile(codestr, StaticCodeGenerator)

    def test_cast_optional(self):
        for code_gen in (StaticCodeGenerator, PythonCodeGenerator):
            codestr = """
                from __static__ import cast
                from typing import Optional

                class C:
                    pass

                def f(x) -> Optional[C]:
                    return cast(Optional[C], x)
            """
            code = self.compile(codestr, code_gen)
            f = self.find_code(code, "f")
            if code_gen is StaticCodeGenerator:
                self.assertInBytecode(f, "CAST", ("<module>", "C", "?"))
            with self.in_module(codestr, code_gen=code_gen) as mod:
                C = mod["C"]
                f = mod["f"]
                self.assertTrue(isinstance(f(C()), C))
                self.assertEqual(f(None), None)
                self.assert_jitted(f)

    def test_code_flags(self):
        codestr = """
        def func():
            print("hi")

        func()
        """
        module = self.compile(codestr, StaticCodeGenerator)
        self.assertTrue(module.co_flags & CO_STATICALLY_COMPILED)
        self.assertTrue(
            self.find_code(module, name="func").co_flags & CO_STATICALLY_COMPILED
        )

    def test_invoke_kws(self):
        codestr = """
        class C:
            def f(self, a):
                return a

        def func():
            a = C()
            return a.f(a=2)

        """
        with self.in_module(codestr, code_gen=StaticCodeGenerator) as mod:
            f = mod["func"]
            self.assertEqual(f(), 2)

    def test_invoke_str_method(self):
        codestr = """
        def func():
            a = 'a b c'
            return a.split()

        """
        with self.in_module(codestr, code_gen=StaticCodeGenerator) as mod:
            f = mod["func"]
            self.assertInBytecode(
                f, "INVOKE_FUNCTION", (("builtins", "str", "split"), 1)
            )
            self.assertEqual(f(), ["a", "b", "c"])

    def test_invoke_str_method_arg(self):
        codestr = """
        def func():
            a = 'a b c'
            return a.split('a')

        """
        with self.in_module(codestr, code_gen=StaticCodeGenerator) as mod:
            f = mod["func"]
            self.assertInBytecode(
                f, "INVOKE_FUNCTION", (("builtins", "str", "split"), 2)
            )
            self.assertEqual(f(), ["", " b c"])

    def test_invoke_str_method_kwarg(self):
        codestr = """
        def func():
            a = 'a b c'
            return a.split(sep='a')

        """
        with self.in_module(codestr, code_gen=StaticCodeGenerator) as mod:
            f = mod["func"]
            self.assertNotInBytecode(f, "INVOKE_FUNCTION")
            self.assertNotInBytecode(f, "INVOKE_METHOD")
            self.assertEqual(f(), ["", " b c"])

    def test_invoke_int_method(self):
        codestr = """
        def func():
            a = 42
            return a.bit_length()

        """
        with self.in_module(codestr, code_gen=StaticCodeGenerator) as mod:
            f = mod["func"]
            self.assertInBytecode(
                f, "INVOKE_FUNCTION", (("builtins", "int", "bit_length"), 1)
            )
            self.assertEqual(f(), 6)

    def test_invoke_chkdict_method(self):
        codestr = """
        from __static__ import CheckedDict
        def dict_maker() -> CheckedDict[int, int]:
            return CheckedDict[int, int]({2:2})
        def func():
            a = dict_maker()
            return a.keys()

        """
        with self.in_module(codestr, code_gen=StaticCodeGenerator) as mod:
            f = mod["func"]

            self.assertInBytecode(
                f,
                "INVOKE_METHOD",
                (
                    (
                        "__static__",
                        "chkdict",
                        (("builtins", "int"), ("builtins", "int")),
                        "keys",
                    ),
                    0,
                ),
            )
            self.assertEqual(list(f()), [2])
            self.assert_jitted(f)

    def test_invoke_method_non_static_base(self):
        codestr = """
        class C(Exception):
            def f(self):
                return 42

            def g(self):
                return self.f()
        """

        with self.in_module(codestr) as mod:
            C = mod["C"]
            self.assertEqual(C().g(), 42)

    def test_invoke_builtin_func(self):
        codestr = """
        from xxclassloader import foo
        from __static__ import int64, box

        def func():
            a: int64 = foo()
            return box(a)

        """
        with self.in_module(codestr, code_gen=StaticCodeGenerator) as mod:
            f = mod["func"]
            self.assertInBytecode(f, "INVOKE_FUNCTION", ((("xxclassloader", "foo"), 0)))
            self.assertEqual(f(), 42)
            self.assert_jitted(f)

    def test_invoke_builtin_func_ret_neg(self):
        # setup xxclassloader as a built-in function for this test, so we can
        # do a direct invoke
        xxclassloader = sys.modules["xxclassloader"]
        try:
            sys.modules["xxclassloader"] = StrictModule(xxclassloader.__dict__, False)
            codestr = """
            from xxclassloader import neg
            from __static__ import int64, box

            def test():
                x: int64 = neg()
                return box(x)
            """
            with self.in_module(codestr, code_gen=StaticCodeGenerator) as mod:
                test = mod["test"]
                self.assertEqual(test(), -1)
        finally:
            sys.modules["xxclassloader"] = xxclassloader

    def test_invoke_builtin_func_arg(self):
        codestr = """
        from xxclassloader import bar
        from __static__ import int64, box

        def func():
            a: int64 = bar(42)
            return box(a)

        """
        with self.in_module(codestr, code_gen=StaticCodeGenerator) as mod:
            f = mod["func"]
            self.assertInBytecode(f, "INVOKE_FUNCTION", ((("xxclassloader", "bar"), 1)))
            self.assertEqual(f(), 42)
            self.assert_jitted(f)

    def test_invoke_meth_o(self):
        codestr = """
        from xxclassloader import spamobj

        def func():
            a = spamobj[int]()
            a.setstate_untyped(42)
            return a.getstate()

        """
        with self.in_module(codestr, code_gen=StaticCodeGenerator) as mod:
            f = mod["func"]

            self.assertInBytecode(
                f,
                "INVOKE_METHOD",
                (
                    (
                        "xxclassloader",
                        "spamobj",
                        (("builtins", "int"),),
                        "setstate_untyped",
                    ),
                    1,
                ),
            )
            self.assertEqual(f(), 42)
            self.assert_jitted(f)

    def test_multi_generic(self):
        codestr = """
        from xxclassloader import XXGeneric

        def func():
            a = XXGeneric[int, str]()
            return a.foo(42, 'abc')
        """
        with self.in_module(codestr, code_gen=StaticCodeGenerator) as mod:
            f = mod["func"]
            self.assertEqual(f(), "42abc")

    def test_verify_positional_args(self):
        codestr = """
            def x(a: int, b: str) -> None:
                pass
            x("a", 2)
        """
        with self.assertRaisesRegex(TypedSyntaxError, "argument type mismatch"):
            self.compile(codestr, StaticCodeGenerator)

    def test_verify_positional_args_unordered(self):
        codestr = """
            def x(a: int, b: str) -> None:
                return y(a, b)
            def y(a: int, b: str) -> None:
                pass
        """
        self.compile(codestr, StaticCodeGenerator)

    def test_verify_kwargs(self):
        codestr = """
            def x(a: int=1, b: str="hunter2") -> None:
                return
            x(b="lol", a=23)
        """
        self.compile(codestr, StaticCodeGenerator)

    def test_verify_kwdefaults(self):
        codestr = """
            def x(*, b: str="hunter2"):
                return b
            z = x(b="lol")
        """
        with self.in_module(codestr, code_gen=StaticCodeGenerator) as mod:
            self.assertEqual(mod["z"], "lol")

    def test_verify_kwdefaults_no_value(self):
        codestr = """
            def x(*, b: str="hunter2"):
                return b
            a = x()
        """
        module = self.compile(codestr, StaticCodeGenerator)
        # we don't yet support optimized dispatch to kw-only functions
        self.assertInBytecode(module, "CALL_FUNCTION")
        with self.in_module(codestr, code_gen=StaticCodeGenerator) as mod:
            self.assertEqual(mod["a"], "hunter2")

    def test_verify_arg_dynamic_type(self):
        codestr = """
            def x(v:str):
                return 'abc'
            def y(v):
                return x(v)
        """
        module = self.compile(codestr, StaticCodeGenerator)
        with self.in_module(codestr, code_gen=StaticCodeGenerator) as mod:
            y = mod["y"]
            with self.assertRaises(TypeError):
                y(42)
            self.assertEqual(y("foo"), "abc")

    def test_verify_arg_unknown_type(self):
        codestr = """
            def x(x:foo):
                return b
            x('abc')
        """
        module = self.compile(codestr, StaticCodeGenerator)
        self.assertInBytecode(module, "INVOKE_FUNCTION")
        x = self.find_code(module)
        self.assertInBytecode(x, "CHECK_ARGS", ())

    def test_dict_invoke(self):
        codestr = """
            from __static__ import pydict
            def f(x):
                y: pydict = x
                return y.get('foo')
        """
        with self.in_module(codestr) as mod:
            f = mod["f"]
            self.assertInBytecode(f, "INVOKE_METHOD", (("builtins", "dict", "get"), 1))
            self.assertEqual(f({}), None)

    def test_dict_invoke_ret(self):
        codestr = """
            from __static__ import pydict
            def g(): return None
            def f(x):
                y: pydict = x
                z = y.get('foo')
                z = None  # should be typed to dynamic
                return z
        """
        with self.in_module(codestr) as mod:
            f = mod["f"]
            self.assertInBytecode(f, "INVOKE_METHOD", (("builtins", "dict", "get"), 1))
            self.assertEqual(f({}), None)

    def test_verify_kwarg_unknown_type(self):
        codestr = """
            def x(x:foo):
                return b
            x(x='abc')
        """
        module = self.compile(codestr, StaticCodeGenerator)
        self.assertInBytecode(module, "INVOKE_FUNCTION")
        x = self.find_code(module)
        self.assertInBytecode(x, "CHECK_ARGS", ())

    def test_verify_kwdefaults_too_many(self):
        codestr = """
            def x(*, b: str="hunter2") -> None:
                return
            x('abc')
        """
        with self.assertRaisesRegex(
            TypedSyntaxError, "x takes 0 positional args but 1 was given"
        ):
            self.compile(codestr, StaticCodeGenerator)

    def test_verify_kwdefaults_too_many_class(self):
        codestr = """
            class C:
                def x(self, *, b: str="hunter2") -> None:
                    return
            C().x('abc')
        """
        with self.assertRaisesRegex(
            TypedSyntaxError, "x takes 1 positional args but 2 were given"
        ):
            self.compile(codestr, StaticCodeGenerator)

    def test_verify_kwonly_failure(self):
        codestr = """
            def x(*, a: int=1, b: str="hunter2") -> None:
                return
            x(a="hi", b="lol")
        """
        with self.assertRaisesRegex(TypedSyntaxError, "keyword argument type mismatch"):
            self.compile(codestr, StaticCodeGenerator)

    def test_verify_kwonly_self_loaded_once(self):
        codestr = """
            class C:
                def x(self, *, a: int=1) -> int:
                    return 43

            def f():
                return C().x(a=1)
        """
        with self.in_module(codestr, code_gen=StaticCodeGenerator) as mod:
            f = mod["f"]
            io = StringIO()
            dis.dis(f, file=io)
            self.assertEqual(1, io.getvalue().count("LOAD_GLOBAL"))

    def test_call_function_unknown_ret_type(self):
        codestr = """
            from __future__ import annotations
            def g() -> foo:
                return 42

            def testfunc():
                return g()
        """
        with self.in_module(codestr, code_gen=StaticCodeGenerator) as mod:
            f = mod["testfunc"]
            self.assertEqual(f(), 42)

    def test_verify_kwargs_failure(self):
        codestr = """
            def x(a: int=1, b: str="hunter2") -> None:
                return
            x(a="hi", b="lol")
        """
        with self.assertRaisesRegex(TypedSyntaxError, "keyword argument type mismatch"):
            self.compile(codestr, StaticCodeGenerator)

    def test_verify_mixed_args(self):
        codestr = """
            def x(a: int=1, b: str="hunter2", c: int=14) -> None:
                return
            x(12, c=56, b="lol")
        """
        self.compile(codestr, StaticCodeGenerator)

    def test_kwarg_cast(self):
        codestr = """
            def x(a: int=1, b: str="hunter2", c: int=14) -> None:
                return

            def g(a):
                x(b=a)
        """
        code = self.find_code(self.compile(codestr, StaticCodeGenerator), "g")
        self.assertInBytecode(code, "CAST", ("builtins", "str"))

    def test_kwarg_nocast(self):
        codestr = """
            def x(a: int=1, b: str="hunter2", c: int=14) -> None:
                return

            def g():
                x(b='abc')
        """
        code = self.find_code(self.compile(codestr, StaticCodeGenerator), "g")
        self.assertNotInBytecode(code, "CAST", ("builtins", "str"))

    def test_verify_mixed_args_kw_failure(self):
        codestr = """
            def x(a: int=1, b: str="hunter2", c: int=14) -> None:
                return
            x(12, c="hi", b="lol")
        """
        with self.assertRaisesRegex(TypedSyntaxError, "keyword argument type mismatch"):
            self.compile(codestr, StaticCodeGenerator)

    def test_verify_mixed_args_positional_failure(self):
        codestr = """
            def x(a: int=1, b: str="hunter2", c: int=14) -> None:
                return
            x("hi", b="lol")
        """
        with self.assertRaisesRegex(
            TypedSyntaxError, "positional argument type mismatch"
        ):
            self.compile(codestr, StaticCodeGenerator)

    # Same tests as above, but for methods.
    def test_verify_positional_args_method(self):
        codestr = """
            class C:
                def x(self, a: int, b: str) -> None:
                    pass
            C().x(2, "hi")
        """
        self.compile(codestr, StaticCodeGenerator)

    def test_verify_positional_args_failure_method(self):
        codestr = """
            class C:
                def x(self, a: int, b: str) -> None:
                    pass
            C().x("a", 2)
        """
        with self.assertRaisesRegex(
            TypedSyntaxError, "positional argument type mismatch"
        ):
            self.compile(codestr, StaticCodeGenerator)

    def test_verify_mixed_args_method(self):
        codestr = """
            class C:
                def x(self, a: int=1, b: str="hunter2", c: int=14) -> None:
                    return
            C().x(12, c=56, b="lol")
        """
        self.compile(codestr, StaticCodeGenerator)

    def test_starargs_invoked_once(self):
        codestr = """
            X = 0

            def f():
                global X
                X += 1
                return {"a": 1, "b": "foo", "c": 42}

            class C:
                def x(self, a: int=1, b: str="hunter2", c: int=14) -> None:
                    return
            C().x(12, **f())
        """
        with self.in_module(codestr) as mod:
            x = mod["X"]
            self.assertEqual(x, 1)

    def test_starargs_invoked_in_order(self):
        codestr = """
            X = 1

            def f():
                global X
                X += 1
                return {"a": 1, "b": "foo"}

            def make_c():
                global X
                X *= 2
                return 42

            class C:
                def x(self, a: int=1, b: str="hunter2", c: int=14) -> None:
                    return

            def test():
                C().x(12, c=make_c(), **f())
        """
        with self.in_module(codestr) as mod:
            test = mod["test"]
            test()
            x = mod["X"]
            self.assertEqual(x, 3)

    def test_verify_mixed_args_kw_failure_method(self):
        codestr = """
            class C:
                def x(self, a: int=1, b: str="hunter2", c: int=14) -> None:
                    return
            C().x(12, c=b'lol', b="lol")
        """
        with self.assertRaisesRegex(TypedSyntaxError, "keyword argument type mismatch"):
            self.compile(codestr, StaticCodeGenerator)

    def test_verify_mixed_args_positional_failure_method(self):
        codestr = """
            class C:
                def x(self, a: int=1, b: str="hunter2", c: int=14) -> None:
                    return
            C().x("hi", b="lol")
        """
        with self.assertRaisesRegex(
            TypedSyntaxError, "positional argument type mismatch"
        ):
            self.compile(codestr, StaticCodeGenerator)

    def test_generic_kwargs_unsupported(self):
        # definition is allowed, we just don't do an optimal invoke
        codestr = """
        def f(a: int, b: str, **my_stuff) -> None:
            pass

        def g():
            return f(1, 'abc', x="y")
        """
        with self.in_module(codestr) as mod:
            g = mod["g"]
            self.assertInBytecode(g, "CALL_FUNCTION_KW", 3)

    def test_generic_kwargs_method_unsupported(self):
        # definition is allowed, we just don't do an optimal invoke
        codestr = """
        class C:
            def f(self, a: int, b: str, **my_stuff) -> None:
                pass

        def g():
            return C().f(1, 'abc', x="y")
        """
        with self.in_module(codestr) as mod:
            g = mod["g"]
            self.assertInBytecode(g, "CALL_FUNCTION_KW", 3)

    def test_generic_varargs_unsupported(self):
        # definition is allowed, we just don't do an optimal invoke
        codestr = """
        def f(a: int, b: str, *my_stuff) -> None:
            pass

        def g():
            return f(1, 'abc', "foo")
        """
        with self.in_module(codestr) as mod:
            g = mod["g"]
            self.assertInBytecode(g, "CALL_FUNCTION", 3)

    def test_generic_varargs_method_unsupported(self):
        # definition is allowed, we just don't do an optimal invoke
        codestr = """
        class C:
            def f(self, a: int, b: str, *my_stuff) -> None:
                pass

        def g():
            return C().f(1, 'abc', "foo")
        """
        with self.in_module(codestr) as mod:
            g = mod["g"]
            self.assertInBytecode(g, "CALL_METHOD", 3)

    def test_kwargs_get(self):
        codestr = """
            def test(**foo):
                print(foo.get('bar'))
        """

        with self.in_module(codestr, code_gen=StaticCodeGenerator) as mod:
            test = mod["test"]
            self.assertInBytecode(
                test, "INVOKE_FUNCTION", (("builtins", "dict", "get"), 2)
            )

    def test_varargs_count(self):
        codestr = """
            def test(*foo):
                print(foo.count('bar'))
        """

        with self.in_module(codestr, code_gen=StaticCodeGenerator) as mod:
            test = mod["test"]
            self.assertInBytecode(
                test, "INVOKE_FUNCTION", (("builtins", "tuple", "count"), 2)
            )

    def test_varargs_call(self):
        codestr = """
            def g(*foo):
                return foo

            def testfunc():
                return g(2)
        """

        with self.in_module(codestr, code_gen=StaticCodeGenerator) as mod:
            test = mod["testfunc"]
            self.assertEqual(test(), (2,))

    def test_kwargs_call(self):
        codestr = """
            def g(**foo):
                return foo

            def testfunc():
                return g(x=2)
        """

        with self.in_module(codestr, code_gen=StaticCodeGenerator) as mod:
            test = mod["testfunc"]
            self.assertEqual(test(), {"x": 2})

    def test_index_by_int(self):
        codestr = """
            from __static__ import int32
            def f(x):
                i: int32 = 0
                return x[i]
        """
        with self.assertRaises(TypedSyntaxError):
            self.compile(codestr, StaticCodeGenerator)

    def test_pydict_arg_annotation(self):
        codestr = """
            from __static__ import PyDict
            def f(d: PyDict[str, int]) -> str:
                # static python ignores the untrusted generic types
                return d[3]
        """
        with self.in_module(codestr) as mod:
            self.assertEqual(mod["f"]({3: "foo"}), "foo")

    def test_list_get_primitive_int(self):
        codestr = """
            from __static__ import int8
            def f():
                l = [1, 2, 3]
                x: int8 = 1
                return l[x]
        """
        f = self.find_code(self.compile(codestr))
        self.assertInBytecode(f, "SEQUENCE_GET", SEQ_LIST)
        with self.in_module(codestr) as mod:
            self.assertEqual(mod["f"](), 2)

    def test_list_set_primitive_int(self):
        codestr = """
            from __static__ import int8
            def f():
                l = [1, 2, 3]
                x: int8 = 1
                l[x] = 5
                return l
        """
        f = self.find_code(self.compile(codestr))
        self.assertInBytecode(f, "SEQUENCE_SET")
        with self.in_module(codestr) as mod:
            self.assertEqual(mod["f"](), [1, 5, 3])

    def test_list_set_primitive_int_2(self):
        codestr = """
            from __static__ import int64
            def f(l1):
                l2 = [None] * len(l1)
                i: int64 = 0
                for item in l1:
                    l2[i] = item + 1
                    i += 1
                return l2
        """
        f = self.find_code(self.compile(codestr))
        self.assertInBytecode(f, "SEQUENCE_SET")
        with self.in_module(codestr) as mod:
            self.assertEqual(mod["f"]([1, 2000]), [2, 2001])

    def test_list_del_primitive_int(self):
        codestr = """
            from __static__ import int8
            def f():
                l = [1, 2, 3]
                x: int8 = 1
                del l[x]
                return l
        """
        f = self.find_code(self.compile(codestr))
        self.assertInBytecode(f, "LIST_DEL")
        with self.in_module(codestr) as mod:
            self.assertEqual(mod["f"](), [1, 3])

    def test_list_append(self):
        codestr = """
            from __static__ import int8
            def f():
                l = [1, 2, 3]
                l.append(4)
                return l
        """
        f = self.find_code(self.compile(codestr))
        self.assertInBytecode(f, "LIST_APPEND", 1)
        with self.in_module(codestr) as mod:
            self.assertEqual(mod["f"](), [1, 2, 3, 4])

    def test_unknown_type_unary(self):
        codestr = """
            def x(y):
                z = -y
        """
        f = self.find_code(self.compile(codestr, StaticCodeGenerator, modname="foo"))
        self.assertInBytecode(f, "UNARY_NEGATIVE")

    def test_unknown_type_binary(self):
        codestr = """
            def x(a, b):
                z = a + b
        """
        f = self.find_code(self.compile(codestr, StaticCodeGenerator, modname="foo"))
        self.assertInBytecode(f, "BINARY_ADD")

    def test_unknown_type_compare(self):
        codestr = """
            def x(a, b):
                z = a > b
        """
        f = self.find_code(self.compile(codestr, StaticCodeGenerator, modname="foo"))
        self.assertInBytecode(f, "COMPARE_OP")

    def test_async_func_ret_type(self):
        codestr = """
            async def x(a) -> int:
                return a
        """
        f = self.find_code(self.compile(codestr, StaticCodeGenerator, modname="foo"))
        self.assertInBytecode(f, "CAST")

    def test_async_func_arg_types(self):
        codestr = """
            async def f(x: int):
                pass
        """
        f = self.find_code(self.compile(codestr))
        self.assertInBytecode(f, "CHECK_ARGS", (0, ("builtins", "int")))

    def test_assign_prim_to_class(self):
        codestr = """
            from __static__ import int64
            class C: pass

            def f():
                x: C = C()
                y: int64 = 42
                x = y
        """
        with self.assertRaisesRegex(TypedSyntaxError, type_mismatch("int64", "foo.C")):
            self.compile(codestr, StaticCodeGenerator, modname="foo")

    def test_field_refcount(self):
        codestr = """
            class C:
                def __init__(self):
                    self.x = None

                def set_x(self, x):
                    self.x = x
        """
        count = 0
        with self.in_module(codestr) as mod:
            C = mod["C"]

            class X:
                def __init__(self):
                    nonlocal count
                    count += 1

                def __del__(self):
                    nonlocal count
                    count -= 1

            c = C()
            c.set_x(X())
            c.set_x(X())
            self.assertEqual(count, 1)
            del c
            self.assertEqual(count, 0)

    def test_typed_field_del(self):
        codestr = """
            class D:
                def __init__(self, counter):
                    self.counter = counter
                    self.counter[0] += 1

                def __del__(self):
                    self.counter[0] -= 1

            class C:
                def __init__(self, value: D):
                    self.x: D = value

                def __del__(self):
                    del self.x
        """
        count = 0
        with self.in_module(codestr) as mod:
            D = mod["D"]
            counter = [0]
            d = D(counter)

            C = mod["C"]
            a = C(d)
            del d
            self.assertEqual(counter[0], 1)
            del a
            self.assertEqual(counter[0], 0)

    def test_typed_field_deleted_attr(self):
        codestr = """
            class C:
                def __init__(self, value: str):
                    self.x: str = value
        """
        count = 0
        with self.in_module(codestr) as mod:
            C = mod["C"]
            a = C("abc")
            del a.x
            with self.assertRaises(AttributeError):
                a.x

    def test_generic_method_ret_type(self):
        codestr = """
            from __static__ import CheckedDict

            from typing import Optional
            MAP: CheckedDict[str, Optional[str]] = CheckedDict[str, Optional[str]]({'abc': 'foo', 'bar': None})
            def f(x: str) -> Optional[str]:
                return MAP.get(x)
        """

        with self.in_module(codestr) as mod:
            f = mod["f"]
            self.assertInBytecode(
                f,
                "INVOKE_METHOD",
                (
                    (
                        "__static__",
                        "chkdict",
                        (("builtins", "str"), ("builtins", "str", "?")),
                        "get",
                    ),
                    2,
                ),
            )
            self.assertEqual(f("abc"), "foo")
            self.assertEqual(f("bar"), None)


    def test_attr_generic_optional(self):
        codestr = """
            from typing import Optional
            def f(x: Optional):
                return x.foo
        """

        with self.assertRaisesRegex(
            TypedSyntaxError, "cannot access attribute from unbound Union"
        ):
            self.compile(codestr, StaticCodeGenerator, modname="foo")

    def test_assign_generic_optional(self):
        codestr = """
            from typing import Optional
            def f():
                x: Optional = 42
        """

        with self.assertRaisesRegex(
            TypedSyntaxError, type_mismatch("Exact[int]", "Optional[T]")
        ):
            self.compile(codestr, StaticCodeGenerator, modname="foo")

    def test_assign_generic_optional_2(self):
        codestr = """
            from typing import Optional
            def f():
                x: Optional = 42 + 1
        """

        with self.assertRaises(TypedSyntaxError):
            self.compile(codestr, StaticCodeGenerator, modname="foo")

    def test_assign_from_generic_optional(self):
        codestr = """
            from typing import Optional
            class C: pass

            def f(x: Optional):
                y: Optional[C] = x
        """

        with self.assertRaisesRegex(
            TypedSyntaxError, type_mismatch("Optional[T]", optional("foo.C"))
        ):
            self.compile(codestr, StaticCodeGenerator, modname="foo")

    def test_list_of_dynamic(self):
        codestr = """
            from threading import Thread
            from typing import List

            def f(threads: List[Thread]) -> int:
                return len(threads)
        """
        f = self.find_code(self.compile(codestr), "f")
        self.assertInBytecode(f, "FAST_LEN")

    def test_int_swap(self):
        codestr = """
            from __static__ import int64, box

            def test():
                x: int64 = 42
                y: int64 = 100
                x, y = y, x
                return box(x), box(y)
        """

        with self.assertRaisesRegex(
            TypedSyntaxError, type_mismatch("int64", "dynamic")
        ):
            self.compile(codestr, StaticCodeGenerator, modname="foo")

    def test_typed_swap(self):
        codestr = """
            def test(a):
                x: int
                y: str
                x, y = 1, a
        """

        f = self.find_code(self.compile(codestr, StaticCodeGenerator, modname="foo"))
        self.assertInBytecode(f, "CAST", ("builtins", "str"))
        self.assertNotInBytecode(f, "CAST", ("builtins", "int"))

    def test_typed_swap_2(self):
        codestr = """
            def test(a):
                x: int
                y: str
                x, y = a, 'abc'

        """

        f = self.find_code(self.compile(codestr, StaticCodeGenerator, modname="foo"))
        self.assertInBytecode(f, "CAST", ("builtins", "int"))
        self.assertNotInBytecode(f, "CAST", ("builtins", "str"))

    def test_typed_swap_member(self):
        codestr = """
            class C:
                def __init__(self):
                    self.x: int = 42

            def test(a):
                x: int
                y: str
                C().x, y = a, 'abc'

        """

        f = self.find_code(
            self.compile(codestr, StaticCodeGenerator, modname="foo"), "test"
        )
        self.assertInBytecode(f, "CAST", ("builtins", "int"))
        self.assertNotInBytecode(f, "CAST", ("builtins", "str"))

    def test_typed_swap_list(self):
        codestr = """
            def test(a):
                x: int
                y: str
                [x, y] = a, 'abc'
        """

        f = self.find_code(self.compile(codestr, StaticCodeGenerator, modname="foo"))
        self.assertInBytecode(f, "CAST", ("builtins", "int"))
        self.assertNotInBytecode(f, "CAST", ("builtins", "str"))

    def test_typed_swap_nested(self):
        codestr = """
            def test(a):
                x: int
                y: str
                z: str
                ((x, y), z) = (a, 'abc'), 'foo'
        """

        f = self.find_code(self.compile(codestr, StaticCodeGenerator, modname="foo"))
        self.assertInBytecode(f, "CAST", ("builtins", "int"))
        self.assertNotInBytecode(f, "CAST", ("builtins", "str"))

    def test_typed_swap_nested_2(self):
        codestr = """
            def test(a):
                x: int
                y: str
                z: str
                ((x, y), z) = (1, a), 'foo'

        """

        f = self.find_code(self.compile(codestr, StaticCodeGenerator, modname="foo"))
        self.assertInBytecode(f, "CAST", ("builtins", "str"))
        self.assertNotInBytecode(f, "CAST", ("builtins", "int"))

    def test_typed_swap_nested_3(self):
        codestr = """
            def test(a):
                x: int
                y: int
                z: str
                ((x, y), z) = (1, 2), a

        """

        f = self.find_code(self.compile(codestr, StaticCodeGenerator, modname="foo"))
        self.assertInBytecode(f, "CAST", ("builtins", "str"))
        # Currently because the tuple gets turned into a constant this is less than
        # ideal:
        self.assertInBytecode(f, "CAST", ("builtins", "int"))

    def test_if_optional(self):
        codestr = """
            from typing import Optional
            class C:
                def __init__(self):
                    self.field = 42

            def f(x: Optional[C]):
                if x is not None:
                    return x.field

                return None
        """

        self.compile(codestr, StaticCodeGenerator, modname="foo")

    def test_return_outside_func(self):
        codestr = """
            return 42
        """
        with self.assertRaisesRegex(SyntaxError, "'return' outside function"):
            self.compile(codestr, StaticCodeGenerator, modname="foo")

    def test_double_decl(self):
        codestr = """
            def f():
                x: int
                x: str
        """
        with self.assertRaisesRegex(
            TypedSyntaxError, "Cannot redefine local variable x"
        ):
            self.compile(codestr, StaticCodeGenerator, modname="foo")

        codestr = """
            def f():
                x = 42
                x: str
        """
        with self.assertRaisesRegex(
            TypedSyntaxError, "Cannot redefine local variable x"
        ):
            self.compile(codestr, StaticCodeGenerator, modname="foo")

        codestr = """
            def f():
                x = 42
                x: int
        """
        with self.assertRaisesRegex(
            TypedSyntaxError, "Cannot redefine local variable x"
        ):
            self.compile(codestr, StaticCodeGenerator, modname="foo")

    def test_if_else_optional(self):
        codestr = """
            from typing import Optional
            class C:
                def __init__(self):
                    self.field = self

            def g(x: C):
                pass

            def f(x: Optional[C], y: Optional[C]):
                if x is None:
                    x = y
                    if x is None:
                        return None
                    else:
                        return g(x)
                else:
                    return g(x)


                return None
        """

        self.compile(codestr, StaticCodeGenerator, modname="foo")

    def test_if_else_optional_return(self):
        codestr = """
            from typing import Optional
            class C:
                def __init__(self):
                    self.field = self

            def f(x: Optional[C]):
                if x is None:
                    return 0
                return x.field
        """

        self.compile(codestr, StaticCodeGenerator, modname="foo")

    def test_if_else_optional_return_two_branches(self):
        codestr = """
            from typing import Optional
            class C:
                def __init__(self):
                    self.field = self

            def f(x: Optional[C]):
                if x is None:
                    if a:
                        return 0
                    else:
                        return 2
                return x.field
        """

        self.compile(codestr, StaticCodeGenerator, modname="foo")

    def test_if_else_optional_return_in_else(self):
        codestr = """
            from typing import Optional

            def f(x: Optional[int]) -> int:
                if x is not None:
                    pass
                else:
                    return 2
                return x
        """

        self.compile(codestr, StaticCodeGenerator, modname="foo")

    def test_if_else_optional_return_in_else_assignment_in_if(self):
        codestr = """
            from typing import Optional

            def f(x: Optional[int]) -> int:
                if x is None:
                    x = 1
                else:
                    return 2
                return x
        """

        self.compile(codestr, StaticCodeGenerator, modname="foo")

    def test_if_else_optional_return_in_if_assignment_in_else(self):
        codestr = """
            from typing import Optional

            def f(x: Optional[int]) -> int:
                if x is not None:
                    return 2
                else:
                    x = 1
                return x
        """

        self.compile(codestr, StaticCodeGenerator, modname="foo")

    def test_narrow_conditional(self):
        codestr = """
            class B:
                def f(self):
                    return 42
            class D(B):
                def f(self):
                    return 'abc'

            def testfunc(abc):
                x = B()
                if abc:
                    x = D()
                    return x.f()
        """

        code = self.compile(codestr, StaticCodeGenerator, modname="foo")
        f = self.find_code(code, "testfunc")
        self.assertInBytecode(f, "INVOKE_METHOD", (("foo", "D", "f"), 0))
        with self.in_module(codestr, code_gen=StaticCodeGenerator) as mod:
            test = mod["testfunc"]
            self.assertEqual(test(True), "abc")
            self.assertEqual(test(False), None)

    def test_no_narrow_to_dynamic(self):
        codestr = """
            def f():
                return 42

            def g():
                x: int = 100
                x = f()
                return x.bit_length()
        """

        with self.in_module(codestr, code_gen=StaticCodeGenerator) as mod:
            g = mod["g"]
            self.assertInBytecode(g, "CAST", ("builtins", "int"))
            self.assertInBytecode(
                g, "INVOKE_METHOD", (("builtins", "int", "bit_length"), 0)
            )
            self.assertEqual(g(), 6)

    def test_global_uses_decl_type(self):
        codestr = """
            # even though we can locally infer G must be None,
            # it's not Final so nested scopes can't assume it
            # remains None
            G: int | None = None

            def f() -> int:
                global G
                # if we use the local_type for G's type,
                # x would have a local type of None
                x: int | None = G
                if x is None:
                    x = G = 1
                return x
        """
        with self.in_strict_module(codestr) as mod:
            self.assertEqual(mod.f(), 1)

    def test_module_level_type_narrow(self):
        codestr = """
            def a() -> int | None:
                return 1

            G = a()
            if G is not None:
                G += 1

            def f() -> int:
                if G is None:
                    return 0
                reveal_type(G)
        """
        with self.assertRaisesRegex(TypedSyntaxError, r"Optional\[int\]"):
            self.compile(codestr)

    def test_narrow_conditional_widened(self):
        codestr = """
            class B:
                def f(self):
                    return 42
            class D(B):
                def f(self):
                    return 'abc'

            def testfunc(abc):
                x = B()
                if abc:
                    x = D()
                return x.f()
        """

        code = self.compile(codestr, StaticCodeGenerator, modname="foo")
        f = self.find_code(code, "testfunc")
        self.assertInBytecode(f, "INVOKE_METHOD", (("foo", "B", "f"), 0))
        with self.in_module(codestr, code_gen=StaticCodeGenerator) as mod:
            test = mod["testfunc"]
            self.assertEqual(test(True), "abc")
            self.assertEqual(test(False), 42)

    def test_widen_to_dynamic(self):
        self.assertReturns(
            """
            def f(x, flag):
                if flag:
                    x = 3
                return x
            """,
            "dynamic",
        )

    def test_assign_conditional_both_sides(self):
        codestr = """
            class B:
                def f(self):
                    return 42
            class D(B):
                def f(self):
                    return 'abc'

            def testfunc(abc):
                x = B()
                if abc:
                    x = D()
                else:
                    x = D()
                return x.f()
        """

        code = self.compile(codestr, StaticCodeGenerator, modname="foo")
        f = self.find_code(code, "testfunc")
        self.assertInBytecode(f, "INVOKE_METHOD", (("foo", "D", "f"), 0))
        with self.in_module(codestr, code_gen=StaticCodeGenerator) as mod:
            test = mod["testfunc"]
            self.assertEqual(test(True), "abc")

    def test_assign_conditional_invoke_in_else(self):
        codestr = """
            class B:
                def f(self):
                    return 42
            class D(B):
                def f(self):
                    return 'abc'

            def testfunc(abc):
                x = B()
                if abc:
                    x = D()
                else:
                    return x.f()
        """

        code = self.compile(codestr, StaticCodeGenerator, modname="foo")
        f = self.find_code(code, "testfunc")
        self.assertInBytecode(f, "INVOKE_METHOD", (("foo", "B", "f"), 0))
        with self.in_module(codestr, code_gen=StaticCodeGenerator) as mod:
            test = mod["testfunc"]
            self.assertEqual(test(True), None)

    def test_assign_else_only(self):
        codestr = """
            class B:
                def f(self):
                    return 42
            class D(B):
                def f(self):
                    return 'abc'

            def testfunc(abc):
                if abc:
                    pass
                else:
                    x = B()
                return x.f()
        """

        code = self.compile(codestr, StaticCodeGenerator, modname="foo")
        f = self.find_code(code, "testfunc")
        self.assertInBytecode(f, "INVOKE_METHOD", (("foo", "B", "f"), 0))
        with self.in_module(codestr, code_gen=StaticCodeGenerator) as mod:
            test = mod["testfunc"]
            self.assertEqual(test(False), 42)

    def test_assign_test_var(self):
        codestr = """
            from typing import Optional

            def f(x: Optional[int]) -> int:
                if x is None:
                    x = 1
                return x
        """

        self.compile(codestr, StaticCodeGenerator, modname="foo")

    def test_assign_while(self):
        codestr = """
            class B:
                def f(self):
                    return 42
            class D(B):
                def f(self):
                    return 'abc'

            def testfunc(abc):
                x = B()
                while abc:
                    x = D()
                return x.f()
        """

        code = self.compile(codestr, StaticCodeGenerator, modname="foo")
        f = self.find_code(code, "testfunc")
        self.assertInBytecode(f, "INVOKE_METHOD", (("foo", "B", "f"), 0))
        with self.in_module(codestr, code_gen=StaticCodeGenerator) as mod:
            test = mod["testfunc"]
            self.assertEqual(test(False), 42)

    def test_assign_while_test_var(self):
        codestr = """
            from typing import Optional

            def f(x: Optional[int]) -> int:
                while x is None:
                    x = 1
                return x
        """
        self.compile(codestr, StaticCodeGenerator, modname="foo")

    def test_assign_while_returns(self):
        codestr = """
            from typing import Optional

            def f(x: Optional[int]) -> int:
                while x is None:
                    return 1
                return x
        """
        self.compile(codestr, StaticCodeGenerator, modname="foo")

    def test_assign_while_returns_but_assigns_first(self):
        codestr = """
            from typing import Optional

            def f(x: Optional[int]) -> int:
                y: Optional[int] = 1
                while x is None:
                    y = None
                    return 1
                return y
        """
        self.compile(codestr, StaticCodeGenerator, modname="foo")

    def test_while_else_reverses_condition(self):
        codestr = """
            from typing import Optional

            def f(x: Optional[int]) -> int:
                while x is None:
                    pass
                else:
                    return x
                return 1
        """
        self.compile(codestr, StaticCodeGenerator, modname="foo")

    def test_continue_condition(self):
        codestr = """
            from typing import Optional

            def f(x: Optional[str]) -> str:
                while True:
                    if x is None:
                        continue
                    return x
        """
        self.compile(codestr, StaticCodeGenerator, modname="foo")

    def test_break_condition(self):
        codestr = """
            from typing import Optional

            def f(x: Optional[str]) -> str:
                while True:
                    if x is None:
                        break
                    return x
        """
        self.compile(codestr, StaticCodeGenerator, modname="foo")

    def test_assign_but_annotated(self):
        codestr = """
            class B:
                def f(self):
                    return 42
            class D(B):
                def f(self):
                    return 'abc'

            def testfunc(abc):
                x: B = D()
                return x.f()
        """

        code = self.compile(codestr, StaticCodeGenerator, modname="foo")
        f = self.find_code(code, "testfunc")
        self.assertInBytecode(f, "INVOKE_METHOD", (("foo", "D", "f"), 0))
        with self.in_module(codestr, code_gen=StaticCodeGenerator) as mod:
            test = mod["testfunc"]
            self.assertEqual(test(False), "abc")

    def test_assign_while_2(self):
        codestr = """
            class B:
                def f(self):
                    return 42
            class D(B):
                def f(self):
                    return 'abc'

            def testfunc(abc):
                x: B = D()
                while abc:
                    x = B()
                return x.f()
        """

        code = self.compile(codestr, StaticCodeGenerator, modname="foo")
        f = self.find_code(code, "testfunc")
        self.assertInBytecode(f, "INVOKE_METHOD", (("foo", "B", "f"), 0))
        with self.in_module(codestr, code_gen=StaticCodeGenerator) as mod:
            test = mod["testfunc"]
            self.assertEqual(test(False), "abc")

    def test_assign_while_else(self):
        codestr = """
            class B:
                def f(self):
                    return 42
            class D(B):
                def f(self):
                    return 'abc'

            def testfunc(abc):
                x = B()
                while abc:
                    pass
                else:
                    x = D()
                return x.f()
        """

        code = self.compile(codestr, StaticCodeGenerator, modname="foo")
        f = self.find_code(code, "testfunc")
        self.assertInBytecode(f, "INVOKE_METHOD", (("foo", "B", "f"), 0))
        with self.in_module(codestr, code_gen=StaticCodeGenerator) as mod:
            test = mod["testfunc"]
            self.assertEqual(test(False), "abc")

    def test_assign_while_else_2(self):
        codestr = """
            class B:
                def f(self):
                    return 42
            class D(B):
                def f(self):
                    return 'abc'

            def testfunc(abc):
                x: B = D()
                while abc:
                    pass
                else:
                    x = B()
                return x.f()
        """

        code = self.compile(codestr, StaticCodeGenerator, modname="foo")
        f = self.find_code(code, "testfunc")
        self.assertInBytecode(f, "INVOKE_METHOD", (("foo", "B", "f"), 0))
        with self.in_module(codestr, code_gen=StaticCodeGenerator) as mod:
            test = mod["testfunc"]
            self.assertEqual(test(False), 42)

    def test_assign_try_except_no_initial(self):
        codestr = """
            class B:
                def f(self):
                    return 42
            class D(B):
                def f(self):
                    return 'abc'

            def testfunc():
                try:
                    x: B = D()
                except:
                    x = B()
                return x.f()
        """

        code = self.compile(codestr, StaticCodeGenerator, modname="foo")
        f = self.find_code(code, "testfunc")
        self.assertInBytecode(f, "INVOKE_METHOD", (("foo", "B", "f"), 0))
        with self.in_module(codestr, code_gen=StaticCodeGenerator) as mod:
            test = mod["testfunc"]
            self.assertEqual(test(), "abc")

    def test_narrow_or(self):
        codestr = """
            def f(x: int | None) -> int:
                if x is None or x > 1:
                    x = 1
                return x
        """
        self.compile(codestr)

    def test_type_of_or(self):
        codestr = """
            def f(x: int, y: str) -> int | str:
                return x or y
        """
        self.compile(codestr)

    def test_none_annotation(self):
        codestr = """
            from typing import Optional

            def f(x: Optional[int]) -> None:
                return x
        """
        with self.assertRaisesRegex(
            TypedSyntaxError,
            type_mismatch("Optional[int]", "None"),
        ):
            self.compile(codestr, StaticCodeGenerator, modname="foo")

    def test_none_compare(self):
        codestr = """
            def f(x: int | None):
                if x > 1:
                    x = 1
                return x
        """
        with self.assertRaisesRegex(
            TypedSyntaxError,
            r"'>' not supported between 'Optional\[int\]' and 'Exact\[int\]'",
        ):
            self.compile(codestr)

    def test_none_compare_reverse(self):
        codestr = """
            def f(x: int | None):
                if 1 > x:
                    x = 1
                return x
        """
        with self.assertRaisesRegex(
            TypedSyntaxError,
            r"'>' not supported between 'Exact\[int\]' and 'Optional\[int\]'",
        ):
            self.compile(codestr)

    def test_union_compare(self):
        codestr = """
            def f(x: int | float) -> bool:
                return x > 0
        """
        with self.in_strict_module(codestr) as mod:
            self.assertEqual(mod.f(3), True)
            self.assertEqual(mod.f(3.1), True)
            self.assertEqual(mod.f(-3), False)
            self.assertEqual(mod.f(-3.1), False)

    def test_global_int(self):
        codestr = """
            X: int =  60 * 60 * 24
        """

        code = self.compile(codestr, StaticCodeGenerator, modname="foo")
        with self.in_module(codestr, code_gen=StaticCodeGenerator) as mod:
            X = mod["X"]
            self.assertEqual(X, 60 * 60 * 24)

    def test_with_traceback(self):
        codestr = """
            def f():
                x = Exception()
                return x.with_traceback(None)
        """

        code = self.compile(codestr, StaticCodeGenerator, modname="foo")
        with self.in_module(codestr, code_gen=StaticCodeGenerator) as mod:
            f = mod["f"]
            self.assertEqual(type(f()), Exception)
            self.assertInBytecode(
                f, "INVOKE_METHOD", (("builtins", "BaseException", "with_traceback"), 1)
            )

    def test_assign_num_to_object(self):
        codestr = """
            def f():
                x: object = 42
        """

        code = self.compile(codestr, StaticCodeGenerator, modname="foo")
        with self.in_module(codestr, code_gen=StaticCodeGenerator) as mod:
            f = mod["f"]
            self.assertNotInBytecode(f, "CAST", ("builtins", "object"))

    def test_assign_num_to_dynamic(self):
        codestr = """
            def f():
                x: foo = 42
        """

        code = self.compile(codestr, StaticCodeGenerator, modname="foo")
        with self.in_module(codestr, code_gen=StaticCodeGenerator) as mod:
            f = mod["f"]
            self.assertNotInBytecode(f, "CAST", ("builtins", "object"))

    def test_assign_dynamic_to_object(self):
        codestr = """
            def f(C):
                x: object = C()
        """

        code = self.compile(codestr, StaticCodeGenerator, modname="foo")
        with self.in_module(codestr, code_gen=StaticCodeGenerator) as mod:
            f = mod["f"]
            self.assertNotInBytecode(f, "CAST", ("builtins", "object"))

    def test_assign_dynamic_to_dynamic(self):
        codestr = """
            def f(C):
                x: unknown = C()
        """

        code = self.compile(codestr, StaticCodeGenerator, modname="foo")
        with self.in_module(codestr, code_gen=StaticCodeGenerator) as mod:
            f = mod["f"]
            self.assertNotInBytecode(f, "CAST", ("builtins", "object"))

    def test_assign_constant_to_object(self):
        codestr = """
            def f():
                x: object = 42 + 1
        """

        code = self.compile(codestr, StaticCodeGenerator, modname="foo")
        with self.in_module(codestr, code_gen=StaticCodeGenerator) as mod:
            f = mod["f"]
            self.assertNotInBytecode(f, "CAST", ("builtins", "object"))

    def test_assign_try_except_typing(self):
        codestr = """
            def testfunc():
                try:
                    pass
                except Exception as e:
                    pass
                return 42
        """

        # We don't do anything special w/ Exception type yet, but it should compile
        code = self.compile(codestr, StaticCodeGenerator, modname="foo")

    def test_assign_try_except_typing_predeclared(self):
        codestr = """
            def testfunc():
                e: Exception
                try:
                    pass
                except Exception as e:
                    pass
                return 42
        """
        # We don't do anything special w/ Exception type yet, but it should compile
        code = self.compile(codestr, StaticCodeGenerator, modname="foo")

    def test_assign_try_except_typing_narrowed(self):
        codestr = """
            class E(Exception):
                pass

            def testfunc():
                e: Exception
                try:
                    pass
                except E as e:
                    pass
                return 42
        """
        # We don't do anything special w/ Exception type yet, but it should compile
        code = self.compile(codestr, StaticCodeGenerator, modname="foo")

    def test_assign_try_except_typing_redeclared_after(self):
        codestr = """
            def testfunc():
                try:
                    pass
                except Exception as e:
                    pass
                e: int = 42
                return 42
        """
        # We don't do anything special w/ Exception type yet, but it should compile
        code = self.compile(codestr, StaticCodeGenerator, modname="foo")

    def test_assign_try_except_redeclare(self):
        codestr = """
            def testfunc():
                e: int
                try:
                    pass
                except Exception as e:
                    pass
                return 42
        """

        code = self.compile(codestr, StaticCodeGenerator, modname="foo")

    def test_assign_try_except_redeclare_unknown_type(self):
        codestr = """
            def testfunc():
                e: int
                try:
                    pass
                except UnknownException as e:
                    pass
                return 42
        """

        code = self.compile(codestr, StaticCodeGenerator, modname="foo")

    def test_assign_try_assign_in_except(self):
        codestr = """
            class B:
                def f(self):
                    return 42
            class D(B):
                def f(self):
                    return 'abc'

            def testfunc():
                x: B = D()
                try:
                    pass
                except:
                    x = B()
                return x.f()
        """

        code = self.compile(codestr, StaticCodeGenerator, modname="foo")
        f = self.find_code(code, "testfunc")
        self.assertInBytecode(f, "INVOKE_METHOD", (("foo", "B", "f"), 0))
        with self.in_module(codestr, code_gen=StaticCodeGenerator) as mod:
            test = mod["testfunc"]
            self.assertEqual(test(), "abc")

    def test_assign_try_assign_in_second_except(self):
        codestr = """
            class B:
                def f(self):
                    return 42
            class D(B):
                def f(self):
                    return 'abc'

            def testfunc():
                x: B = D()
                try:
                    pass
                except TypeError:
                    pass
                except:
                    x = B()
                return x.f()
        """

        code = self.compile(codestr, StaticCodeGenerator, modname="foo")
        f = self.find_code(code, "testfunc")
        self.assertInBytecode(f, "INVOKE_METHOD", (("foo", "B", "f"), 0))
        with self.in_module(codestr, code_gen=StaticCodeGenerator) as mod:
            test = mod["testfunc"]
            self.assertEqual(test(), "abc")

    def test_assign_try_assign_in_except_with_var(self):
        codestr = """
            class B:
                def f(self):
                    return 42
            class D(B):
                def f(self):
                    return 'abc'

            def testfunc():
                x: B = D()
                try:
                    pass
                except TypeError as e:
                    x = B()
                return x.f()
        """

        code = self.compile(codestr, StaticCodeGenerator, modname="foo")
        f = self.find_code(code, "testfunc")
        self.assertInBytecode(f, "INVOKE_METHOD", (("foo", "B", "f"), 0))
        with self.in_module(codestr, code_gen=StaticCodeGenerator) as mod:
            test = mod["testfunc"]
            self.assertEqual(test(), "abc")

    def test_try_except_finally(self):
        codestr = """
            class B:
                def f(self):
                    return 42
            class D(B):
                def f(self):
                    return 'abc'

            def testfunc():
                x: B = D()
                try:
                    pass
                except TypeError:
                    pass
                finally:
                    x = B()
                return x.f()
        """

        code = self.compile(codestr, StaticCodeGenerator, modname="foo")
        f = self.find_code(code, "testfunc")
        self.assertInBytecode(f, "INVOKE_METHOD", (("foo", "B", "f"), 0))
        with self.in_module(codestr, code_gen=StaticCodeGenerator) as mod:
            test = mod["testfunc"]
            self.assertEqual(test(), 42)

    def test_assign_try_assign_in_try(self):
        codestr = """
            class B:
                def f(self):
                    return 42
            class D(B):
                def f(self):
                    return 'abc'

            def testfunc():
                x: B = D()
                try:
                    x = B()
                except:
                    pass
                return x.f()
        """

        code = self.compile(codestr, StaticCodeGenerator, modname="foo")
        f = self.find_code(code, "testfunc")
        self.assertInBytecode(f, "INVOKE_METHOD", (("foo", "B", "f"), 0))
        with self.in_module(codestr, code_gen=StaticCodeGenerator) as mod:
            test = mod["testfunc"]
            self.assertEqual(test(), 42)

    def test_assign_try_assign_in_finally(self):
        codestr = """
            class B:
                def f(self):
                    return 42
            class D(B):
                def f(self):
                    return 'abc'

            def testfunc():
                x: B = D()
                try:
                    pass
                finally:
                    x = B()
                return x.f()
        """

        code = self.compile(codestr, StaticCodeGenerator, modname="foo")
        f = self.find_code(code, "testfunc")
        self.assertInBytecode(f, "INVOKE_METHOD", (("foo", "B", "f"), 0))
        with self.in_module(codestr, code_gen=StaticCodeGenerator) as mod:
            test = mod["testfunc"]
            self.assertEqual(test(), 42)

    def test_assign_try_assign_in_else(self):
        codestr = """
            class B:
                def f(self):
                    return 42
            class D(B):
                def f(self):
                    return 'abc'

            def testfunc():
                x: B = D()
                try:
                    pass
                except:
                    pass
                else:
                    x = B()
                return x.f()
        """

        code = self.compile(codestr, StaticCodeGenerator, modname="foo")
        f = self.find_code(code, "testfunc")
        self.assertInBytecode(f, "INVOKE_METHOD", (("foo", "B", "f"), 0))
        with self.in_module(codestr, code_gen=StaticCodeGenerator) as mod:
            test = mod["testfunc"]
            self.assertEqual(test(), 42)

    def test_if_optional_reassign(self):
        codestr = """
        class C: pass

        def testfunc(abc: Optional[C]):
            if abc is not None:
                abc = None
        """
        code = self.compile(codestr, StaticCodeGenerator, modname="foo")

    def test_widening_assign(self):
        codestr = """
            from __static__ import int8, int16, box

            def testfunc():
                x: int16
                y: int8
                x = y = 42
                return box(x), box(y)
        """
        with self.in_module(codestr, code_gen=StaticCodeGenerator) as mod:
            test = mod["testfunc"]
            self.assertEqual(test(), (42, 42))

    def test_unknown_imported_annotation(self):
        codestr = """
            from unknown_mod import foo

            def testfunc():
                x: foo = 42
                return x
        """
        code = self.compile(codestr, StaticCodeGenerator, modname="foo")

    def test_widening_assign_reassign(self):
        codestr = """
            from __static__ import int8, int16, box

            def testfunc():
                x: int16
                y: int8
                x = y = 42
                x = 257
                return box(x), box(y)
        """
        with self.in_module(codestr, code_gen=StaticCodeGenerator) as mod:
            test = mod["testfunc"]
            self.assertEqual(test(), (257, 42))

    def test_widening_assign_reassign_error(self):
        codestr = """
            from __static__ import int8, int16, box

            def testfunc():
                x: int16
                y: int8
                x = y = 42
                y = 128
                return box(x), box(y)
        """
        with self.assertRaisesRegex(
            TypedSyntaxError,
            "constant 128 is outside of the range -128 to 127 for int8",
        ):
            self.compile(codestr, StaticCodeGenerator, modname="foo")

    def test_narrowing_assign_literal(self):
        codestr = """
            from __static__ import int8, int16, box

            def testfunc():
                x: int8
                y: int16
                x = y = 42
                return box(x), box(y)
        """
        self.compile(codestr, StaticCodeGenerator, modname="foo")

    def test_narrowing_assign_out_of_range(self):
        codestr = """
            from __static__ import int8, int16, box

            def testfunc():
                x: int8
                y: int16
                x = y = 300
                return box(x), box(y)
        """
        with self.assertRaisesRegex(
            TypedSyntaxError,
            "constant 300 is outside of the range -128 to 127 for int8",
        ):
            self.compile(codestr, StaticCodeGenerator, modname="foo")

    def test_module_primitive(self):
        codestr = """
            from __static__ import int8
            x: int8
        """
        with self.assertRaisesRegex(
            TypedSyntaxError, "cannot use primitives in global or closure scope"
        ):
            self.compile(codestr, StaticCodeGenerator, modname="foo")

    def test_implicit_module_primitive(self):
        codestr = """
            from __static__ import int8
            x = y = int8(0)
        """
        with self.assertRaisesRegex(
            TypedSyntaxError, "cannot use primitives in global or closure scope"
        ):
            self.compile(codestr, StaticCodeGenerator, modname="foo")

    def test_chained_primitive_to_non_primitive(self):
        codestr = """
            from __static__ import int8
            def f():
                x: object
                y: int8 = 42
                x = y = 42
        """
        with self.assertRaisesRegex(
            TypedSyntaxError, "int8 cannot be assigned to object"
        ):
            self.compile(codestr, StaticCodeGenerator, modname="foo")

    def test_closure_primitive(self):
        codestr = """
            from __static__ import int8
            def f():
                x: int8 = 0
                def g():
                    return x
        """
        with self.assertRaisesRegex(
            TypedSyntaxError, "cannot use primitives in global or closure scope"
        ):
            self.compile(codestr, StaticCodeGenerator, modname="foo")

    def test_nonlocal_primitive(self):
        codestr = """
            from __static__ import int8
            def f():
                x: int8 = 0
                def g():
                    nonlocal x
                    x = 1
        """
        with self.assertRaisesRegex(
            TypedSyntaxError, "cannot use primitives in global or closure scope"
        ):
            self.compile(codestr, StaticCodeGenerator, modname="foo")

    def test_dynamic_chained_assign_param(self):
        codestr = """
            from __static__ import int16
            def testfunc(y):
                x: int16
                x = y = 42
                return box(x)
        """
        with self.assertRaisesRegex(
            TypedSyntaxError, type_mismatch("Exact[int]", "int16")
        ):
            self.compile(codestr, StaticCodeGenerator, modname="foo")

    def test_dynamic_chained_assign_param_2(self):
        codestr = """
            from __static__ import int16
            def testfunc(y):
                x: int16
                y = x = 42
        """
        with self.assertRaisesRegex(
            TypedSyntaxError, type_mismatch("int16", "dynamic")
        ):
            self.compile(codestr, StaticCodeGenerator, modname="foo")

    def test_dynamic_chained_assign_1(self):
        codestr = """
            from __static__ import int16, box
            def testfunc():
                x: int16
                x = y = 42
                return box(x)
        """
        with self.in_module(codestr, code_gen=StaticCodeGenerator) as mod:
            test = mod["testfunc"]
            self.assertEqual(test(), 42)

    def test_dynamic_chained_assign_2(self):
        codestr = """
            from __static__ import int16, box
            def testfunc():
                x: int16
                y = x = 42
                return box(y)
        """
        with self.in_module(codestr, code_gen=StaticCodeGenerator) as mod:
            test = mod["testfunc"]
            self.assertEqual(test(), 42)

    def test_tuple_assign_list(self):
        codestr = """
            from __static__ import int16, box
            def testfunc(a: int, b: int):
                x: int
                y: str
                x, y = [a, b]
        """
        with self.assertRaisesRegex(TypedSyntaxError, "int cannot be assigned to str"):
            self.compile(codestr, StaticCodeGenerator, modname="foo")

    def test_tuple_assign_tuple(self):
        codestr = """
            from __static__ import int16, box
            def testfunc(a: int, b: int):
                x: int
                y: str
                x, y = a, b
        """
        with self.assertRaisesRegex(TypedSyntaxError, "int cannot be assigned to str"):
            self.compile(codestr, StaticCodeGenerator, modname="foo")

    def test_tuple_assign_constant(self):
        codestr = """
            from __static__ import int16, box
            def testfunc():
                x: int
                y: str
                x, y = 1, 1
        """
        with self.assertRaisesRegex(
            TypedSyntaxError,
            r"type mismatch: Exact\[int\] cannot be assigned to str",
        ):
            self.compile(codestr, StaticCodeGenerator, modname="foo")

    def test_if_optional_cond(self):
        codestr = """
            from typing import Optional
            class C:
                def __init__(self):
                    self.field = 42

            def f(x: Optional[C]):
                return x.field if x is not None else None
        """

        self.compile(codestr, StaticCodeGenerator, modname="foo")

    def test_while_optional_cond(self):
        codestr = """
            from typing import Optional
            class C:
                def __init__(self):
                    self.field: Optional["C"] = self

            def f(x: Optional[C]):
                while x is not None:
                    val: Optional[C] = x.field
                    if val is not None:
                        x = val
        """

        self.compile(codestr, StaticCodeGenerator, modname="foo")

    def test_if_optional_dependent_conditions(self):
        codestr = """
            from typing import Optional
            class C:
                def __init__(self):
                    self.field: Optional[C] = None

            def f(x: Optional[C]) -> C:
                if x is not None and x.field is not None:
                    return x

                if x is None:
                    return C()

                return x
        """

        self.compile(codestr, StaticCodeGenerator, modname="foo")

    def test_none_attribute_error(self):
        codestr = """
            def f():
                x = None
                return x.foo
        """

        with self.assertRaisesRegex(
            TypedSyntaxError, "'NoneType' object has no attribute 'foo'"
        ):
            self.compile(codestr, StaticCodeGenerator, modname="foo")

    def test_none_call(self):
        codestr = """
            def f():
                x = None
                return x()
        """

        with self.assertRaisesRegex(
            TypedSyntaxError, "'NoneType' object is not callable"
        ):
            self.compile(codestr, StaticCodeGenerator, modname="foo")

    def test_none_subscript(self):
        codestr = """
            def f():
                x = None
                return x[0]
        """

        with self.assertRaisesRegex(
            TypedSyntaxError, "'NoneType' object is not subscriptable"
        ):
            self.compile(codestr, StaticCodeGenerator, modname="foo")

    def test_none_unaryop(self):
        codestr = """
            def f():
                x = None
                return -x
        """

        with self.assertRaisesRegex(
            TypedSyntaxError, "bad operand type for unary -: 'NoneType'"
        ):
            self.compile(codestr, StaticCodeGenerator, modname="foo")

    def test_vector_import(self):
        codestr = """
            from __static__ import int64, Vector

            def test() -> Vector[int64]:
                x: Vector[int64] = Vector[int64]()
                x.append(1)
                return x
        """

        with self.in_module(codestr, code_gen=StaticCodeGenerator) as mod:
            test = mod["test"]
            self.assertEqual(test(), array("L", [1]))

    def test_vector_assign_non_primitive(self):
        codestr = """
            from __static__ import int64, Vector

            def test(abc) -> Vector[int64]:
                x: Vector[int64] = Vector[int64](2)
                i: int64 = 0
                x[i] = abc
        """

        with self.assertRaisesRegex(
            TypedSyntaxError, "Cannot assign a dynamic to int64"
        ):
            self.compile(codestr)

    def test_vector_sizes(self):
        for signed in ["int", "uint"]:
            for size in ["8", "16", "32", "64"]:
                with self.subTest(size=size, signed=signed):
                    int_type = f"{signed}{size}"
                    codestr = f"""
                        from __static__ import {int_type}, Vector

                        def test() -> Vector[{int_type}]:
                            x: Vector[{int_type}] = Vector[{int_type}]()
                            y: {int_type} = 1
                            x.append(y)
                            return x
                    """

                    with self.in_module(codestr, code_gen=StaticCodeGenerator) as mod:
                        test = mod["test"]
                        res = test()
                        self.assertEqual(list(res), [1])

    def test_vector_invalid_literal(self):
        codestr = f"""
            from __static__ import int8, Vector

            def test() -> Vector[int8]:
                x: Vector[int8] = Vector[int8]()
                x.append(128)
                return x
        """
        with self.assertRaisesRegex(
            TypedSyntaxError,
            "type mismatch: int positional argument type mismatch int8",
        ):
            self.compile(codestr)

    def test_vector_wrong_size(self):
        codestr = f"""
            from __static__ import int8, int16, Vector

            def test() -> Vector[int8]:
                y: int16 = 1
                x: Vector[int8] = Vector[int8]()
                x.append(y)
                return x
        """

        with self.assertRaisesRegex(
            TypedSyntaxError,
            "type mismatch: int16 positional argument type mismatch int8",
        ):
            self.compile(codestr)

    def test_vector_presized(self):
        codestr = f"""
            from __static__ import int8, Vector

            def test() -> Vector[int8]:
                x: Vector[int8] = Vector[int8](4)
                x[1] = 1
                return x
        """

        with self.in_module(codestr) as mod:
            f = mod["test"]
            self.assertEqual(f(), array("b", [0, 1, 0, 0]))

    def test_array_import(self):
        codestr = """
            from __static__ import int64, Array

            def test() -> Array[int64]:
                x: Array[int64] = Array[int64](1)
                x[0] = 1
                return x
        """

        with self.in_module(codestr, code_gen=StaticCodeGenerator) as mod:
            test = mod["test"]
            self.assertEqual(test(), array("L", [1]))

    def test_array_create(self):
        codestr = """
            from __static__ import int64, Array

            def test() -> Array[int64]:
                x: Array[int64] = Array[int64]([1, 3, 5])
                return x
        """

        with self.in_module(codestr, code_gen=StaticCodeGenerator) as mod:
            test = mod["test"]
            self.assertEqual(test(), array("l", [1, 3, 5]))

    def test_array_create_failure(self):
        # todo - in the future we're going to support this, but for now fail it.
        codestr = """
            from __static__ import int64, Array

            class C: pass

            def test() -> Array[C]:
                return Array[C]([1, 3, 5])
        """
        with self.assertRaisesRegex(
            TypedSyntaxError, "Invalid Array element type: foo.C"
        ):
            self.compile(codestr, StaticCodeGenerator, modname="foo")

    def test_array_call_unbound(self):
        codestr = """
            from __static__ import Array

            def f() -> Array:
                return Array([1, 2, 3])
        """
        with self.assertRaisesRegex(
            TypedSyntaxError,
            r"create instances of a generic Type\[Exact\[Array\[T\]\]\]",
        ):
            self.compile(codestr, StaticCodeGenerator, modname="foo")

    def test_array_assign_wrong_type(self):
        codestr = """
            from __static__ import int64, char, Array

            def test() -> None:
                x: Array[int64] = Array[char]([48])
        """
        with self.assertRaisesRegex(
            TypedSyntaxError,
            type_mismatch(
                "Exact[Array[char]]",
                "Array[int64]",
            ),
        ):
            self.compile(codestr, StaticCodeGenerator, modname="foo")

    def test_array_subclass_assign(self):
        codestr = """
            from __static__ import int64, Array

            class MyArray(Array):
                pass

            def y(inexact: Array[int64]):
                exact = Array[int64]([1])
                exact = inexact
        """
        with self.assertRaisesRegex(
            TypedSyntaxError,
            type_mismatch(
                "Array[int64]",
                "Exact[Array[int64]]",
            ),
        ):
            self.compile(codestr, StaticCodeGenerator, modname="foo")

    def test_array_types(self):
        codestr = """
            from __static__ import (
                int8,
                int16,
                int32,
                int64,
                uint8,
                uint16,
                uint32,
                uint64,
                char,
                double,
                Array
            )
            from typing import Tuple

            def test() -> Tuple[Array[int64], Array[char], Array[double]]:
                x1: Array[int8] = Array[int8]([1, 3, -5])
                x2: Array[uint8] = Array[uint8]([1, 3, 5])
                x3: Array[int16] = Array[int16]([1, -3, 5])
                x4: Array[uint16] = Array[uint16]([1, 3, 5])
                x5: Array[int32] = Array[int32]([1, 3, 5])
                x6: Array[uint32] = Array[uint32]([1, 3, 5])
                x7: Array[int64] = Array[int64]([1, 3, 5])
                x8: Array[uint64] = Array[uint64]([1, 3, 5])
                x9: Array[char] = Array[char]([ord('a')])
                x10: Array[double] = Array[double]([1.1, 3.3, 5.5])
                x11: Array[float] = Array[float]([1.1, 3.3, 5.5])
                arrays = [
                    x1,
                    x2,
                    x3,
                    x4,
                    x5,
                    x6,
                    x7,
                    x8,
                    x9,
                    x10,
                    x11,
                ]
                first_elements = []
                for ar in arrays:
                    first_elements.append(ar[0])
                return (arrays, first_elements)
        """

        with self.in_module(codestr, code_gen=StaticCodeGenerator) as mod:
            test = mod["test"]
            arrays, first_elements = test()
            exp_arrays = [
                array(*args)
                for args in [
                    ("h", [1, 3, -5]),
                    ("H", [1, 3, 5]),
                    ("i", [1, -3, 5]),
                    ("I", [1, 3, 5]),
                    ("l", [1, 3, 5]),
                    ("L", [1, 3, 5]),
                    ("q", [1, 3, 5]),
                    ("Q", [1, 3, 5]),
                    ("b", [ord("a")]),
                    ("d", [1.1, 3.3, 5.5]),
                    ("f", [1.1, 3.3, 5.5]),
                ]
            ]
            exp_first_elements = [ar[0] for ar in exp_arrays]
            for result, expectation in zip(arrays, exp_arrays):
                self.assertEqual(result, expectation)
            for result, expectation in zip(first_elements, exp_first_elements):
                self.assertEqual(result, expectation)

    def test_assign_type_propagation(self):
        codestr = """
            def test() -> int:
                x = 5
                return x
        """
        self.compile(codestr, StaticCodeGenerator, modname="foo")

    def test_assign_subtype_handling(self):
        codestr = """
            class B: pass
            class D(B): pass

            def f():
                b = B()
                b = D()
                b = B()
        """
        self.compile(codestr, StaticCodeGenerator, modname="foo")

    def test_assign_subtype_handling_fail(self):
        codestr = """
            class B: pass
            class D(B): pass

            def f():
                d = D()
                d = B()
        """
        with self.assertRaisesRegex(TypedSyntaxError, type_mismatch("foo.B", "foo.D")):
            self.compile(codestr, StaticCodeGenerator, modname="foo")

    def test_assign_chained(self):
        codestr = """
            def test() -> str:
                x: str = "hi"
                y = x = "hello"
                return y
        """
        self.compile(codestr, StaticCodeGenerator, modname="foo")

    def test_assign_chained_failure_wrong_target_type(self):
        codestr = """
            def test() -> str:
                x = 1
                y = x = "hello"
                return y
        """
        with self.assertRaisesRegex(
            TypedSyntaxError, type_mismatch("Exact[str]", "int")
        ):
            self.compile(codestr, StaticCodeGenerator, modname="foo")

    def test_chained_assign_type_propagation(self):
        codestr = """
            from __static__ import int64, char, Array

            def test2() -> Array[char]:
                x = y = Array[char]([48])
                return y
        """
        self.compile(codestr, StaticCodeGenerator, modname="foo")

    def test_chained_assign_type_propagation_failure_redefine(self):
        codestr = """
            from __static__ import int64, char, Array

            def test2() -> Array[char]:
                x = Array[int64]([54])
                x = y = Array[char]([48])
                return y
        """
        with self.assertRaisesRegex(
            TypedSyntaxError,
            type_mismatch(
                "Exact[Array[char]]",
                "Exact[Array[int64]]",
            ),
        ):
            self.compile(codestr, StaticCodeGenerator, modname="foo")

    def test_chained_assign_type_propagation_failure_redefine_2(self):
        codestr = """
            from __static__ import int64, char, Array

            def test2() -> Array[char]:
                x = Array[int64]([54])
                y = x = Array[char]([48])
                return y
        """
        with self.assertRaisesRegex(
            TypedSyntaxError,
            type_mismatch(
                "Exact[Array[char]]",
                "Exact[Array[int64]]",
            ),
        ):
            self.compile(codestr, StaticCodeGenerator, modname="foo")

    def test_chained_assign_type_inference(self):
        codestr = """
            from __static__ import int64, char, Array

            def test2():
                y = x = 4
                x = "hello"
                return y
        """
        with self.assertRaisesRegex(
            TypedSyntaxError, type_mismatch("Exact[str]", "int")
        ):
            self.compile(codestr, StaticCodeGenerator, modname="foo")

    def test_chained_assign_type_inference_2(self):
        codestr = """
            from __static__ import int64, char, Array

            def test2():
                y = x = 4
                y = "hello"
                return x
        """
        with self.assertRaisesRegex(
            TypedSyntaxError, type_mismatch("Exact[str]", "int")
        ):
            self.compile(codestr, StaticCodeGenerator, modname="foo")

    def test_array_inplace_assign(self):
        codestr = """
            from __static__ import Array, int8

            def m() -> Array[int8]:
                a = Array[int8]([1, 3, -5, -1, 7, 22])
                a[0] += 1
                return a
        """
        with self.in_module(codestr) as mod:
            m = mod["m"]
            self.assertEqual(m()[0], 2)

    def test_array_subscripting_slice(self):
        codestr = """
            from __static__ import Array, int8

            def m() -> Array[int8]:
                a = Array[int8]([1, 3, -5, -1, 7, 22])
                return a[1:3]
        """
        self.compile(codestr, StaticCodeGenerator, modname="foo")

    @skipIf(cinderjit is not None, "can't report error from JIT")
    def test_load_uninit_module(self):
        """verify we don't crash if we receive a module w/o a dictionary"""
        codestr = """
        class C:
            def __init__(self):
                self.x: Optional[C] = None

        """
        with self.in_module(codestr) as mod:
            C = mod["C"]

            class UninitModule(ModuleType):
                def __init__(self):
                    # don't call super init
                    pass

            sys.modules["test_load_uninit_module"] = UninitModule()
            with self.assertRaisesRegex(
                TypeError, r"unknown type \('test_load_uninit_module', 'C'\)"
            ):
                C()

    def test_module_subclass(self):
        codestr = """
        class C:
            def __init__(self):
                self.x: Optional[C] = None

        """
        with self.in_module(codestr) as mod:
            C = mod["C"]

            class CustomModule(ModuleType):
                def __getattr__(self, name):
                    if name == "C":
                        return C

            sys.modules["test_module_subclass"] = CustomModule("test_module_subclass")
            c = C()
            self.assertEqual(c.x, None)

    def test_invoke_and_raise_noframe_strictmod(self):
        codestr = """
        from __static__.compiler_flags import noframe

        def x():
            raise TypeError()

        def y():
            return x()
        """
        with self.in_strict_module(codestr) as mod:
            y = mod.y
            x = mod.x
            with self.assertRaises(TypeError):
                y()
            self.assert_jitted(x)
            self.assertInBytecode(
                y,
                "INVOKE_FUNCTION",
                (("test_invoke_and_raise_noframe_strictmod", "x"), 0),
            )

    def test_override_okay(self):
        codestr = """
        class B:
            def f(self) -> "B":
                return self

        def f(x: B):
            return x.f()
        """
        with self.in_module(codestr) as mod:
            B = mod["B"]
            f = mod["f"]

            class D(B):
                def f(self):
                    return self

            x = f(D())

    def test_override_override_inherited(self):
        codestr = """
        from typing import Optional
        class B:
            def f(self) -> "Optional[B]":
                return self

        class D(B):
            pass

        def f(x: B):
            return x.f()
        """
        with self.in_module(codestr) as mod:
            B = mod["B"]
            D = mod["D"]
            f = mod["f"]

            b = B()
            d = D()
            self.assertEqual(f(b), b)
            self.assertEqual(f(d), d)

            D.f = lambda self: None
            self.assertEqual(f(b), b)
            self.assertEqual(f(d), None)

    def test_override_bad_ret(self):
        codestr = """
        class B:
            def f(self) -> "B":
                return self

        def f(x: B):
            return x.f()
        """
        with self.in_module(codestr) as mod:
            B = mod["B"]
            f = mod["f"]

            class D(B):
                def f(self):
                    return 42

            with self.assertRaisesRegex(
                TypeError, "unexpected return type from D.f, expected B, got int"
            ):
                f(D())

    def test_dynamic_base(self):
        nonstatic_code = """
            class Foo:
                pass
        """

        with self.in_module(
            nonstatic_code, code_gen=PythonCodeGenerator, name="nonstatic"
        ):
            codestr = """
                from nonstatic import Foo

                class A(Foo):
                    def __init__(self):
                        self.x = 1

                    def f(self) -> int:
                        return self.x

                def f(x: A) -> int:
                    return x.f()
            """
            with self.in_module(codestr) as mod:
                f = mod["f"]
                self.assertInBytecode(f, "INVOKE_METHOD")
                a = mod["A"]()
                self.assertEqual(f(a), 1)
                # x is a data descriptor, it takes precedence
                a.__dict__["x"] = 100
                self.assertEqual(f(a), 1)
                # but methods are normal descriptors, instance
                # attributes should take precedence
                a.__dict__["f"] = lambda: 42
                self.assertEqual(f(a), 42)


    def test_invoke_type_modified(self):
        codestr = """
            class C:
                def f(self):
                    return 1

            def x(c: C):
                x = c.f()
                x += c.f()
                return x
        """

        code = self.compile(codestr, StaticCodeGenerator, modname="foo")
        x = self.find_code(code, "x")
        self.assertInBytecode(x, "INVOKE_METHOD", (("foo", "C", "f"), 0))

        with self.in_module(codestr) as mod:
            x, C = mod["x"], mod["C"]
            self.assertEqual(x(C()), 2)
            C.f = lambda self: 42
            self.assertEqual(x(C()), 84)

    def test_invoke_type_modified_pre_invoke(self):
        codestr = """
            class C:
                def f(self):
                    return 1

            def x(c: C):
                x = c.f()
                x += c.f()
                return x
        """

        code = self.compile(codestr, StaticCodeGenerator, modname="foo")
        x = self.find_code(code, "x")
        self.assertInBytecode(x, "INVOKE_METHOD", (("foo", "C", "f"), 0))

        with self.in_module(codestr) as mod:
            x, C = mod["x"], mod["C"]
            C.f = lambda self: 42
            self.assertEqual(x(C()), 84)

    def test_override_modified_base_class(self):
        codestr = """
        class B:
            def f(self):
                return 1

        def f(x: B):
            return x.f()
        """
        with self.in_module(codestr) as mod:
            B = mod["B"]
            f = mod["f"]
            B.f = lambda self: 2

            class D(B):
                def f(self):
                    return 3

            d = D()
            self.assertEqual(f(d), 3)

    def test_override_remove_base_method(self):
        codestr = """
        from typing import Optional
        class B:
            def f(self) -> "B":
                return self

        class D(B): pass

        def f(x: B):
            return x.f()
        """
        with self.in_module(codestr) as mod:
            B = mod["B"]
            D = mod["D"]
            f = mod["f"]
            b = B()
            d = D()
            self.assertEqual(f(b), b)
            self.assertEqual(f(d), d)
            del B.f

            with self.assertRaises(AttributeError):
                f(b)
            with self.assertRaises(AttributeError):
                f(d)

    def test_override_remove_derived_method(self):
        codestr = """
        from typing import Optional
        class B:
            def f(self) -> "Optional[B]":
                return self

        class D(B):
            def f(self) -> Optional["B"]:
                return None

        def f(x: B):
            return x.f()
        """
        with self.in_module(codestr) as mod:
            B = mod["B"]
            D = mod["D"]
            f = mod["f"]
            b = B()
            d = D()
            self.assertEqual(f(b), b)
            self.assertEqual(f(d), None)
            del D.f

            self.assertEqual(f(b), b)
            self.assertEqual(f(d), d)

    def test_override_remove_method(self):
        codestr = """
        from typing import Optional
        class B:
            def f(self) -> "Optional[B]":
                return self

        def f(x: B):
            return x.f()
        """
        with self.in_module(codestr) as mod:
            B = mod["B"]
            f = mod["f"]
            b = B()
            self.assertEqual(f(b), b)
            del B.f

            with self.assertRaises(AttributeError):
                f(b)

    def test_override_remove_method_add_type_check(self):
        codestr = """
        from typing import Optional
        class B:
            def f(self) -> "B":
                return self

        def f(x: B):
            return x.f()
        """
        with self.in_module(codestr) as mod:
            B = mod["B"]
            f = mod["f"]
            b = B()
            self.assertEqual(f(b), b)
            del B.f

            with self.assertRaises(AttributeError):
                f(b)

            B.f = lambda self: None
            with self.assertRaises(TypeError):
                f(b)

    def test_override_update_derived(self):
        codestr = """
        from typing import Optional
        class B:
            def f(self) -> "Optional[B]":
                return self

        class D(B):
            pass

        def f(x: B):
            return x.f()
        """
        with self.in_module(codestr) as mod:
            B = mod["B"]
            D = mod["D"]
            f = mod["f"]

            b = B()
            d = D()
            self.assertEqual(f(b), b)
            self.assertEqual(f(d), d)

            B.f = lambda self: None
            self.assertEqual(f(b), None)
            self.assertEqual(f(d), None)

    def test_override_update_derived_2(self):
        codestr = """
        from typing import Optional
        class B:
            def f(self) -> "Optional[B]":
                return self

        class D1(B): pass

        class D(D1):
            pass

        def f(x: B):
            return x.f()
        """
        with self.in_module(codestr) as mod:
            B = mod["B"]
            D = mod["D"]
            f = mod["f"]

            b = B()
            d = D()
            self.assertEqual(f(b), b)
            self.assertEqual(f(d), d)

            B.f = lambda self: None
            self.assertEqual(f(b), None)
            self.assertEqual(f(d), None)

    def test_method_prologue(self):
        codestr = """
        def f(x: str):
            return 42
        """
        with self.in_module(codestr) as mod:
            f = mod["f"]
            self.assertInBytecode(f, "CHECK_ARGS", (0, ("builtins", "str")))
            with self.assertRaisesRegex(
                TypeError, ".*expected 'str' for argument x, got 'int'"
            ):
                f(42)

    def test_method_prologue_2(self):
        codestr = """
        def f(x, y: str):
            return 42
        """
        with self.in_module(codestr) as mod:
            f = mod["f"]
            self.assertInBytecode(f, "CHECK_ARGS", (1, ("builtins", "str")))
            with self.assertRaisesRegex(
                TypeError, ".*expected 'str' for argument y, got 'int'"
            ):
                f("abc", 42)

    def test_method_prologue_3(self):
        codestr = """
        def f(x: int, y: str):
            return 42
        """
        with self.in_module(codestr) as mod:
            f = mod["f"]
            self.assertInBytecode(
                f, "CHECK_ARGS", (0, ("builtins", "int"), 1, ("builtins", "str"))
            )
            with self.assertRaisesRegex(
                TypeError, ".*expected 'str' for argument y, got 'int'"
            ):
                f(42, 42)

    def test_method_prologue_posonly(self):
        codestr = """
        def f(x: int, /, y: str):
            return 42
        """
        with self.in_module(codestr) as mod:
            f = mod["f"]
            self.assertInBytecode(
                f, "CHECK_ARGS", (0, ("builtins", "int"), 1, ("builtins", "str"))
            )
            with self.assertRaisesRegex(
                TypeError, ".*expected 'str' for argument y, got 'int'"
            ):
                f(42, 42)

    def test_method_prologue_shadowcode(self):
        codestr = """
        def f(x, y: str):
            return 42
        """
        with self.in_module(codestr) as mod:
            f = mod["f"]
            self.assertInBytecode(f, "CHECK_ARGS", (1, ("builtins", "str")))
            for i in range(100):
                self.assertEqual(f("abc", "abc"), 42)
            with self.assertRaisesRegex(
                TypeError, ".*expected 'str' for argument y, got 'int'"
            ):
                f("abc", 42)

    def test_method_prologue_shadowcode_2(self):
        codestr = """
        def f(x: str):
            return 42
        """
        with self.in_module(codestr) as mod:
            f = mod["f"]
            self.assertInBytecode(f, "CHECK_ARGS", (0, ("builtins", "str")))
            for i in range(100):
                self.assertEqual(f("abc"), 42)
            with self.assertRaisesRegex(
                TypeError, ".*expected 'str' for argument x, got 'int'"
            ):
                f(42)

    def test_method_prologue_no_annotation(self):
        codestr = """
        def f(x):
            return 42
        """
        with self.in_module(codestr) as mod:
            f = mod["f"]
            self.assertInBytecode(f, "CHECK_ARGS", ())
            self.assertEqual(f("abc"), 42)

    def test_method_prologue_kwonly(self):
        codestr = """
        def f(*, x: str):
            return 42
        """
        with self.in_module(codestr) as mod:
            f = mod["f"]
            self.assertInBytecode(f, "CHECK_ARGS", (0, ("builtins", "str")))
            with self.assertRaisesRegex(
                TypeError, "f expected 'str' for argument x, got 'int'"
            ):
                f(x=42)

    def test_method_prologue_kwonly_2(self):
        codestr = """
        def f(x, *, y: str):
            return 42
        """
        with self.in_module(codestr) as mod:
            f = mod["f"]
            self.assertInBytecode(f, "CHECK_ARGS", (1, ("builtins", "str")))
            with self.assertRaisesRegex(
                TypeError, "f expected 'str' for argument y, got 'object'"
            ):
                f(1, y=object())

    def test_method_prologue_kwonly_3(self):
        codestr = """
        def f(x, *, y: str, z=1):
            return 42
        """
        with self.in_module(codestr) as mod:
            f = mod["f"]
            self.assertInBytecode(f, "CHECK_ARGS", (1, ("builtins", "str")))
            with self.assertRaisesRegex(
                TypeError, "f expected 'str' for argument y, got 'object'"
            ):
                f(1, y=object())

    def test_method_prologue_kwonly_4(self):
        codestr = """
        def f(x, *, y: str, **rest):
            return 42
        """
        with self.in_module(codestr) as mod:
            f = mod["f"]
            self.assertInBytecode(f, "CHECK_ARGS", (1, ("builtins", "str")))
            with self.assertRaisesRegex(
                TypeError, "f expected 'str' for argument y, got 'object'"
            ):
                f(1, y=object(), z=2)

    def test_method_prologue_kwonly_no_annotation(self):
        codestr = """
        def f(*, x):
            return 42
        """
        with self.in_module(codestr) as mod:
            f = mod["f"]
            self.assertInBytecode(f, "CHECK_ARGS", ())
            f(x=42)

    def test_package_no_parent(self):
        codestr = """
            class C:
                def f(self):
                    return 42
        """
        with self.in_module(
            codestr, code_gen=StaticCodeGenerator, name="package_no_parent.child"
        ) as mod:
            C = mod["C"]
            self.assertInBytecode(
                C.f, "CHECK_ARGS", (0, ("package_no_parent.child", "C"))
            )
            self.assertEqual(C().f(), 42)

    def test_direct_super_init(self):
        value = 42
        expected = value
        codestr = f"""
            class Obj:
                pass

            class C:
                def __init__(self, x: Obj):
                    pass

            class D:
                def __init__(self):
                    C.__init__(None)
        """
        with self.assertRaisesRegex(
            TypedSyntaxError,
            "type mismatch: None positional argument type mismatch foo.C",
        ):
            self.compile(codestr, StaticCodeGenerator, modname="foo")

    def test_class_unknown_attr(self):
        value = 42
        expected = value
        codestr = f"""
            class C:
                pass

            def f():
                return C.foo
        """
        with self.in_module(codestr) as mod:
            f = mod["f"]
            self.assertInBytecode(f, "LOAD_ATTR", "foo")

    def test_descriptor_access(self):
        value = 42
        expected = value
        codestr = f"""
            class Obj:
                abc: int

            class C:
                x: Obj

            def f():
                return C.x.abc
        """
        with self.in_module(codestr) as mod:
            f = mod["f"]
            self.assertInBytecode(f, "LOAD_ATTR", "abc")
            self.assertNotInBytecode(f, "LOAD_FIELD")

    @skipIf(not path.exists(RICHARDS_PATH), "richards not found")
    def test_richards(self):
        with open(RICHARDS_PATH) as f:
            codestr = f.read()

        with self.in_module(codestr) as mod:
            Richards = mod["Richards"]
            self.assertTrue(Richards().run(1))

    def test_unknown_isinstance_bool_ret(self):
        codestr = """
            from typing import Any

            class C:
                def __init__(self, x: str):
                    self.x: str = x

                def __eq__(self, other: Any) -> bool:
                    return isinstance(other, C)

        """
        with self.in_module(codestr, code_gen=StaticCodeGenerator) as mod:
            C = mod["C"]
            x = C("abc")
            y = C("foo")
            self.assertTrue(x == y)

    def test_unknown_issubclass_bool_ret(self):
        codestr = """
            from typing import Any

            class C:
                def __init__(self, x: str):
                    self.x: str = x

                def __eq__(self, other: Any) -> bool:
                    return issubclass(type(other), C)

        """
        with self.in_module(codestr, code_gen=StaticCodeGenerator) as mod:
            C = mod["C"]
            x = C("abc")
            y = C("foo")
            self.assertTrue(x == y)

    def test_unknown_isinstance_narrows(self):
        codestr = """
            from typing import Any

            class C:
                def __init__(self, x: str):
                    self.x: str = x

            def testfunc(x):
                if isinstance(x, C):
                    return x.x
        """
        with self.in_module(codestr, code_gen=StaticCodeGenerator) as mod:
            testfunc = mod["testfunc"]
            self.assertInBytecode(
                testfunc, "LOAD_FIELD", ("test_unknown_isinstance_narrows", "C", "x")
            )

    def test_unknown_isinstance_narrows_class_attr(self):
        codestr = """
            from typing import Any

            class C:
                def __init__(self, x: str):
                    self.x: str = x

                def f(self, other) -> str:
                    if isinstance(other, self.__class__):
                        return other.x
                    return ''
        """
        with self.in_module(codestr, code_gen=StaticCodeGenerator) as mod:
            C = mod["C"]
            self.assertInBytecode(
                C.f,
                "LOAD_FIELD",
                ("test_unknown_isinstance_narrows_class_attr", "C", "x"),
            )

    def test_unknown_isinstance_narrows_class_attr_dynamic(self):
        codestr = """
            from typing import Any

            class C:
                def __init__(self, x: str):
                    self.x: str = x

                def f(self, other, unknown):
                    if isinstance(other, unknown.__class__):
                        return other.x
                    return ''
        """
        with self.in_module(codestr, code_gen=StaticCodeGenerator) as mod:
            C = mod["C"]
            self.assertInBytecode(C.f, "LOAD_ATTR", "x")

    def test_unknown_isinstance_narrows_else_correct(self):
        codestr = """
            from typing import Any

            class C:
                def __init__(self, x: str):
                    self.x: str = x

            def testfunc(x):
                if isinstance(x, C):
                    pass
                else:
                    return x.x
        """
        with self.in_module(codestr, code_gen=StaticCodeGenerator) as mod:
            testfunc = mod["testfunc"]
            self.assertNotInBytecode(
                testfunc, "LOAD_FIELD", ("test_unknown_isinstance_narrows", "C", "x")
            )

    def test_narrow_while_break(self):
        codestr = """
            from typing import Optional
            def f(x: Optional[int]) -> int:
                while x is None:
                    break
                return x
        """
        with self.assertRaisesRegex(
            TypedSyntaxError,
            type_mismatch("Optional[int]", "int"),
        ):
            self.compile(codestr)

    def test_narrow_while_if_break_else_return(self):
        codestr = """
            from typing import Optional
            def f(x: Optional[int], y: int) -> int:
                while x is None:
                    if y > 0:
                        break
                    else:
                        return 42
                return x
        """
        with self.assertRaisesRegex(
            TypedSyntaxError,
            type_mismatch("Optional[int]", "int"),
        ):
            self.compile(codestr)

    def test_narrow_while_break_if(self):
        codestr = """
            from typing import Optional
            def f(x: Optional[int]) -> int:
                while True:
                    if x is None:
                        break
                    return x
        """
        self.compile(codestr)

    def test_narrow_while_continue_if(self):
        codestr = """
            from typing import Optional
            def f(x: Optional[int]) -> int:
                while True:
                    if x is None:
                        continue
                    return x
        """
        self.compile(codestr)

    def test_unknown_param_ann(self):
        codestr = """
            from typing import Any

            class C:
                def __init__(self, x: str):
                    self.x: str = x

                def __eq__(self, other: Any) -> bool:
                    return False

        """
        with self.in_module(codestr, code_gen=StaticCodeGenerator) as mod:
            C = mod["C"]
            x = C("abc")
            self.assertInBytecode(
                C.__eq__, "CHECK_ARGS", (0, ("test_unknown_param_ann", "C"))
            )
            self.assertNotEqual(x, x)

    def test_class_init_kw(self):
        codestr = """
            class C:
                def __init__(self, x: str):
                    self.x: str = x

            def f():
                x = C(x='abc')

        """
        with self.in_module(codestr, code_gen=StaticCodeGenerator) as mod:
            f = mod["f"]
            self.assertInBytecode(f, "CALL_FUNCTION_KW", 1)

    def test_ret_type_cast(self):
        codestr = """
            from typing import Any

            def testfunc(x: str, y: str) -> bool:
                return x == y
        """
        with self.in_module(codestr, code_gen=StaticCodeGenerator) as mod:
            f = mod["testfunc"]
            self.assertEqual(f("abc", "abc"), True)
            self.assertInBytecode(f, "CAST", ("builtins", "bool"))

    def test_bind_boolop_type(self):
        codestr = """
            from typing import Any

            class C:
                def f(self) -> bool:
                    return True

                def g(self) -> bool:
                    return False

                def x(self) -> bool:
                    return self.f() and self.g()

                def y(self) -> bool:
                    return self.f() or self.g()
        """
        with self.in_module(codestr, code_gen=StaticCodeGenerator) as mod:
            C = mod["C"]
            c = C()
            self.assertEqual(c.x(), False)
            self.assertEqual(c.y(), True)

    def test_decorated_function_ignored_class(self):
        codestr = """
            class C:
                @property
                def x(self):
                    return lambda: 42

                def y(self):
                    return self.x()

        """
        with self.in_module(codestr, code_gen=StaticCodeGenerator) as mod:
            C = mod["C"]
            self.assertNotInBytecode(C.y, "INVOKE_METHOD")
            self.assertEqual(C().y(), 42)

    def test_decorated_function_ignored(self):
        codestr = """
            class C: pass

            def mydecorator(x):
                return C

            @mydecorator
            def f():
                return 42

            def g():
                return f()

        """
        with self.in_module(codestr, code_gen=StaticCodeGenerator) as mod:
            C = mod["C"]
            g = mod["g"]
            self.assertNotInBytecode(g, "INVOKE_FUNCTION")
            self.assertEqual(type(g()), C)

    def test_static_function_invoke(self):
        codestr = """
            class C:
                @staticmethod
                def f():
                    return 42

            def f():
                return C.f()
        """
        with self.in_module(codestr, code_gen=StaticCodeGenerator) as mod:
            f = mod["f"]
            self.assertInBytecode(
                f, "INVOKE_FUNCTION", (("test_static_function_invoke", "C", "f"), 0)
            )
            self.assertEqual(f(), 42)

    def test_static_function_invoke_on_instance(self):
        codestr = """
            class C:
                @staticmethod
                def f():
                    return 42

            def f():
                return C().f()
        """
        with self.in_module(codestr, code_gen=StaticCodeGenerator) as mod:
            f = mod["f"]
            self.assertInBytecode(
                f,
                "INVOKE_FUNCTION",
                (("test_static_function_invoke_on_instance", "C", "f"), 0),
            )
            self.assertEqual(f(), 42)

    def test_spamobj_no_params(self):
        codestr = """
            from xxclassloader import spamobj

            def f():
                x = spamobj()
        """
        with self.assertRaisesRegex(
            TypedSyntaxError,
            r"cannot create instances of a generic Type\[xxclassloader.spamobj\[T\]\]",
        ):
            self.compile(codestr, StaticCodeGenerator, modname="foo")

    def test_spamobj_error(self):
        codestr = """
            from xxclassloader import spamobj

            def f():
                x = spamobj[int]()
                return x.error(1)
        """
        with self.in_module(codestr, code_gen=StaticCodeGenerator) as mod:
            f = mod["f"]
            with self.assertRaisesRegex(TypeError, "no way!"):
                f()

    def test_spamobj_no_error(self):
        codestr = """
            from xxclassloader import spamobj

            def testfunc():
                x = spamobj[int]()
                return x.error(0)
        """
        with self.in_module(codestr, code_gen=StaticCodeGenerator) as mod:
            f = mod["testfunc"]
            self.assertEqual(f(), None)

    def test_generic_type_box_box(self):
        codestr = """
            from xxclassloader import spamobj

            def testfunc():
                x = spamobj[str]()
                return (x.getint(), )
        """

        with self.assertRaisesRegex(
            TypedSyntaxError,
            "type mismatch: int64 is an invalid return type, expected dynamic",
        ):
            code = self.compile(codestr, StaticCodeGenerator, modname="foo")

    def test_generic_type(self):
        codestr = """
            from xxclassloader import spamobj
            from __static__ import box

            def testfunc():
                x = spamobj[str]()
                x.setstate('abc')
                x.setint(42)
                return (x.getstate(), box(x.getint()))
        """

        code = self.compile(codestr, StaticCodeGenerator, modname="foo")
        f = self.find_code(code, "testfunc")
        self.assertInBytecode(
            f,
            "INVOKE_METHOD",
            ((("xxclassloader", "spamobj", (("builtins", "str"),), "setstate"), 1)),
        )
        with self.in_module(codestr, code_gen=StaticCodeGenerator) as mod:
            test = mod["testfunc"]
            self.assertEqual(test(), ("abc", 42))

    def test_ret_void(self):
        codestr = """
            from xxclassloader import spamobj
            from __static__ import box

            def testfunc():
                x = spamobj[str]()
                y = x.setstate('abc')
                return y
        """

        code = self.compile(codestr, StaticCodeGenerator, modname="foo")
        f = self.find_code(code, "testfunc")
        self.assertInBytecode(
            f,
            "INVOKE_METHOD",
            ((("xxclassloader", "spamobj", (("builtins", "str"),), "setstate"), 1)),
        )
        with self.in_module(codestr, code_gen=StaticCodeGenerator) as mod:
            test = mod["testfunc"]
            self.assertEqual(test(), None)

    def test_user_enumerate_list(self):
        codestr = """
            from __static__ import int64, box, clen

            def f(x: list):
                i: int64 = 0
                res = []
                while i < clen(x):
                    elem = x[i]
                    res.append((box(i), elem))
                    i += 1
                return res
        """
        with self.in_module(codestr, code_gen=StaticCodeGenerator) as mod:
            f = mod["f"]
            self.assertInBytecode(f, "SEQUENCE_GET", SEQ_LIST_INEXACT)
            res = f([1, 2, 3])
            self.assertEqual(res, [(0, 1), (1, 2), (2, 3)])

    def test_user_enumerate_list_nooverride(self):
        class mylist(list):
            pass

        codestr = """
            from __static__ import int64, box, clen

            def f(x: list):
                i: int64 = 0
                res = []
                while i < clen(x):
                    elem = x[i]
                    res.append((box(i), elem))
                    i += 1
                return res
        """
        with self.in_module(codestr, code_gen=StaticCodeGenerator) as mod:
            f = mod["f"]
            self.assertInBytecode(f, "SEQUENCE_GET", SEQ_LIST_INEXACT)
            res = f(mylist([1, 2, 3]))
            self.assertEqual(res, [(0, 1), (1, 2), (2, 3)])

    def test_user_enumerate_list_subclass(self):
        class mylist(list):
            def __getitem__(self, idx):
                return list.__getitem__(self, idx) + 1

        codestr = """
            from __static__ import int64, box, clen

            def f(x: list):
                i: int64 = 0
                res = []
                while i < clen(x):
                    elem = x[i]
                    res.append((box(i), elem))
                    i += 1
                return res
        """
        with self.in_module(codestr, code_gen=StaticCodeGenerator) as mod:
            f = mod["f"]
            self.assertInBytecode(f, "SEQUENCE_GET", SEQ_LIST_INEXACT)
            res = f(mylist([1, 2, 3]))
            self.assertEqual(res, [(0, 2), (1, 3), (2, 4)])

    def test_list_assign_subclass(self):
        class mylist(list):
            def __setitem__(self, idx, value):
                return list.__setitem__(self, idx, value + 1)

        codestr = """
            from __static__ import int64, box, clen

            def f(x: list):
                i: int64 = 0
                x[i] = 42
        """
        with self.in_module(codestr, code_gen=StaticCodeGenerator) as mod:
            f = mod["f"]
            self.assertInBytecode(f, "SEQUENCE_SET", SEQ_LIST_INEXACT)
            l = mylist([0])
            f(l)
            self.assertEqual(l[0], 43)

    def test_inexact_list_negative(self):
        codestr = """
            from __static__ import int64, box, clen

            def f(x: list):
                i: int64 = 1
                return x[-i]
        """
        with self.in_module(codestr, code_gen=StaticCodeGenerator) as mod:
            f = mod["f"]
            self.assertInBytecode(f, "SEQUENCE_GET", SEQ_LIST_INEXACT)
            res = f([1, 2, 3])
            self.assertEqual(res, 3)

    def test_inexact_list_negative_small_int(self):
        codestr = """
            from __static__ import int64, box, clen

            def f(x: list):
                i: int8 = 1
                return x[-i]
        """
        with self.in_module(codestr, code_gen=StaticCodeGenerator) as mod:
            f = mod["f"]
            res = f([1, 2, 3])
            self.assertEqual(res, 3)

    def test_inexact_list_large_unsigned(self):
        codestr = """
            from __static__ import uint64
            def f(x: list):
                i: uint64 = 0xffffffffffffffff
                return x[i]
        """
        with self.assertRaisesRegex(
            TypedSyntaxError, "type mismatch: uint64 cannot be assigned to dynamic"
        ):
            self.compile(codestr)

    def test_named_tuple(self):
        codestr = """
            from typing import NamedTuple

            class C(NamedTuple):
                x: int
                y: str

            def myfunc(x: C):
                return x.x
        """
        with self.in_module(codestr, code_gen=StaticCodeGenerator) as mod:
            f = mod["myfunc"]
            self.assertNotInBytecode(f, "LOAD_FIELD")

    def test_generic_type_error(self):
        codestr = """
            from xxclassloader import spamobj

            def testfunc():
                x = spamobj[str]()
                x.setstate(42)
        """

        with self.assertRaisesRegex(
            TypedSyntaxError,
            "type mismatch: Exact\\[int\\] positional argument type mismatch str",
        ):
            code = self.compile(codestr, StaticCodeGenerator, modname="foo")

    def test_generic_optional_type_param(self):
        codestr = """
            from xxclassloader import spamobj

            def testfunc():
                x = spamobj[str]()
                x.setstateoptional(None)
        """

        code = self.compile(codestr, StaticCodeGenerator, modname="foo")

    def test_generic_optional_type_param_2(self):
        codestr = """
            from xxclassloader import spamobj

            def testfunc():
                x = spamobj[str]()
                x.setstateoptional('abc')
        """

        code = self.compile(codestr, StaticCodeGenerator, modname="foo")

    def test_generic_optional_type_param_error(self):
        codestr = """
            from xxclassloader import spamobj

            def testfunc():
                x = spamobj[str]()
                x.setstateoptional(42)
        """

        with self.assertRaisesRegex(
            TypedSyntaxError,
            "type mismatch: Exact\\[int\\] positional argument type mismatch Optional\\[str\\]",
        ):
            code = self.compile(codestr, StaticCodeGenerator, modname="foo")

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
        with self.in_module(codestr, code_gen=StaticCodeGenerator) as mod:
            test = mod["testfunc"]
            B = mod["B"]
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
            "type mismatch: Exact\\[int\\] positional argument type mismatch Optional\\[str\\]",
        ):
            self.compile(codestr, StaticCodeGenerator, modname="foo")

    def test_compile_dict_get(self):
        codestr = """
            from __static__ import CheckedDict
            def testfunc():
                x = CheckedDict[int, str]({42: 'abc', })
                x.get(42, 42)
        """
        with self.assertRaisesRegex(
            TypedSyntaxError,
            "type mismatch: Exact\\[int\\] positional argument type mismatch Optional\\[str\\]",
        ):
            self.compile(codestr, StaticCodeGenerator, modname="foo")

        codestr = """
            from __static__ import CheckedDict

            class B: pass
            class D(B): pass

            def testfunc():
                x = CheckedDict[B, int]({B():42, D():42})
                return x
        """
        with self.in_module(codestr, code_gen=StaticCodeGenerator) as mod:
            test = mod["testfunc"]
            B = mod["B"]
            self.assertEqual(type(test()), chkdict[B, int])

    def test_compile_dict_setitem(self):
        codestr = """
            from __static__ import CheckedDict

            def testfunc():
                x = CheckedDict[int, str]({1:'abc'})
                x.__setitem__(2, 'def')
                return x
        """
        with self.in_module(codestr, code_gen=StaticCodeGenerator) as mod:
            test = mod["testfunc"]
            x = test()
            self.assertInBytecode(
                test,
                "INVOKE_FUNCTION",
                (
                    (
                        "__static__",
                        "chkdict",
                        (("builtins", "int"), ("builtins", "str")),
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
        with self.in_module(codestr, code_gen=StaticCodeGenerator) as mod:
            test = mod["testfunc"]
            x = test()
            self.assertInBytecode(
                test,
                "INVOKE_METHOD",
                (
                    (
                        "__static__",
                        "chkdict",
                        (("builtins", "int"), ("builtins", "str")),
                        "__setitem__",
                    ),
                    2,
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
            type_mismatch("Exact[int]", "str"),
        ):
            self.compile(codestr, StaticCodeGenerator, modname="foo")

    def test_compile_generic_dict_setitem_bad_type(self):
        codestr = """
            from __static__ import CheckedDict

            def testfunc():
                x = CheckedDict[str, int]({"abc": 42})
                x[42] = 42
        """
        with self.assertRaisesRegex(
            TypedSyntaxError,
            type_mismatch("Exact[int]", "str"),
        ):
            self.compile(codestr, StaticCodeGenerator, modname="foo")

    def test_compile_generic_dict_setitem_bad_type_2(self):
        codestr = """
            from __static__ import CheckedDict

            def testfunc():
                x = CheckedDict[str, int]({"abc": 42})
                x["foo"] = "abc"
        """
        with self.assertRaisesRegex(
            TypedSyntaxError,
            type_mismatch("Exact[str]", "int"),
        ):
            self.compile(codestr, StaticCodeGenerator, modname="foo")

    def test_compile_checked_dict_shadowcode(self):
        codestr = """
            from __static__ import CheckedDict

            class B: pass
            class D(B): pass

            def testfunc():
                x = CheckedDict[B, int]({B():42, D():42})
                return x
        """
        with self.in_module(codestr, code_gen=StaticCodeGenerator) as mod:
            test = mod["testfunc"]
            B = mod["B"]
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
        with self.in_module(codestr, code_gen=StaticCodeGenerator) as mod:
            f = mod["testfunc"]
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
        with self.in_module(codestr, code_gen=StaticCodeGenerator) as mod:
            test = mod["testfunc"]
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
                "Exact[chkdict[str, str]]",
                "chkdict[int, int]",
            ),
        ):
            self.compile(codestr, StaticCodeGenerator, modname="foo")

    def test_compile_checked_dict_ann_differs_2(self):
        codestr = """
            from __static__ import CheckedDict

            def testfunc():
                x: int = CheckedDict[str, str]({'abc':'abc'})
                return x
        """
        with self.assertRaisesRegex(
            TypedSyntaxError,
            type_mismatch("Exact[chkdict[str, str]]", "int"),
        ):
            self.compile(codestr, StaticCodeGenerator, modname="foo")

    def test_compile_checked_dict_opt_out(self):
        codestr = """
            from __static__.compiler_flags import nonchecked_dicts
            class B: pass
            class D(B): pass

            def testfunc():
                x = {B():42, D():42}
                return x
        """
        with self.in_module(codestr, code_gen=StaticCodeGenerator) as mod:
            test = mod["testfunc"]
            B = mod["B"]
            self.assertEqual(type(test()), dict)

    def test_compile_checked_dict_explicit_dict(self):
        codestr = """
            from __static__ import pydict
            class B: pass
            class D(B): pass

            def testfunc():
                x: pydict = {B():42, D():42}
                return x
        """
        with self.in_module(codestr, code_gen=StaticCodeGenerator) as mod:
            test = mod["testfunc"]
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
        with self.in_module(codestr, code_gen=StaticCodeGenerator) as mod:
            test = mod["testfunc"]
            B = mod["B"]
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
        with self.in_module(codestr, code_gen=StaticCodeGenerator) as mod:
            test = mod["testfunc"]
            B = mod["B"]
            self.assertEqual(type(test()), chkdict[B, int])

    def test_compile_checked_dict_with_annotation_comprehension(self):
        codestr = """
            from __static__ import CheckedDict

            def testfunc():
                x: CheckedDict[int, object] = {int(i): object() for i in range(1, 5)}
                return x
        """
        with self.in_module(codestr, code_gen=StaticCodeGenerator) as mod:
            test = mod["testfunc"]
            self.assertEqual(type(test()), chkdict[int, object])

    def test_compile_checked_dict_with_annotation(self):
        codestr = """
            from __static__ import CheckedDict

            class B: pass

            def testfunc():
                x: CheckedDict[B, int] = {B():42}
                return x
        """
        with self.in_module(codestr, code_gen=StaticCodeGenerator) as mod:
            test = mod["testfunc"]
            B = mod["B"]
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
                "Exact[chkdict[foo.B, Exact[str]]]",
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
                "Exact[chkdict[object, Exact[int]]]",
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
            TypedSyntaxError, "positional argument type mismatch"
        ):
            self.compile(codestr, StaticCodeGenerator, modname="foo")

    def test_compile_checked_dict_opt_out_dict_call(self):
        codestr = """
            from __static__.compiler_flags import nonchecked_dicts

            def testfunc():
                x = dict(x=42)
                return x
        """
        with self.in_module(codestr, code_gen=StaticCodeGenerator) as mod:
            test = mod["testfunc"]
            self.assertEqual(type(test()), dict)

    def test_compile_checked_dict_explicit_dict_as_dict(self):
        codestr = """
            from __static__ import pydict as dict
            class B: pass
            class D(B): pass

            def testfunc():
                x: dict = {B():42, D():42}
                return x
        """
        with self.in_module(codestr, code_gen=StaticCodeGenerator) as mod:
            test = mod["testfunc"]
            self.assertEqual(type(test()), dict)

    def test_compile_checked_dict_from_dict_call(self):
        codestr = """
            def testfunc():
                x = dict(x=42)
                return x
        """
        with self.assertRaisesRegex(
            TypeError, "cannot create 'dict\\[K, V\\]' instances"
        ):
            with self.in_module(codestr, code_gen=StaticCodeGenerator) as mod:
                test = mod["testfunc"]
                test()

    def test_compile_checked_dict_from_dict_call_2(self):
        codestr = """
            def testfunc():
                x = dict[str, int](x=42)
                return x
        """
        with self.in_module(codestr, code_gen=StaticCodeGenerator) as mod:
            test = mod["testfunc"]
            self.assertEqual(type(test()), chkdict[str, int])

    def test_compile_checked_dict_from_dict_call_3(self):
        # we emit the chkdict import first before future annotations, but that
        # should be fine as we're the compiler.
        codestr = """
            from __future__ import annotations

            def testfunc():
                x = dict[str, int](x=42)
                return x
        """
        with self.in_module(codestr, code_gen=StaticCodeGenerator) as mod:
            test = mod["testfunc"]
            self.assertEqual(type(test()), chkdict[str, int])

    def test_patch_function(self):
        codestr = """
            def f():
                return 42

            def g():
                return f()
        """
        with self.in_module(codestr, code_gen=StaticCodeGenerator) as mod:
            g = mod["g"]
            for i in range(100):
                g()
            with patch("test_patch_function.f", autospec=True, return_value=100) as p:
                self.assertEqual(g(), 100)

    def test_self_patching_function(self):
        codestr = """
            def x(d, d2=1): pass
            def removeit(d):
                global f
                f = x

            def f(d):
                if d:
                    removeit(d)
                return 42

            def g(d):
                return f(d)
        """
        with self.in_module(codestr, code_gen=StaticCodeGenerator) as mod:
            g = mod["g"]
            f = mod["f"]
            import weakref

            wr = weakref.ref(f, lambda *args: self.assertEqual(i, -1))
            del f
            for i in range(100):
                g(False)
            i = -1
            self.assertEqual(g(True), 42)
            i = 0
            self.assertEqual(g(True), None)

    def test_patch_function_unwatchable_dict(self):
        codestr = """
            def f():
                return 42

            def g():
                return f()
        """
        with self.in_module(codestr, code_gen=StaticCodeGenerator) as mod:
            g = mod["g"]
            for i in range(100):
                g()
            with patch(
                "test_patch_function_unwatchable_dict.f",
                autospec=True,
                return_value=100,
            ) as p:
                mod[42] = 1
                self.assertEqual(g(), 100)

    def test_patch_function_deleted_func(self):
        codestr = """
            def f():
                return 42

            def g():
                return f()
        """
        with self.in_module(codestr, code_gen=StaticCodeGenerator) as mod:
            g = mod["g"]
            for i in range(100):
                g()
            del mod["f"]
            with self.assertRaisesRegex(
                TypeError,
                re.escape("unknown function ('test_patch_function_deleted_func', 'f')"),
            ):
                g()

    def test_patch_static_function(self):
        codestr = """
            class C:
                @staticmethod
                def f():
                    return 42

            def g():
                return C.f()
        """
        with self.in_module(codestr, code_gen=StaticCodeGenerator) as mod:
            g = mod["g"]
            for i in range(100):
                self.assertEqual(g(), 42)
            with patch(
                "test_patch_static_function.C.f", autospec=True, return_value=100
            ) as p:
                self.assertEqual(g(), 100)

    def test_patch_static_function_non_autospec(self):
        codestr = """
            class C:
                @staticmethod
                def f():
                    return 42

            def g():
                return C.f()
        """
        with self.in_module(codestr, code_gen=StaticCodeGenerator) as mod:
            g = mod["g"]
            for i in range(100):
                g()
            with patch(
                "test_patch_static_function_non_autospec.C.f", return_value=100
            ) as p:
                self.assertEqual(g(), 100)

    def test_invoke_frozen_type(self):
        codestr = """
            from cinder import freeze_type

            @freeze_type
            class C:
                @staticmethod
                def f():
                    return 42

            def g():
                return C.f()
        """
        with self.in_module(codestr, code_gen=StaticCodeGenerator) as mod:
            g = mod["g"]
            for i in range(100):
                self.assertEqual(g(), 42)

    def test_invoke_strict_module(self):
        codestr = """
            def f():
                return 42

            def g():
                return f()
        """
        with self.in_strict_module(codestr) as mod:
            g = mod.g
            for i in range(100):
                self.assertEqual(g(), 42)
            self.assertInBytecode(
                g, "INVOKE_FUNCTION", (("test_invoke_strict_module", "f"), 0)
            )

    def test_invoke_with_cell(self):
        codestr = """
            def f(l: list):
                x = 2
                return [x + y for y in l]

            def g():
                return f([1,2,3])
        """
        with self.in_strict_module(codestr) as mod:
            g = mod.g
            self.assertEqual(g(), [3, 4, 5])
            self.assertInBytecode(
                g, "INVOKE_FUNCTION", (("test_invoke_with_cell", "f"), 1)
            )

    def test_invoke_with_cell_arg(self):
        codestr = """
            def f(l: list, x: int):
                return [x + y for y in l]

            def g():
                return f([1,2,3], 2)
        """
        with self.in_strict_module(codestr) as mod:
            g = mod.g
            self.assertEqual(g(), [3, 4, 5])
            self.assertInBytecode(
                g, "INVOKE_FUNCTION", (("test_invoke_with_cell_arg", "f"), 2)
            )

    def test_invoke_all_reg_args(self):
        codestr = """
            def target(a, b, c, d, e, f):
                return a * 2 + b * 3 + c * 4 + d * 5 + e * 6 + f * 7

            def testfunc():
                return target(1,2,3,4,5,6)
        """
        with self.in_strict_module(codestr) as mod:
            f = mod.testfunc
            self.assertInBytecode(
                f,
                "INVOKE_FUNCTION",
                (("test_invoke_all_reg_args", "target"), 6),
            )
            self.assertEqual(f(), 112)

    def test_invoke_all_extra_args(self):
        codestr = """
            def target(a, b, c, d, e, f, g):
                return a * 2 + b * 3 + c * 4 + d * 5 + e * 6 + f * 7 + g

            def testfunc():
                return target(1,2,3,4,5,6,7)
        """
        with self.in_strict_module(codestr) as mod:
            f = mod.testfunc
            self.assertInBytecode(
                f,
                "INVOKE_FUNCTION",
                (("test_invoke_all_extra_args", "target"), 7),
            )
            self.assertEqual(f(), 119)

    def test_invoke_strict_module_deep(self):
        codestr = """
            def f0(): return 42
            def f1(): return f0()
            def f2(): return f1()
            def f3(): return f2()
            def f4(): return f3()
            def f5(): return f4()
            def f6(): return f5()
            def f7(): return f6()
            def f8(): return f7()
            def f9(): return f8()
            def f10(): return f9()
            def f11(): return f10()

            def g():
                return f11()
        """
        with self.in_strict_module(codestr) as mod:
            g = mod.g
            self.assertEqual(g(), 42)
            self.assertEqual(g(), 42)
            self.assertInBytecode(
                g, "INVOKE_FUNCTION", (("test_invoke_strict_module_deep", "f11"), 0)
            )

    def test_invoke_strict_module_deep_unjitable(self):
        codestr = """
            def f0(): return 42
            def f1():
                from sys import *
                return f0()
            def f2(): return f1()
            def f3(): return f2()
            def f4(): return f3()
            def f5(): return f4()
            def f6(): return f5()
            def f7(): return f6()
            def f8(): return f7()
            def f9(): return f8()
            def f10(): return f9()
            def f11(): return f10()

            def g(x):
                if x: return 0

                return f11()
        """
        with self.in_strict_module(codestr) as mod:
            g = mod.g
            self.assertEqual(g(True), 0)
            # we should have done some level of pre-jitting
            self.assert_not_jitted(mod.f2)
            self.assert_not_jitted(mod.f1)
            self.assert_not_jitted(mod.f0)
            [self.assert_jitted(getattr(mod, f"f{i}")) for i in range(3, 12)]
            self.assertEqual(g(False), 42)
            self.assertInBytecode(
                g,
                "INVOKE_FUNCTION",
                (("test_invoke_strict_module_deep_unjitable", "f11"), 0),
            )

    def test_invoke_strict_module_deep_unjitable_many_args(self):
        codestr = """
            def f0(): return 42
            def f1(a, b, c, d, e, f, g, h):
                from sys import *
                return f0() - a + b - c + d - e + f - g + h - 4

            def f2(): return f1(1,2,3,4,5,6,7,8)
            def f3(): return f2()
            def f4(): return f3()
            def f5(): return f4()
            def f6(): return f5()
            def f7(): return f6()
            def f8(): return f7()
            def f9(): return f8()
            def f10(): return f9()
            def f11(): return f10()

            def g():
                return f11()
        """
        with self.in_strict_module(codestr) as mod:
            g = mod.g
            f1 = mod.f1
            self.assertEqual(g(), 42)
            self.assertEqual(g(), 42)
            self.assertInBytecode(
                g,
                "INVOKE_FUNCTION",
                (("test_invoke_strict_module_deep_unjitable_many_args", "f11"), 0),
            )
            self.assert_not_jitted(f1)

    def test_invoke_strict_module_recursive(self):
        codestr = """
            def fib(number):
                if number <= 1:
                    return number
                return(fib(number-1) + fib(number-2))
        """
        with self.in_strict_module(codestr) as mod:
            fib = mod.fib
            self.assertInBytecode(
                fib,
                "INVOKE_FUNCTION",
                (("test_invoke_strict_module_recursive", "fib"), 1),
            )
            self.assertEqual(fib(4), 3)

    def test_invoke_strict_module_mutual_recursive(self):
        codestr = """
            def fib1(number):
                if number <= 1:
                    return number
                return(fib(number-1) + fib(number-2))

            def fib(number):
                if number <= 1:
                    return number
                return(fib1(number-1) + fib1(number-2))
        """
        with self.in_strict_module(codestr) as mod:
            fib = mod.fib
            fib1 = mod.fib1
            self.assertInBytecode(
                fib,
                "INVOKE_FUNCTION",
                (("test_invoke_strict_module_mutual_recursive", "fib1"), 1),
            )
            self.assertInBytecode(
                fib1,
                "INVOKE_FUNCTION",
                (("test_invoke_strict_module_mutual_recursive", "fib"), 1),
            )
            self.assertEqual(fib(0), 0)
            self.assert_jitted(fib1)
            self.assertEqual(fib(4), 3)

    def test_invoke_strict_module_pre_invoked(self):
        codestr = """
            def f():
                return 42

            def g():
                return f()
        """
        with self.in_strict_module(codestr) as mod:
            self.assertEqual(mod.f(), 42)
            self.assert_jitted(mod.f)
            g = mod.g
            self.assertEqual(g(), 42)
            self.assertInBytecode(
                g,
                "INVOKE_FUNCTION",
                (("test_invoke_strict_module_pre_invoked", "f"), 0),
            )

    def test_invoke_strict_module_patching(self):
        codestr = """
            def f():
                return 42

            def g():
                return f()
        """
        with self.in_strict_module(codestr, enable_patching=True) as mod:
            g = mod.g
            for i in range(100):
                self.assertEqual(g(), 42)
            self.assertInBytecode(
                g, "INVOKE_FUNCTION", (("test_invoke_strict_module_patching", "f"), 0)
            )
            mod.patch("f", lambda: 100)
            self.assertEqual(g(), 100)

    def test_invoke_patch_non_vectorcall(self):
        codestr = """
            def f():
                return 42

            def g():
                return f()
        """
        with self.in_strict_module(codestr, enable_patching=True) as mod:
            g = mod.g
            self.assertInBytecode(
                g, "INVOKE_FUNCTION", (("test_invoke_patch_non_vectorcall", "f"), 0)
            )
            self.assertEqual(g(), 42)
            mod.patch("f", Mock(return_value=100))
            self.assertEqual(g(), 100)

    def test_patch_method(self):
        codestr = """
            class C:
                def f(self):
                    pass

            def g():
                return C().f()
        """
        with self.in_module(codestr, code_gen=StaticCodeGenerator) as mod:
            g = mod["g"]
            C = mod["C"]
            orig = C.f
            C.f = lambda *args: args
            for i in range(100):
                v = g()
                self.assertEqual(type(v), tuple)
                self.assertEqual(type(v[0]), C)
            C.f = orig
            self.assertEqual(g(), None)

    def test_patch_method_ret_none_error(self):
        codestr = """
            class C:
                def f(self) -> None:
                    pass

            def g():
                return C().f()
        """
        with self.in_module(codestr, code_gen=StaticCodeGenerator) as mod:
            g = mod["g"]
            C = mod["C"]
            C.f = lambda *args: args
            with self.assertRaisesRegex(
                TypeError,
                "unexpected return type from C.f, expected NoneType, got tuple",
            ):
                v = g()

    def test_patch_method_ret_none(self):
        codestr = """
            class C:
                def f(self) -> None:
                    pass

            def g():
                return C().f()
        """
        with self.in_module(codestr, code_gen=StaticCodeGenerator) as mod:
            g = mod["g"]
            C = mod["C"]
            C.f = lambda *args: None
            self.assertEqual(g(), None)

    def test_patch_method_bad_ret(self):
        codestr = """
            class C:
                def f(self) -> int:
                    return 42

            def g():
                return C().f()
        """
        with self.in_module(codestr, code_gen=StaticCodeGenerator) as mod:
            g = mod["g"]
            C = mod["C"]
            C.f = lambda *args: "abc"
            with self.assertRaisesRegex(
                TypeError, "unexpected return type from C.f, expected int, got str"
            ):
                v = g()

    def test_primitive_args_funcdef(self):
        codestr = """
            from __static__ import int8, box

            def n(val: int8):
                return box(val)

            def x():
                y: int8 = 42
                return n(y)
        """
        with self.in_strict_module(codestr) as mod:
            n = mod.n
            x = mod.x
            self.assertEqual(x(), 42)
            self.assertEqual(mod.n(-128), -128)
            self.assertEqual(mod.n(127), 127)
            with self.assertRaises(OverflowError):
                print(mod.n(-129))
            with self.assertRaises(OverflowError):
                print(mod.n(128))

    def test_primitive_args_funcdef_unjitable(self):
        codestr = """
            from __static__ import int8, box

            def n(val: int8):
                try:
                    from sys import *
                except:
                    pass
                return box(val)

            def x():
                y: int8 = 42
                return n(y)
        """
        with self.in_strict_module(codestr) as mod:
            n = mod.n
            x = mod.x
            self.assertEqual(x(), 42)
            self.assertEqual(mod.n(-128), -128)
            self.assertEqual(mod.n(127), 127)
            with self.assertRaises(OverflowError):
                print(mod.n(-129))
            with self.assertRaises(OverflowError):
                print(mod.n(128))

    def test_primitive_args_funcdef_too_many_args(self):
        codestr = """
            from __static__ import int8, box

            def n(x: int8):
                return box(x)
        """
        with self.in_strict_module(codestr) as mod:
            n = mod.n
            with self.assertRaises(TypeError):
                print(mod.n(-128, x=2))
            with self.assertRaises(TypeError):
                print(mod.n(-128, 2))

    def test_primitive_args_funcdef_missing_starargs(self):
        codestr = """
            from __static__ import int8, box

            def x(val: int8, *foo):
                return box(val), foo
            def y(val: int8, **foo):
                return box(val), foo
        """
        with self.in_strict_module(codestr) as mod:
            self.assertEqual(mod.x(-128), (-128, ()))
            self.assertEqual(mod.y(-128), (-128, {}))

    def test_primitive_args_many_args(self):
        codestr = """
            from __static__ import int8, int16, int32, int64, uint8, uint16, uint32, uint64, box

            def x(i8: int8, i16: int16, i32: int32, i64: int64, u8: uint8, u16: uint16, u32: uint32, u64: uint64):
                return box(i8), box(i16), box(i32), box(i64), box(u8), box(u16), box(u32), box(u64)

            def y():
                return x(1,2,3,4,5,6,7,8)
        """
        with self.in_strict_module(codestr) as mod:
            self.assertInBytecode(
                mod.y, "INVOKE_FUNCTION", (("test_primitive_args_many_args", "x"), 8)
            )
            self.assertEqual(mod.y(), (1, 2, 3, 4, 5, 6, 7, 8))
            self.assertEqual(mod.x(1, 2, 3, 4, 5, 6, 7, 8), (1, 2, 3, 4, 5, 6, 7, 8))

    def test_primitive_args_sizes(self):
        cases = [
            ("cbool", True, False),
            ("cbool", False, False),
            ("int8", (1 << 7), True),
            ("int8", (-1 << 7) - 1, True),
            ("int8", -1 << 7, False),
            ("int8", (1 << 7) - 1, False),
            ("int16", (1 << 15), True),
            ("int16", (-1 << 15) - 1, True),
            ("int16", -1 << 15, False),
            ("int16", (1 << 15) - 1, False),
            ("int32", (1 << 31), True),
            ("int32", (-1 << 31) - 1, True),
            ("int32", -1 << 31, False),
            ("int32", (1 << 31) - 1, False),
            ("int64", (1 << 63), True),
            ("int64", (-1 << 63) - 1, True),
            ("int64", -1 << 63, False),
            ("int64", (1 << 63) - 1, False),
            ("uint8", (1 << 8), True),
            ("uint8", -1, True),
            ("uint8", (1 << 8) - 1, False),
            ("uint8", 0, False),
            ("uint16", (1 << 16), True),
            ("uint16", -1, True),
            ("uint16", (1 << 16) - 1, False),
            ("uint16", 0, False),
            ("uint32", (1 << 32), True),
            ("uint32", -1, True),
            ("uint32", (1 << 32) - 1, False),
            ("uint32", 0, False),
            ("uint64", (1 << 64), True),
            ("uint64", -1, True),
            ("uint64", (1 << 64) - 1, False),
            ("uint64", 0, False),
        ]
        for type, val, overflows in cases:
            codestr = f"""
                from __static__ import {type}, box

                def x(val: {type}):
                    return box(val)
            """
            with self.subTest(type=type, val=val, overflows=overflows):
                with self.in_strict_module(codestr) as mod:
                    if overflows:
                        with self.assertRaises(OverflowError):
                            mod.x(val)
                    else:
                        self.assertEqual(mod.x(val), val)

    def test_primitive_args_funcdef_missing_kw_call(self):
        codestr = """
            from __static__ import int8, box

            def testfunc(x: int8, foo):
                return box(x), foo
        """
        with self.in_strict_module(codestr) as mod:
            self.assertEqual(mod.testfunc(-128, foo=42), (-128, 42))

    def test_primitive_args_funccall(self):
        codestr = """
            from __static__ import int8

            def f(foo):
                pass

            def n() -> int:
                x: int8 = 3
                return f(x)
        """
        with self.assertRaisesRegex(
            TypedSyntaxError,
            "type mismatch: int8 positional argument type mismatch dynamic",
        ):
            self.compile(codestr, StaticCodeGenerator, modname="foo.py")

    def test_primitive_args_funccall_int(self):
        codestr = """
            from __static__ import int8

            def f(foo: int):
                pass

            def n() -> int:
                x: int8 = 3
                return f(x)
        """
        with self.assertRaisesRegex(
            TypedSyntaxError,
            "type mismatch: int8 positional argument type mismatch int",
        ):
            self.compile(codestr, StaticCodeGenerator, modname="foo.py")

    def test_primitive_args_typecall(self):
        codestr = """
            from __static__ import int8

            def n() -> int:
                x: int8 = 3
                return int(x)
        """
        with self.assertRaisesRegex(
            TypedSyntaxError, "Call argument cannot be a primitive"
        ):
            self.compile(codestr, StaticCodeGenerator, modname="foo.py")

    def test_primitive_args_typecall_kwarg(self):
        codestr = """
            from __static__ import int8

            def n() -> int:
                x: int8 = 3
                return dict(a=x)
        """
        with self.assertRaisesRegex(
            TypedSyntaxError, "Call argument cannot be a primitive"
        ):
            self.compile(codestr, StaticCodeGenerator, modname="foo.py")

    def test_primitive_args_nonstrict(self):
        codestr = """
            from __static__ import int8, int16, box

            def f(x: int8, y: int16) -> int16:
                return x + y

            def g() -> int:
                return box(f(1, 300))
        """
        with self.in_module(codestr) as mod:
            self.assertEqual(mod["g"](), 301)

    def test_primitive_args_and_return(self):
        cases = [
            ("cbool", 1),
            ("cbool", 0),
            ("int8", -1 << 7),
            ("int8", (1 << 7) - 1),
            ("int16", -1 << 15),
            ("int16", (1 << 15) - 1),
            ("int32", -1 << 31),
            ("int32", (1 << 31) - 1),
            ("int64", -1 << 63),
            ("int64", (1 << 63) - 1),
            ("uint8", (1 << 8) - 1),
            ("uint8", 0),
            ("uint16", (1 << 16) - 1),
            ("uint16", 0),
            ("uint32", (1 << 32) - 1),
            ("uint32", 0),
            ("uint64", (1 << 64) - 1),
            ("uint64", 0),
        ]
        for typ, val in cases:
            if typ == "cbool":
                op = "or"
                expected = True
                other = "cbool(True)"
                boxed = "bool"
            else:
                op = "+" if val <= 0 else "-"
                expected = val + (1 if op == "+" else -1)
                other = "1"
                boxed = "int"
            with self.subTest(typ=typ, val=val, op=op, expected=expected):
                codestr = f"""
                    from __static__ import {typ}, box

                    def f(x: {typ}, y: {typ}) -> {typ}:
                        return x {op} y

                    def g() -> {boxed}:
                        return box(f({val}, {other}))
                """
                with self.in_strict_module(codestr) as mod:
                    self.assertEqual(mod.g(), expected)

    def test_primitive_return(self):
        cases = [
            ("cbool", True),
            ("cbool", False),
            ("int8", -1 << 7),
            ("int8", (1 << 7) - 1),
            ("int16", -1 << 15),
            ("int16", (1 << 15) - 1),
            ("int32", -1 << 31),
            ("int32", (1 << 31) - 1),
            ("int64", -1 << 63),
            ("int64", (1 << 63) - 1),
            ("uint8", (1 << 8) - 1),
            ("uint8", 0),
            ("uint16", (1 << 16) - 1),
            ("uint16", 0),
            ("uint32", (1 << 32) - 1),
            ("uint32", 0),
            ("uint64", (1 << 64) - 1),
            ("uint64", 0),
        ]
        tf = [True, False]
        for (type, val), box, strict, error, unjitable in itertools.product(
            cases, [False], [True], [False], [True]
        ):
            if type == "cbool":
                op = "or"
                other = "False"
                boxed = "bool"
            else:
                op = "*"
                other = "1"
                boxed = "int"
            unjitable_code = "from sys import *" if unjitable else ""
            codestr = f"""
                from __static__ import {type}, box

                def f(error: bool) -> {type}:
                    {unjitable_code}
                    if error:
                        raise RuntimeError("boom")
                    return {val}
            """
            if box:
                codestr += f"""

                def g() -> {boxed}:
                    return box(f({error}) {op} {type}({other}))
                """
            else:
                codestr += f"""

                def g() -> {type}:
                    return f({error}) {op} {type}({other})
                """
            ctx = self.in_strict_module if strict else self.in_module
            oparg = PRIM_NAME_TO_TYPE[type]
            with self.subTest(
                type=type,
                val=val,
                strict=strict,
                box=box,
                error=error,
                unjitable=unjitable,
            ):
                with ctx(codestr) as mod:
                    f = mod.f if strict else mod["f"]
                    g = mod.g if strict else mod["g"]
                    self.assertInBytecode(f, "RETURN_INT", oparg)
                    if box:
                        self.assertNotInBytecode(g, "RETURN_INT")
                    else:
                        self.assertInBytecode(g, "RETURN_INT", oparg)
                    if error:
                        with self.assertRaisesRegex(RuntimeError, "boom"):
                            g()
                    else:
                        self.assertEqual(g(), val)
                    self.assert_jitted(g)
                    if unjitable:
                        self.assert_not_jitted(f)
                    else:
                        self.assert_jitted(f)

    def test_primitive_return_recursive(self):
        codestr = """
            from __static__ import int32

            def fib(n: int32) -> int32:
                if n <= 1:
                    return n
                return fib(n-1) + fib(n-2)
        """
        with self.in_strict_module(codestr) as mod:
            self.assertInBytecode(
                mod.fib,
                "INVOKE_FUNCTION",
                (("test_primitive_return_recursive", "fib"), 1),
            )
            self.assertEqual(mod.fib(2), 1)
            self.assert_jitted(mod.fib)

    def test_primitive_return_unannotated(self):
        codestr = """
            from __static__ import int32

            def f():
                x: int32 = 1
                return x
        """
        with self.assertRaisesRegex(
            TypedSyntaxError, "type mismatch: int32 cannot be assigned to dynamic"
        ):
            self.compile(codestr)

    def test_module_level_final_decl(self):
        codestr = """
        from typing import Final

        x: Final
        """
        with self.assertRaisesRegex(
            TypedSyntaxError, "Must assign a value when declaring a Final"
        ):
            self.compile(codestr, StaticCodeGenerator, modname="foo")

    def test_final_multiple_typeargs(self):
        codestr = """
        from typing import Final
        from something import hello

        x: Final[int, str] = hello()
        """
        with self.assertRaisesRegex(
            TypedSyntaxError, "Final types can only have a single type arg"
        ):
            self.compile(codestr, StaticCodeGenerator, modname="foo")

    def test_final_annotation_nesting(self):
        with self.assertRaisesRegex(
            TypedSyntaxError, "Final annotation is only valid in initial declaration"
        ):
            self.compile(
                """
                from typing import Final, List

                x: List[Final[str]] = []
                """,
                StaticCodeGenerator,
                modname="foo",
            )

        with self.assertRaisesRegex(
            TypedSyntaxError, "Final annotation is only valid in initial declaration"
        ):
            self.compile(
                """
                from typing import Final, List
                x: List[int | Final] = []
                """,
                StaticCodeGenerator,
                modname="foo",
            )

    def test_final(self):
        codestr = """
        from typing import Final

        x: Final = 0xdeadbeef
        """
        self.compile(codestr, StaticCodeGenerator, modname="foo")

    def test_final_generic(self):
        codestr = """
        from typing import Final

        x: Final[int] = 0xdeadbeef
        """
        self.compile(codestr, StaticCodeGenerator, modname="foo")

    def test_final_generic_types(self):
        codestr = """
        from typing import Final

        def g(i: int) -> int:
            return i

        def f() -> int:
            x: Final[int] = 0xdeadbeef
            return g(x)
        """
        self.compile(codestr, StaticCodeGenerator, modname="foo")

    def test_final_uninitialized(self):
        codestr = """
        from typing import Final

        x: Final
        """
        with self.assertRaisesRegex(
            TypedSyntaxError, "Must assign a value when declaring a Final"
        ):
            self.compile(codestr, StaticCodeGenerator, modname="foo")

    def test_final_reassign(self):
        codestr = """
        from typing import Final

        x: Final = 0xdeadbeef
        x = "something"
        """
        with self.assertRaisesRegex(
            TypedSyntaxError, "Cannot assign to a Final variable"
        ):
            self.compile(codestr, StaticCodeGenerator, modname="foo")

    def test_final_reassign_explicit_global(self):
        codestr = """
            from typing import Final

            a: Final = 1337

            def fn():
                def fn2():
                    global a
                    a = 0
        """
        with self.assertRaisesRegex(
            TypedSyntaxError, "Cannot assign to a Final variable"
        ):
            self.compile(codestr, StaticCodeGenerator, modname="foo")

    def test_final_reassign_explicit_global_shadowed(self):
        codestr = """
            from typing import Final

            a: Final = 1337

            def fn():
                a = 2
                def fn2():
                    global a
                    a = 0
        """
        with self.assertRaisesRegex(
            TypedSyntaxError, "Cannot assign to a Final variable"
        ):
            self.compile(codestr, StaticCodeGenerator, modname="foo")

    def test_final_reassign_nonlocal(self):
        codestr = """
            from typing import Final

            a: Final = 1337

            def fn():
                def fn2():
                    nonlocal a
                    a = 0
        """
        with self.assertRaisesRegex(
            TypedSyntaxError, "Cannot assign to a Final variable"
        ):
            self.compile(codestr, StaticCodeGenerator, modname="foo")

    def test_final_reassign_nonlocal_shadowed(self):
        codestr = """
            from typing import Final

            a: Final = 1337

            def fn():
                a = 3
                def fn2():
                    nonlocal a
                    # should be allowed, we're assigning to the shadowed
                    # value
                    a = 0
        """
        self.compile(codestr, StaticCodeGenerator, modname="foo")

    def test_final_reassigned_in_tuple(self):
        codestr = """
        from typing import Final

        x: Final = 0xdeadbeef
        y = 3
        x, y = 4, 5
        """
        with self.assertRaisesRegex(
            TypedSyntaxError, "Cannot assign to a Final variable"
        ):
            self.compile(codestr, StaticCodeGenerator, modname="foo")

    def test_final_reassigned_in_loop(self):
        codestr = """
        from typing import Final

        x: Final = 0xdeadbeef

        for x in [1, 3, 5]:
            pass
        """
        with self.assertRaisesRegex(
            TypedSyntaxError, "Cannot assign to a Final variable"
        ):
            self.compile(codestr, StaticCodeGenerator, modname="foo")

    def test_final_reassigned_in_except(self):
        codestr = """
        from typing import Final

        def f():
            e: Final = 3
            try:
                x = 1 + "2"
            except Exception as e:
                pass
        """
        with self.assertRaisesRegex(
            TypedSyntaxError, "Cannot assign to a Final variable"
        ):
            self.compile(codestr, StaticCodeGenerator, modname="foo")

    def test_final_reassigned_in_loop_target_tuple(self):
        codestr = """
        from typing import Final

        x: Final = 0xdeadbeef

        for x, y in [(1, 2)]:
            pass
        """
        with self.assertRaisesRegex(
            TypedSyntaxError, "Cannot assign to a Final variable"
        ):
            self.compile(codestr, StaticCodeGenerator, modname="foo")

    def test_final_reassigned_in_ctxmgr(self):
        codestr = """
        from typing import Final

        x: Final = 0xdeadbeef

        with open("lol") as x:
            pass
        """
        with self.assertRaisesRegex(
            TypedSyntaxError, "Cannot assign to a Final variable"
        ):
            self.compile(codestr, StaticCodeGenerator, modname="foo")

    def test_final_generic_reassign(self):
        codestr = """
        from typing import Final

        x: Final[int] = 0xdeadbeef
        x = 0x5ca1ab1e
        """
        with self.assertRaisesRegex(
            TypedSyntaxError, "Cannot assign to a Final variable"
        ):
            self.compile(codestr, StaticCodeGenerator, modname="foo")

    def test_class_level_final_decl(self):
        codestr = """
        from typing import Final

        class C:
            x: Final[int]

            def __init__(self):
                self.x = 3
        """
        self.compile(codestr, StaticCodeGenerator, modname="foo")

    def test_class_level_final_decl_uninitialized(self):
        codestr = """
        from typing import Final

        class C:
            x: Final
        """
        with self.assertRaisesRegex(
            TypedSyntaxError,
            re.escape("Final attribute not initialized: foo.C:x"),
        ):
            self.compile(codestr, StaticCodeGenerator, modname="foo")

    def test_class_level_final_reinitialized(self):
        codestr = """
        from typing import Final

        class C:
            x: Final = 3
            x = 4
        """
        with self.assertRaisesRegex(
            TypedSyntaxError, "Cannot assign to a Final variable"
        ):
            self.compile(codestr, StaticCodeGenerator, modname="foo")

    def test_class_level_final_reinitialized_directly(self):
        codestr = """
        from typing import Final

        class C:
            x: Final = 3

        C.x = 4
        """
        # Note - this will raise even without the Final, we don't allow assignments to slots
        with self.assertRaisesRegex(
            TypedSyntaxError,
            type_mismatch("Exact[int]", "types.MemberDescriptorType"),
        ):
            self.compile(codestr, StaticCodeGenerator, modname="foo")

    def test_class_level_final_reinitialized_in_instance(self):
        codestr = """
        from typing import Final

        class C:
            x: Final = 3

        C().x = 4
        """
        with self.assertRaisesRegex(
            TypedSyntaxError,
            "Cannot assign to a Final attribute of foo.C:x",
        ):
            self.compile(codestr, StaticCodeGenerator, modname="foo")

    def test_class_level_final_reinitialized_in_method(self):
        codestr = """
        from typing import Final

        class C:
            x: Final = 3

            def something(self) -> None:
                self.x = 4
        """
        with self.assertRaisesRegex(
            TypedSyntaxError, "Cannot assign to a Final attribute of foo.C:x"
        ):
            self.compile(codestr, StaticCodeGenerator, modname="foo")

    def test_class_level_final_reinitialized_in_subclass_without_annotation(self):
        codestr = """
        from typing import Final

        class C:
            x: Final = 3

        class D(C):
            x = 4
        """
        with self.assertRaisesRegex(
            TypedSyntaxError,
            "Cannot assign to a Final attribute of foo.D:x",
        ):
            self.compile(codestr, StaticCodeGenerator, modname="foo")

    def test_class_level_final_reinitialized_in_subclass_with_annotation(self):
        codestr = """
        from typing import Final

        class C:
            x: Final = 3

        class D(C):
            x: Final[int] = 4
        """
        with self.assertRaisesRegex(
            TypedSyntaxError,
            "Cannot assign to a Final attribute of foo.D:x",
        ):
            self.compile(codestr, StaticCodeGenerator, modname="foo")

    def test_class_level_final_reinitialized_in_subclass_init(self):
        codestr = """
        from typing import Final

        class C:
            x: Final = 3

        class D(C):
            def __init__(self):
                self.x = 4
        """
        with self.assertRaisesRegex(
            TypedSyntaxError,
            "Cannot assign to a Final attribute of foo.D:x",
        ):
            self.compile(codestr, StaticCodeGenerator, modname="foo")

    def test_class_level_final_reinitialized_in_subclass_init_with_annotation(self):
        codestr = """
        from typing import Final

        class C:
            x: Final[int] = 3

        class D(C):
            def __init__(self):
                self.x: Final[int] = 4
        """
        with self.assertRaisesRegex(
            TypedSyntaxError,
            "Cannot assign to a Final attribute of foo.D:x",
        ):
            self.compile(codestr, StaticCodeGenerator, modname="foo")

    def test_class_level_final_decl_in_init_reinitialized_in_subclass_init(self):
        codestr = """
            from typing import Final

            class C:
                x: Final[int]

                def __init__(self) -> None:
                    self.x = 3

            class D(C):
                def __init__(self) -> None:
                    self.x = 4
        """
        with self.assertRaisesRegex(
            TypedSyntaxError,
            "Cannot assign to a Final attribute of foo.D:x",
        ):
            self.compile(codestr, StaticCodeGenerator, modname="foo")

    def test_final_in_args(self):
        codestr = """
        from typing import Final

        def f(a: Final) -> None:
            pass
        """
        with self.assertRaisesRegex(
            TypedSyntaxError,
            "Final annotation is only valid in initial declaration",
        ):
            self.compile(codestr, StaticCodeGenerator, modname="foo")

    def test_final_returns(self):
        codestr = """
        from typing import Final

        def f() -> Final[int]:
            return 1
        """
        with self.assertRaisesRegex(
            TypedSyntaxError,
            "Final annotation is only valid in initial declaration",
        ):
            self.compile(codestr, StaticCodeGenerator, modname="foo")

    def test_final_decorator(self):
        codestr = """
        from typing import final

        class C:
            @final
            def f():
                pass
        """
        self.compile(codestr, StaticCodeGenerator, modname="foo")

    def test_final_decorator_override(self):
        codestr = """
        from typing import final

        class C:
            @final
            def f():
                pass

        class D(C):
            def f():
                pass
        """
        with self.assertRaisesRegex(
            TypedSyntaxError, "Cannot assign to a Final attribute of foo.D:f"
        ):
            self.compile(codestr, StaticCodeGenerator, modname="foo")

    def test_final_decorator_override_with_assignment(self):
        codestr = """
        from typing import final

        class C:
            @final
            def f():
                pass

        class D(C):
            f = print
        """
        with self.assertRaisesRegex(
            TypedSyntaxError, "Cannot assign to a Final attribute of foo.D:f"
        ):
            self.compile(codestr, StaticCodeGenerator, modname="foo")

    def test_final_decorator_override_transitivity(self):
        codestr = """
        from typing import final

        class C:
            @final
            def f():
                pass

        class D(C):
            pass

        class E(D):
            def f():
                pass
        """
        with self.assertRaisesRegex(
            TypedSyntaxError, "Cannot assign to a Final attribute of foo.E:f"
        ):
            self.compile(codestr, StaticCodeGenerator, modname="foo")

    def test_final_decorator_class(self):
        codestr = """
        from typing import final

        @final
        class C:
            def f(self):
                pass

        def f():
            return C().f()
        """
        c = self.compile(codestr, StaticCodeGenerator, modname="foo")
        f = self.find_code(c, "f")
        self.assertInBytecode(f, "INVOKE_METHOD")

    def test_final_decorator_class_inheritance(self):
        codestr = """
        from typing import final

        @final
        class C:
            pass

        class D(C):
            pass
        """
        with self.assertRaisesRegex(
            TypedSyntaxError, "Class `foo.D` cannot subclass a Final class: `foo.C`"
        ):
            self.compile(codestr, StaticCodeGenerator, modname="foo")

    def test_final_decorator_class_dynamic(self):
        """We should never mark DYNAMIC_TYPE as final."""
        codestr = """
            from typing import final, Generic, NamedTuple

            @final
            class NT(NamedTuple):
                x: int

            class C(Generic):
                pass
        """
        # No TypedSyntaxError "cannot inherit from Final class 'dynamic'"
        self.compile(codestr)

    def test_slotification_decorated(self):
        codestr = """
            class _Inner():
                pass

            def something(klass):
                return _Inner

            @something
            class C:
                def f(self):
                    pass

            def f():
                return C().f()
        """
        with self.in_module(codestr, code_gen=StaticCodeGenerator) as mod:
            f = mod["f"]
            self.assertNotInBytecode(f, "INVOKE_FUNCTION")
            self.assertNotInBytecode(f, "INVOKE_METHOD")

    def test_inline_func(self):
        codestr = """
            from __static__ import inline

            @inline
            def f(x, y):
                return x + y

            def g():
                return f(1,2)
        """
        # we only inline at opt level 2 to avoid test patching problems
        # TODO longer term we might need something better here (e.g. emit both
        # inlined code and call and a guard to choose); assuming
        # non-patchability at opt 2 works for IG but isn't generally valid
        for opt in [0, 1, 2]:
            with self.subTest(opt=opt):
                with self.in_module(codestr, optimize=opt) as mod:
                    g = mod["g"]
                    if opt == 2:
                        self.assertInBytecode(g, "LOAD_CONST", 3)
                    else:
                        self.assertInBytecode(
                            g, "INVOKE_FUNCTION", (("test_inline_func", "f"), 2)
                        )
                    self.assertEqual(g(), 3)

    def test_inline_kwarg(self):
        codestr = """
            from __static__ import inline

            @inline
            def f(x, y):
                return x + y

            def g():
                return f(x=1,y=2)
        """
        with self.in_module(codestr, optimize=2) as mod:
            g = mod["g"]
            self.assertInBytecode(g, "LOAD_CONST", 3)
            self.assertEqual(g(), 3)

    def test_inline_bare_return(self):
        codestr = """
            from __static__ import inline

            @inline
            def f(x, y):
                return

            def g():
                return f(x=1,y=2)
        """
        with self.in_module(codestr, optimize=2) as mod:
            g = mod["g"]
            self.assertInBytecode(g, "LOAD_CONST", None)
            self.assertEqual(g(), None)

    def test_inline_final(self):
        codestr = """
            from __static__ import inline
            from typing import Final

            Y: Final[int] = 42
            @inline
            def f(x):
                return x + Y

            def g():
                return f(1)
        """
        with self.in_module(codestr, optimize=2) as mod:
            g = mod["g"]
            # We don't currently inline math with finals
            self.assertInBytecode(g, "LOAD_CONST", 42)
            self.assertEqual(g(), 43)

    def test_inline_nested(self):
        codestr = """
            from __static__ import inline

            @inline
            def e(x, y):
                return x + y

            @inline
            def f(x, y):
                return e(x, 3)

            def g():
                return f(1,2)
        """
        with self.in_module(codestr, optimize=2) as mod:
            g = mod["g"]
            self.assertInBytecode(g, "LOAD_CONST", 4)
            self.assertEqual(g(), 4)

    def test_inline_nested_arg(self):
        codestr = """
            from __static__ import inline

            @inline
            def e(x, y):
                return x + y

            @inline
            def f(x, y):
                return e(x, 3)

            def g(a,b):
                return f(a,b)
        """
        with self.in_module(codestr, optimize=2) as mod:
            g = mod["g"]
            self.assertInBytecode(g, "LOAD_CONST", 3)
            self.assertInBytecode(g, "BINARY_ADD")
            self.assertEqual(g(1, 2), 4)

    def test_inline_recursive(self):
        codestr = """
            from __static__ import inline

            @inline
            def f(x, y):
                return f(x, y)

            def g():
                return f(1,2)
        """
        with self.in_module(codestr, optimize=2) as mod:
            g = mod["g"]
            self.assertInBytecode(
                g, "INVOKE_FUNCTION", ((("test_inline_recursive", "f"), 2))
            )

    def test_inline_func_default(self):
        codestr = """
            from __static__ import inline

            @inline
            def f(x, y = 2):
                return x + y

            def g():
                return f(1)
        """
        with self.in_module(codestr, optimize=2) as mod:
            g = mod["g"]
            self.assertInBytecode(g, "LOAD_CONST", 3)

            self.assertEqual(g(), 3)

    def test_inline_arg_type(self):
        codestr = """
            from __static__ import box, inline, int64, int32

            @inline
            def f(x: int64) -> int:
                return box(x)

            def g(arg: int) -> int:
                return f(int64(arg))
        """
        with self.in_module(codestr, optimize=2) as mod:
            g = mod["g"]
            self.assertInBytecode(g, "PRIMITIVE_BOX")
            self.assertEqual(g(3), 3)

    def test_augassign_primitive_int(self):
        codestr = """
        from __static__ import int8, box, unbox

        def a(i: int) -> int:
            j: int8 = unbox(i)
            j += 2
            return box(j)
        """
        with self.in_module(codestr, code_gen=StaticCodeGenerator) as mod:
            a = mod["a"]
            self.assertInBytecode(a, "PRIMITIVE_BINARY_OP", 0)
            self.assertEqual(a(3), 5)

    def test_primitive_compare_immediate_no_branch_on_result(self):
        for rev in [True, False]:
            compare = "0 == xp" if rev else "xp == 0"
            codestr = f"""
                from __static__ import box, int64, int32

                def f(x: int) -> bool:
                    xp = int64(x)
                    y = {compare}
                    return box(y)
            """
            with self.subTest(rev=rev):
                with self.in_module(codestr) as mod:
                    f = mod["f"]
                    self.assertEqual(f(3), 0)
                    self.assertEqual(f(0), 1)
                    self.assertIs(f(0), True)

    def test_type_type_final(self):
        codestr = """
        class A(type):
            pass
        """
        self.compile(codestr)


class StaticRuntimeTests(StaticTestBase):
    def test_bad_slots_qualname_conflict(self):
        with self.assertRaises(ValueError):

            class C:
                __slots__ = ("x",)
                __slot_types__ = {"x": ("__static__", "int32")}
                x = 42

    def test_typed_slots_bad_inst(self):
        class C:
            __slots__ = ("a",)
            __slot_types__ = {"a": ("__static__", "int32")}

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
                __slot_types__ = {'a': ('test_typed_slots_object', 'C')}

            inst = C()
        """

        with self.in_module(codestr, code_gen=PythonCodeGenerator) as mod:
            inst, C = mod["inst"], mod["C"]
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
            C = mod["C"]
            c = C()
            ref = mod["f"](c)
            self.assertIs(ref(), c)
            del c
            self.assertIs(ref(), None)
            self.assertEqual(C.__slots__, ("__weakref__",))

    def test_dynamic_return(self):
        codestr = """
            from __future__ import annotations
            from __static__ import allow_weakrefs, dynamic_return
            import weakref

            singletons = []

            @allow_weakrefs
            class C:
                @dynamic_return
                @staticmethod
                def make() -> C:
                    return weakref.proxy(singletons[0])

                def g(self) -> int:
                    return 1

            singletons.append(C())

            def f() -> int:
                c = C.make()
                return c.g()
        """
        with self.in_strict_module(codestr) as mod:
            # We don't try to cast the return type of make
            self.assertNotInBytecode(mod.C.make, "CAST")
            # We can statically invoke make
            self.assertInBytecode(
                mod.f, "INVOKE_FUNCTION", (("test_dynamic_return", "C", "make"), 0)
            )
            # But we can't statically invoke a method on the returned instance
            self.assertNotInBytecode(mod.f, "INVOKE_METHOD")
            self.assertEqual(mod.f(), 1)
            # We don't mess with __annotations__
            self.assertEqual(mod.C.make.__annotations__, {"return": "C"})

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
                __slot_types__ = {'a': ('test_typed_slots_object', 'C')}

            inst = C()
        """

        with self.in_module(codestr, code_gen=PythonCodeGenerator) as mod:
            inst, C = mod["inst"], mod["C"]
            self.assertEqual(C.a.__class__.__name__, "typed_descriptor")
            with self.assertRaises(TypeError):
                # type is checked
                inst.a = 42

    def test_typed_slots_optional_object(self):
        codestr = """
            class C:
                __slots__ = ('a', )
                __slot_types__ = {'a': ('test_typed_slots_optional_object', 'C', '?')}

            inst = C()
        """

        with self.in_module(codestr, code_gen=PythonCodeGenerator) as mod:
            inst, C = mod["inst"], mod["C"]
            inst.a = None
            self.assertEqual(inst.a, None)

    def test_typed_slots_private(self):
        codestr = """
            class C:
                __slots__ = ('__a', )
                __slot_types__ = {'__a': ('test_typed_slots_private', 'C', '?')}
                def __init__(self):
                    self.__a = None

            inst = C()
        """

        with self.in_module(codestr, code_gen=PythonCodeGenerator) as mod:
            inst, C = mod["inst"], mod["C"]
            self.assertEqual(inst._C__a, None)
            inst._C__a = inst
            self.assertEqual(inst._C__a, inst)
            inst._C__a = None
            self.assertEqual(inst._C__a, None)

    def test_typed_slots_optional_not_defined(self):
        codestr = """
            class C:
                __slots__ = ('a', )
                __slot_types__ = {'a': ('test_typed_slots_optional_object', 'D', '?')}

                def __init__(self):
                    self.a = None

            inst = C()

            class D:
                pass
        """

        with self.in_module(codestr, code_gen=PythonCodeGenerator) as mod:
            inst, C = mod["inst"], mod["C"]
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
            inst, C = mod["inst"], mod["C"]
            inst.a = None
            self.assertEqual(inst.a, None)

    def test_typed_slots_primitives(self):
        slot_types = [
            # signed
            (
                ("__static__", "byte"),
                0,
                1,
                [(1 << 7) - 1, -(1 << 7)],
                [1 << 8],
                ["abc"],
            ),
            (
                ("__static__", "int8"),
                0,
                1,
                [(1 << 7) - 1, -(1 << 7)],
                [1 << 8],
                ["abc"],
            ),
            (
                ("__static__", "int16"),
                0,
                2,
                [(1 << 15) - 1, -(1 << 15)],
                [1 << 15, -(1 << 15) - 1],
                ["abc"],
            ),
            (
                ("__static__", "int32"),
                0,
                4,
                [(1 << 31) - 1, -(1 << 31)],
                [1 << 31, -(1 << 31) - 1],
                ["abc"],
            ),
            (("__static__", "int64"), 0, 8, [(1 << 63) - 1, -(1 << 63)], [], [1 << 63]),
            # unsigned
            (("__static__", "uint8"), 0, 1, [(1 << 8) - 1, 0], [1 << 8, -1], ["abc"]),
            (
                ("__static__", "uint16"),
                0,
                2,
                [(1 << 16) - 1, 0],
                [1 << 16, -1],
                ["abc"],
            ),
            (
                ("__static__", "uint32"),
                0,
                4,
                [(1 << 32) - 1, 0],
                [1 << 32, -1],
                ["abc"],
            ),
            (("__static__", "uint64"), 0, 8, [(1 << 64) - 1, 0], [], [1 << 64]),
            # pointer
            (
                ("__static__", "ssize_t"),
                0,
                self.ptr_size,
                [1, sys.maxsize, -sys.maxsize - 1],
                [],
                [sys.maxsize + 1, -sys.maxsize - 2],
            ),
            # floating point
            (("__static__", "single"), 0.0, 4, [1.0], [], ["abc"]),
            (("__static__", "double"), 0.0, 8, [1.0], [], ["abc"]),
            # misc
            (("__static__", "char"), "\x00", 1, ["a"], [], ["abc"]),
            (("__static__", "cbool"), False, 1, [True], [], ["abc", 1]),
        ]

        base_size = self.base_size
        for type_spec, default, size, test_vals, warn_vals, err_vals in slot_types:
            with self.subTest(
                type_spec=type_spec,
                default=default,
                size=size,
                test_vals=test_vals,
                warn_vals=warn_vals,
                err_vals=err_vals,
            ):

                class C:
                    __slots__ = ("a",)
                    __slot_types__ = {"a": type_spec}

                a = C()
                self.assertEqual(sys.getsizeof(a), base_size + size, type)
                self.assertEqual(a.a, default)
                self.assertEqual(type(a.a), type(default))
                for val in test_vals:
                    a.a = val
                    self.assertEqual(a.a, val)

                with warnings.catch_warnings():
                    warnings.simplefilter("error", category=RuntimeWarning)
                    for val in warn_vals:
                        with self.assertRaises(RuntimeWarning):
                            a.a = val

                for val in err_vals:
                    with self.assertRaises((TypeError, OverflowError)):
                        a.a = val

    def test_invoke_function(self):
        my_int = "12345"
        codestr = f"""
        def x(a: str, b: int) -> str:
            return a + str(b)

        def test() -> str:
            return x("hello", {my_int})
        """
        c = self.compile(codestr, StaticCodeGenerator, modname="foo.py")
        test = self.find_code(c, "test")
        self.assertInBytecode(test, "INVOKE_FUNCTION", (("foo.py", "x"), 2))
        with self.in_module(codestr) as mod:
            test_callable = mod["test"]
            self.assertEqual(test_callable(), "hello" + my_int)

    def test_awaited_invoke_function(self):
        codestr = """
            async def f() -> int:
                return 1

            async def g() -> int:
                return await f()
        """
        with self.in_strict_module(codestr) as mod:
            self.assertInBytecode(
                mod.g, "INVOKE_FUNCTION", (("test_awaited_invoke_function", "f"), 0)
            )
            self.assertEqual(asyncio.run(mod.g()), 1)

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
                (("test_awaited_invoke_function_with_args", "f"), 2),
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
            g = mod["g"]
            self.assertInBytecode(
                g,
                "INVOKE_FUNCTION",
                (("test_awaited_invoke_function_indirect_with_args", "f"), 2),
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
                (("test_awaited_invoke_function_future", "g"), 0),
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
                mod.C.g, "INVOKE_METHOD", (("test_awaited_invoke_method", "C", "f"), 0)
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
                (("test_awaited_invoke_method_with_args", "C", "f"), 2),
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
                "INVOKE_METHOD",
                (("test_awaited_invoke_method_future", "C", "g"), 0),
            )
            asyncio.run(mod.f())

            # exercise shadowcode, INVOKE_METHOD_CACHED
            self.make_async_func_hot(mod.f)
            asyncio.run(mod.f())

    def test_load_iterable_arg(self):
        codestr = """
        def x(a: int, b: int, c: str, d: float, e: float) -> int:
            return 7

        def y() -> int:
            p = ("hi", 0.1, 0.2)
            return x(1, 3, *p)
        """
        y = self.find_code(
            self.compile(codestr, StaticCodeGenerator, modname="foo"), name="y"
        )
        self.assertInBytecode(y, "LOAD_ITERABLE_ARG", 0)
        self.assertInBytecode(y, "LOAD_ITERABLE_ARG", 1)
        self.assertInBytecode(y, "LOAD_ITERABLE_ARG", 2)
        self.assertNotInBytecode(y, "LOAD_ITERABLE_ARG", 3)
        with self.in_module(codestr) as mod:
            y_callable = mod["y"]
            self.assertEqual(y_callable(), 7)

    def test_load_iterable_arg_default_overridden(self):
        codestr = """
            def x(a: int, b: int, c: str, d: float = 10.1, e: float = 20.1) -> bool:
                return bool(
                    a == 1
                    and b == 3
                    and c == "hi"
                    and d == 0.1
                    and e == 0.2
                )

            def y() -> bool:
                p = ("hi", 0.1, 0.2)
                return x(1, 3, *p)
        """
        y = self.find_code(
            self.compile(codestr, StaticCodeGenerator, modname="foo"), name="y"
        )
        self.assertNotInBytecode(y, "LOAD_ITERABLE_ARG", 3)
        self.assertNotInBytecode(y, "LOAD_MAPPING_ARG", 3)
        with self.in_module(codestr) as mod:
            y_callable = mod["y"]
            self.assertTrue(y_callable())

    def test_load_iterable_arg_multi_star(self):
        codestr = """
        def x(a: int, b: int, c: str, d: float, e: float) -> int:
            return 7

        def y() -> int:
            p = (1, 3)
            q = ("hi", 0.1, 0.2)
            return x(*p, *q)
        """
        y = self.find_code(
            self.compile(codestr, StaticCodeGenerator, modname="foo"), name="y"
        )
        # we should fallback to the normal Python compiler for this
        self.assertNotInBytecode(y, "LOAD_ITERABLE_ARG")
        with self.in_module(codestr) as mod:
            y_callable = mod["y"]
            self.assertEqual(y_callable(), 7)

    def test_load_iterable_arg_star_not_last(self):
        codestr = """
        def x(a: int, b: int, c: str, d: float, e: float) -> int:
            return 7

        def y() -> int:
            p = (1, 3, 'abc', 0.1)
            return x(*p, 1.0)
        """
        y = self.find_code(
            self.compile(codestr, StaticCodeGenerator, modname="foo"), name="y"
        )
        # we should fallback to the normal Python compiler for this
        self.assertNotInBytecode(y, "LOAD_ITERABLE_ARG")
        with self.in_module(codestr) as mod:
            y_callable = mod["y"]
            self.assertEqual(y_callable(), 7)

    def test_load_iterable_arg_failure(self):
        codestr = """
        def x(a: int, b: int, c: str, d: float, e: float) -> int:
            return 7

        def y() -> int:
            p = ("hi", 0.1)
            return x(1, 3, *p)
        """
        y = self.find_code(
            self.compile(codestr, StaticCodeGenerator, modname="foo"), name="y"
        )
        self.assertInBytecode(y, "LOAD_ITERABLE_ARG", 0)
        self.assertInBytecode(y, "LOAD_ITERABLE_ARG", 1)
        self.assertInBytecode(y, "LOAD_ITERABLE_ARG", 2)
        self.assertNotInBytecode(y, "LOAD_ITERABLE_ARG", 3)
        with self.in_module(codestr) as mod:
            y_callable = mod["y"]
            with self.assertRaises(IndexError):
                y_callable()

    def test_load_iterable_arg_sequence(self):
        codestr = """
        def x(a: int, b: int, c: str, d: float, e: float) -> int:
            return 7

        def y() -> int:
            p = ["hi", 0.1, 0.2]
            return x(1, 3, *p)
        """
        y = self.find_code(
            self.compile(codestr, StaticCodeGenerator, modname="foo"), name="y"
        )
        self.assertInBytecode(y, "LOAD_ITERABLE_ARG", 0)
        self.assertInBytecode(y, "LOAD_ITERABLE_ARG", 1)
        self.assertInBytecode(y, "LOAD_ITERABLE_ARG", 2)
        self.assertNotInBytecode(y, "LOAD_ITERABLE_ARG", 3)
        with self.in_module(codestr) as mod:
            y_callable = mod["y"]
            self.assertEqual(y_callable(), 7)

    def test_load_iterable_arg_sequence_1(self):
        codestr = """
        def x(a: int, b: int, c: str, d: float, e: float) -> int:
            return 7

        def gen():
            for i in ["hi", 0.05, 0.2]:
                yield i

        def y() -> int:
            g = gen()
            return x(1, 3, *g)
        """
        y = self.find_code(
            self.compile(codestr, StaticCodeGenerator, modname="foo"), name="y"
        )
        self.assertInBytecode(y, "LOAD_ITERABLE_ARG", 0)
        self.assertInBytecode(y, "LOAD_ITERABLE_ARG", 1)
        self.assertInBytecode(y, "LOAD_ITERABLE_ARG", 2)
        self.assertNotInBytecode(y, "LOAD_ITERABLE_ARG", 3)
        with self.in_module(codestr) as mod:
            y_callable = mod["y"]
            self.assertEqual(y_callable(), 7)

    def test_load_iterable_arg_sequence_failure(self):
        codestr = """
        def x(a: int, b: int, c: str, d: float, e: float) -> int:
            return 7

        def y() -> int:
            p = ["hi", 0.1]
            return x(1, 3, *p)
        """
        y = self.find_code(
            self.compile(codestr, StaticCodeGenerator, modname="foo"), name="y"
        )
        self.assertInBytecode(y, "LOAD_ITERABLE_ARG", 0)
        self.assertInBytecode(y, "LOAD_ITERABLE_ARG", 1)
        self.assertInBytecode(y, "LOAD_ITERABLE_ARG", 2)
        self.assertNotInBytecode(y, "LOAD_ITERABLE_ARG", 3)
        with self.in_module(codestr) as mod:
            y_callable = mod["y"]
            with self.assertRaises(IndexError):
                y_callable()

    def test_load_mapping_arg(self):
        codestr = """
        def x(a: int, b: int, c: str, d: float=-0.1, e: float=1.1, f: str="something") -> bool:
            return bool(f == "yo" and d == 1.0 and e == 1.1)

        def y() -> bool:
            d = {"d": 1.0}
            return x(1, 3, "hi", f="yo", **d)
        """
        y = self.find_code(
            self.compile(codestr, StaticCodeGenerator, modname="foo"), name="y"
        )
        self.assertInBytecode(y, "LOAD_MAPPING_ARG", 3)
        with self.in_module(codestr) as mod:
            y_callable = mod["y"]
            self.assertTrue(y_callable())

    def test_load_mapping_and_iterable_args_failure_1(self):
        """
        Fails because we don't supply enough positional args
        """

        codestr = """
        def x(a: int, b: int, c: str, d: float=2.2, e: float=1.1, f: str="something") -> bool:
            return bool(a == 1 and b == 3 and f == "yo" and d == 2.2 and e == 1.1)

        def y() -> bool:
            return x(1, 3, f="yo")
        """
        with self.assertRaisesRegex(
            SyntaxError, "Function foo.x expects a value for argument c"
        ):
            self.compile(codestr, StaticCodeGenerator, modname="foo")

    def test_load_mapping_arg_failure(self):
        """
        Fails because we supply an extra kwarg
        """
        codestr = """
        def x(a: int, b: int, c: str, d: float=2.2, e: float=1.1, f: str="something") -> bool:
            return bool(a == 1 and b == 3 and f == "yo" and d == 2.2 and e == 1.1)

        def y() -> bool:
            return x(1, 3, "hi", f="yo", g="lol")
        """
        with self.assertRaisesRegex(
            TypedSyntaxError,
            "Given argument g does not exist in the definition of foo.x",
        ):
            self.compile(codestr, StaticCodeGenerator, modname="foo")

    def test_load_mapping_arg_custom_class(self):
        """
        Fails because we supply a custom class for the mapped args, instead of a dict
        """
        codestr = """
        def x(a: int, b: int, c: str="hello") -> bool:
            return bool(a == 1 and b == 3 and c == "hello")

        class C:
            def __getitem__(self, key: str) -> str:
                if key == "c":
                    return "hi"

            def keys(self):
                return ["c"]

        def y() -> bool:
            return x(1, 3, **C())
        """
        with self.in_module(codestr) as mod:
            y_callable = mod["y"]
            with self.assertRaisesRegex(
                TypeError, r"argument after \*\* must be a dict, not C"
            ):
                self.assertTrue(y_callable())

    def test_load_mapping_arg_use_defaults(self):
        codestr = """
        def x(a: int, b: int, c: str, d: float=-0.1, e: float=1.1, f: str="something") -> bool:
            return bool(f == "yo" and d == -0.1 and e == 1.1)

        def y() -> bool:
            d = {"d": 1.0}
            return x(1, 3, "hi", f="yo")
        """
        y = self.find_code(
            self.compile(codestr, StaticCodeGenerator, modname="foo"), name="y"
        )
        self.assertInBytecode(y, "LOAD_CONST", 1.1)
        with self.in_module(codestr) as mod:
            y_callable = mod["y"]
            self.assertTrue(y_callable())

    def test_default_arg_non_const(self):
        codestr = """
        class C: pass
        def x(val=C()) -> C:
            return val

        def f() -> C:
            return x()
        """
        with self.in_module(codestr) as mod:
            f = mod["f"]
            self.assertInBytecode(f, "CALL_FUNCTION")

    def test_default_arg_non_const_kw_provided(self):
        codestr = """
        class C: pass
        def x(val:object=C()):
            return val

        def f():
            return x(val=42)
        """

        with self.in_module(codestr) as mod:
            f = mod["f"]
            self.assertEqual(f(), 42)

    def test_load_mapping_arg_order(self):
        codestr = """
        def x(a: int, b: int, c: str, d: float=-0.1, e: float=1.1, f: str="something") -> bool:
            return bool(
                a == 1
                and b == 3
                and c == "hi"
                and d == 1.1
                and e == 3.3
                and f == "hmm"
            )

        stuff = []
        def q() -> float:
            stuff.append("q")
            return 1.1

        def r() -> float:
            stuff.append("r")
            return 3.3

        def s() -> str:
            stuff.append("s")
            return "hmm"

        def y() -> bool:
            return x(1, 3, "hi", f=s(), d=q(), e=r())
        """
        y = self.find_code(
            self.compile(codestr, StaticCodeGenerator, modname="foo"), name="y"
        )
        self.assertInBytecode(y, "STORE_FAST", "_pystatic_.0._tmp__d")
        self.assertInBytecode(y, "LOAD_FAST", "_pystatic_.0._tmp__d")
        with self.in_module(codestr) as mod:
            y_callable = mod["y"]
            self.assertTrue(y_callable())
            self.assertEqual(["s", "q", "r"], mod["stuff"])

    def test_load_mapping_arg_order_with_variadic_kw_args(self):
        codestr = """
        def x(a: int, b: int, c: str, d: float=-0.1, e: float=1.1, f: str="something", g: str="look-here") -> bool:
            return bool(
                a == 1
                and b == 3
                and c == "hi"
                and d == 1.1
                and e == 3.3
                and f == "hmm"
                and g == "overridden"
            )

        stuff = []
        def q() -> float:
            stuff.append("q")
            return 1.1

        def r() -> float:
            stuff.append("r")
            return 3.3

        def s() -> str:
            stuff.append("s")
            return "hmm"

        def y() -> bool:
            kw = {"g": "overridden"}
            return x(1, 3, "hi", f=s(), **kw, d=q(), e=r())
        """
        y = self.find_code(
            self.compile(codestr, StaticCodeGenerator, modname="foo"), name="y"
        )
        self.assertInBytecode(y, "STORE_FAST", "_pystatic_.0._tmp__d")
        self.assertInBytecode(y, "LOAD_FAST", "_pystatic_.0._tmp__d")
        with self.in_module(codestr) as mod:
            y_callable = mod["y"]
            self.assertTrue(y_callable())
            self.assertEqual(["s", "q", "r"], mod["stuff"])

    def test_load_mapping_arg_order_with_variadic_kw_args_one_positional(self):
        codestr = """
        def x(a: int, b: int, c: str, d: float=-0.1, e: float=1.1, f: str="something", g: str="look-here") -> bool:
            return bool(
                a == 1
                and b == 3
                and c == "hi"
                and d == 1.1
                and e == 3.3
                and f == "hmm"
                and g == "overridden"
            )

        stuff = []
        def q() -> float:
            stuff.append("q")
            return 1.1

        def r() -> float:
            stuff.append("r")
            return 3.3

        def s() -> str:
            stuff.append("s")
            return "hmm"


        def y() -> bool:
            kw = {"g": "overridden"}
            return x(1, 3, "hi", 1.1, f=s(), **kw, e=r())
        """
        y = self.find_code(
            self.compile(codestr, StaticCodeGenerator, modname="foo"), name="y"
        )
        self.assertNotInBytecode(y, "STORE_FAST", "_pystatic_.0._tmp__d")
        self.assertNotInBytecode(y, "LOAD_FAST", "_pystatic_.0._tmp__d")
        with self.in_module(codestr) as mod:
            y_callable = mod["y"]
            self.assertTrue(y_callable())
            self.assertEqual(["s", "r"], mod["stuff"])

    def test_vector_generics(self):
        T = TypeVar("T")
        VT = Vector[T]
        VT2 = VT[int64]
        a = VT2()
        a.append(42)
        with self.assertRaisesRegex(TypeError, "Cannot create plain Vector"):
            VT()

    def test_vector_invalid_type(self):
        class C:
            pass

        with self.assertRaisesRegex(
            TypeError, "Invalid type for ArrayElement: C when instantiating Vector"
        ):
            Vector[C]

    def test_vector_wrong_arg_count(self):
        class C:
            pass

        with self.assertRaisesRegex(
            TypeError, "Incorrect number of type arguments for Vector"
        ):
            Vector[int64, int64]

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

    def test_array_slice(self):
        v = Array[int64]([1, 2, 3, 4])
        self.assertEqual(v[1:3], Array[int64]([2, 3]))
        self.assertEqual(type(v[1:2]), Array[int64])

    def test_vector_slice(self):
        v = Vector[int64]([1, 2, 3, 4])
        self.assertEqual(v[1:3], Vector[int64]([2, 3]))
        self.assertEqual(type(v[1:2]), Vector[int64])

    def test_array_deepcopy(self):
        v = Array[int64]([1, 2, 3, 4])
        self.assertEqual(v, deepcopy(v))
        self.assertIsNot(v, deepcopy(v))
        self.assertEqual(type(v), type(deepcopy(v)))

    def test_vector_deepcopy(self):
        v = Vector[int64]([1, 2, 3, 4])
        self.assertEqual(v, deepcopy(v))
        self.assertIsNot(v, deepcopy(v))
        self.assertEqual(type(v), type(deepcopy(v)))

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

    def test_array_len(self):
        codestr = """
            from __static__ import int64, char, double, Array
            from array import array

            def y():
                return len(Array[int64]([1, 3, 5]))
        """
        y = self.find_code(
            self.compile(codestr, StaticCodeGenerator, modname="foo"), name="y"
        )
        self.assertInBytecode(y, "FAST_LEN", FAST_LEN_ARRAY)
        with self.in_module(codestr, code_gen=StaticCodeGenerator) as mod:
            y = mod["y"]
            self.assertEqual(y(), 3)

    def test_array_isinstance(self):
        x = Array[int64](0)
        self.assertTrue(isinstance(x, Array[int64]))
        self.assertFalse(isinstance(x, Array[int32]))
        self.assertTrue(issubclass(Array[int64], Array[int64]))
        self.assertFalse(issubclass(Array[int64], Array[int32]))

    def test_array_weird_type_constrution(self):
        self.assertIs(
            Array[int64],
            Array[
                int64,
            ],
        )

    def test_array_not_subclassable(self):

        with self.assertRaises(TypeError):

            class C(Array[int64]):
                pass

        with self.assertRaises(TypeError):

            class C(Array):

                pass

    def test_array_enum(self):
        codestr = """
            from __static__ import Array, clen, int64, box

            def f(x: Array[int64]):
                i: int64 = 0
                j: int64 = 0
                while i < clen(x):
                    j += x[i]
                    i+=1
                return box(j)
        """
        with self.in_module(codestr, code_gen=StaticCodeGenerator) as mod:
            f = mod["f"]
            a = Array[int64]([1, 2, 3, 4])
            self.assertEqual(f(a), 10)
            with self.assertRaises(TypeError):
                f(None)

    def test_optional_array_enum(self):
        codestr = """
            from __static__ import Array, clen, int64, box
            from typing import Optional

            def f(x: Optional[Array[int64]]):
                if x is None:
                    return 42

                i: int64 = 0
                j: int64 = 0
                while i < clen(x):
                    j += x[i]
                    i+=1
                return box(j)
        """
        with self.in_module(codestr, code_gen=StaticCodeGenerator) as mod:
            f = mod["f"]
            a = Array[int64]([1, 2, 3, 4])
            self.assertEqual(f(a), 10)
            self.assertEqual(f(None), 42)

    def test_array_len_subclass(self):
        codestr = """
            from __static__ import int64, Array

            def y(ar: Array[int64]):
                return len(ar)
        """
        y = self.find_code(
            self.compile(codestr, StaticCodeGenerator, modname="foo"), name="y"
        )
        self.assertInBytecode(y, "FAST_LEN", FAST_LEN_ARRAY | FAST_LEN_INEXACT)

        # TODO the below requires Array to be a generic type in C, or else
        # support for generic annotations for not-generic-in-C types. For now
        # it's sufficient to validate we emitted FAST_LEN_INEXACT flag.

        # class MyArray(Array):
        #    def __len__(self):
        #        return 123

        # with self.in_module(codestr, code_gen=StaticCodeGenerator) as mod:
        #    y = mod["y"]
        #    self.assertEqual(y(MyArray[int64]([1])), 123)

    def test_nonarray_len(self):
        codestr = """
            class Lol:
                def __len__(self):
                    return 421

            def y():
                return len(Lol())
        """
        y = self.find_code(
            self.compile(codestr, StaticCodeGenerator, modname="foo"), name="y"
        )
        self.assertNotInBytecode(y, "FAST_LEN")
        with self.in_module(codestr, code_gen=StaticCodeGenerator) as mod:
            y = mod["y"]
            self.assertEqual(y(), 421)

    def test_clen(self):
        codestr = """
            from __static__ import box, clen, int64
            from typing import List

            def f(l: List[int]):
                x: int64 = clen(l)
                return box(x)
        """
        with self.in_module(codestr) as mod:
            f = mod["f"]
            self.assertInBytecode(f, "FAST_LEN", FAST_LEN_LIST | FAST_LEN_INEXACT)
            self.assertEqual(f([1, 2, 3]), 3)

            class MyList(list):
                def __len__(self):
                    return 99

            self.assertEqual(f(MyList([1, 2])), 99)

    def test_clen_bad_arg(self):
        codestr = """
            from __static__ import clen

            def f(l):
                clen(l)
        """
        with self.assertRaisesRegex(
            TypedSyntaxError, "bad argument type 'dynamic' for clen()"
        ):
            self.compile(codestr)

    def test_seq_repeat_list(self):
        codestr = """
            def f():
                l = [1, 2]
                return l * 2
        """
        f = self.find_code(self.compile(codestr))
        self.assertInBytecode(f, "SEQUENCE_REPEAT", SEQ_LIST)
        with self.in_module(codestr) as mod:
            self.assertEqual(mod["f"](), [1, 2, 1, 2])

    def test_seq_repeat_list_reversed(self):
        codestr = """
            def f():
                l = [1, 2]
                return 2 * l
        """
        f = self.find_code(self.compile(codestr))
        self.assertInBytecode(f, "SEQUENCE_REPEAT", SEQ_LIST | SEQ_REPEAT_REVERSED)
        with self.in_module(codestr) as mod:
            self.assertEqual(mod["f"](), [1, 2, 1, 2])

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
            self.assertEqual(mod["f"](), [1, 2, 1, 2])

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
            self.assertEqual(mod["f"](), [1, 2, 1, 2])

    def test_seq_repeat_tuple(self):
        codestr = """
            def f():
                t = (1, 2)
                return t * 2
        """
        f = self.find_code(self.compile(codestr))
        self.assertInBytecode(f, "SEQUENCE_REPEAT", SEQ_TUPLE)
        with self.in_module(codestr) as mod:
            self.assertEqual(mod["f"](), (1, 2, 1, 2))

    def test_seq_repeat_tuple_reversed(self):
        codestr = """
            def f():
                t = (1, 2)
                return 2 * t
        """
        f = self.find_code(self.compile(codestr))
        self.assertInBytecode(f, "SEQUENCE_REPEAT", SEQ_TUPLE | SEQ_REPEAT_REVERSED)
        with self.in_module(codestr) as mod:
            self.assertEqual(mod["f"](), (1, 2, 1, 2))

    def test_seq_repeat_inexact_list(self):
        codestr = """
            from typing import List

            def f(l: List[int]):
                return l * 2
        """
        f = self.find_code(self.compile(codestr))
        self.assertInBytecode(f, "SEQUENCE_REPEAT", SEQ_LIST | SEQ_REPEAT_INEXACT_SEQ)
        with self.in_module(codestr) as mod:
            self.assertEqual(mod["f"]([1, 2]), [1, 2, 1, 2])

            class MyList(list):
                def __mul__(self, other):
                    return "RESULT"

            self.assertEqual(mod["f"](MyList([1, 2])), "RESULT")

    def test_seq_repeat_inexact_tuple(self):

        codestr = """
            from typing import Tuple

            def f(t: Tuple[int]):
                return t * 2
        """
        f = self.find_code(self.compile(codestr))
        self.assertInBytecode(f, "SEQUENCE_REPEAT", SEQ_TUPLE | SEQ_REPEAT_INEXACT_SEQ)
        with self.in_module(codestr) as mod:
            self.assertEqual(mod["f"]((1, 2)), (1, 2, 1, 2))

            class MyTuple(tuple):
                def __mul__(self, other):
                    return "RESULT"

            self.assertEqual(mod["f"](MyTuple((1, 2))), "RESULT")

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
            self.assertEqual(mod["f"](2), [1, 2, 1, 2])

            class MyInt(int):
                def __mul__(self, other):
                    return "RESULT"

            self.assertEqual(mod["f"](MyInt(2)), "RESULT")

    def test_load_int_const_sizes(self):
        cases = [
            ("int8", (1 << 7), True),
            ("int8", (-1 << 7) - 1, True),
            ("int8", -1 << 7, False),
            ("int8", (1 << 7) - 1, False),
            ("int16", (1 << 15), True),
            ("int16", (-1 << 15) - 1, True),
            ("int16", -1 << 15, False),
            ("int16", (1 << 15) - 1, False),
            ("int32", (1 << 31), True),
            ("int32", (-1 << 31) - 1, True),
            ("int32", -1 << 31, False),
            ("int32", (1 << 31) - 1, False),
            ("int64", (1 << 63), True),
            ("int64", (-1 << 63) - 1, True),
            ("int64", -1 << 63, False),
            ("int64", (1 << 63) - 1, False),
            ("uint8", (1 << 8), True),
            ("uint8", -1, True),
            ("uint8", (1 << 8) - 1, False),
            ("uint8", 0, False),
            ("uint16", (1 << 16), True),
            ("uint16", -1, True),
            ("uint16", (1 << 16) - 1, False),
            ("uint16", 0, False),
            ("uint32", (1 << 32), True),
            ("uint32", -1, True),
            ("uint32", (1 << 32) - 1, False),
            ("uint32", 0, False),
            ("uint64", (1 << 64), True),
            ("uint64", -1, True),
            ("uint64", (1 << 64) - 1, False),
            ("uint64", 0, False),
        ]
        for type, val, overflows in cases:
            codestr = f"""
                from __static__ import {type}, box

                def f() -> int:
                    x: {type} = {val}
                    return box(x)
            """
            with self.subTest(type=type, val=val, overflows=overflows):
                if overflows:
                    with self.assertRaisesRegex(
                        TypedSyntaxError, "outside of the range"
                    ):
                        self.compile(codestr)
                else:
                    with self.in_strict_module(codestr) as mod:
                        self.assertEqual(mod.f(), val)

    def test_load_int_const_signed(self):
        int_types = [
            "int8",
            "int16",
            "int32",
            "int64",
        ]
        signs = ["-", ""]
        values = [12]

        for type, sign, value in itertools.product(int_types, signs, values):
            expected = value if sign == "" else -value

            codestr = f"""
                from __static__ import {type}, box

                def y() -> int:
                    x: {type} = {sign}{value}
                    return box(x)
            """
            with self.subTest(type=type, sign=sign, value=value):
                y = self.find_code(
                    self.compile(codestr, StaticCodeGenerator, modname="foo"), name="y"
                )
                self.assertInBytecode(y, "PRIMITIVE_LOAD_CONST")
                with self.in_module(codestr, code_gen=StaticCodeGenerator) as mod:
                    y = mod["y"]
                    self.assertEqual(y(), expected)

    def test_load_int_const_unsigned(self):
        int_types = [
            "uint8",
            "uint16",
            "uint32",
            "uint64",
        ]
        values = [12]

        for type, value in itertools.product(int_types, values):
            expected = value
            codestr = f"""
                from __static__ import {type}, box

                def y() -> int:
                    return box({type}({value}))
            """
            with self.subTest(type=type, value=value):
                y = self.find_code(
                    self.compile(codestr, StaticCodeGenerator, modname="foo"), name="y"
                )
                self.assertInBytecode(y, "PRIMITIVE_LOAD_CONST")
                with self.in_module(codestr, code_gen=StaticCodeGenerator) as mod:
                    y = mod["y"]
                    self.assertEqual(y(), expected)

    def test_primitive_out_of_range(self):
        codestr = f"""
            from __static__ import int8, box

            def f() -> int:
                x = int8(255)
                return box(x)
        """
        with self.assertRaisesRegex(
            TypedSyntaxError,
            "constant 255 is outside of the range -128 to 127 for int8",
        ):
            self.compile(codestr)

    def test_primitive_conversions(self):
        cases = [
            ("int8", "int8", 5, 5),
            ("int8", "int16", 5, 5),
            ("int8", "int32", 5, 5),
            ("int8", "int64", 5, 5),
            ("int8", "uint8", -1, 255),
            ("int8", "uint8", 12, 12),
            ("int8", "uint16", -1, 65535),
            ("int8", "uint16", 12, 12),
            ("int8", "uint32", -1, 4294967295),
            ("int8", "uint32", 12, 12),
            ("int8", "uint64", -1, 18446744073709551615),
            ("int8", "uint64", 12, 12),
            ("int16", "int8", 5, 5),
            ("int16", "int8", -1, -1),
            ("int16", "int8", 32767, -1),
            ("int16", "int16", 5, 5),
            ("int16", "int32", -5, -5),
            ("int16", "int64", -6, -6),
            ("int16", "uint8", 32767, 255),
            ("int16", "uint8", -1, 255),
            ("int16", "uint16", 32767, 32767),
            ("int16", "uint16", -1, 65535),
            ("int16", "uint32", 1000, 1000),
            ("int16", "uint32", -1, 4294967295),
            ("int16", "uint64", 1414, 1414),
            ("int16", "uint64", -1, 18446744073709551615),
            ("int32", "int8", 5, 5),
            ("int32", "int8", -1, -1),
            ("int32", "int8", 2147483647, -1),
            ("int32", "int16", 5, 5),
            ("int32", "int16", -1, -1),
            ("int32", "int16", 2147483647, -1),
            ("int32", "int32", 5, 5),
            ("int32", "int64", 5, 5),
            ("int32", "uint8", 5, 5),
            ("int32", "uint8", 65535, 255),
            ("int32", "uint8", -1, 255),
            ("int32", "uint16", 5, 5),
            ("int32", "uint16", 2147483647, 65535),
            ("int32", "uint16", -1, 65535),
            ("int32", "uint32", 5, 5),
            ("int32", "uint32", -1, 4294967295),
            ("int32", "uint64", 5, 5),
            ("int32", "uint64", -1, 18446744073709551615),
            ("int64", "int8", 5, 5),
            ("int64", "int8", -1, -1),
            ("int64", "int8", 65535, -1),
            ("int64", "int16", 5, 5),
            ("int64", "int16", -1, -1),
            ("int64", "int16", 4294967295, -1),
            ("int64", "int32", 5, 5),
            ("int64", "int32", -1, -1),
            ("int64", "int32", 9223372036854775807, -1),
            ("int64", "int64", 5, 5),
            ("int64", "uint8", 5, 5),
            ("int64", "uint8", 65535, 255),
            ("int64", "uint8", -1, 255),
            ("int64", "uint16", 5, 5),
            ("int64", "uint16", 4294967295, 65535),
            ("int64", "uint16", -1, 65535),
            ("int64", "uint32", 5, 5),
            ("int64", "uint32", 9223372036854775807, 4294967295),
            ("int64", "uint32", -1, 4294967295),
            ("int64", "uint64", 5, 5),
            ("int64", "uint64", -1, 18446744073709551615),
            ("uint8", "int8", 5, 5),
            ("uint8", "int8", 255, -1),
            ("uint8", "int16", 255, 255),
            ("uint8", "int32", 255, 255),
            ("uint8", "int64", 255, 255),
            ("uint8", "uint8", 5, 5),
            ("uint8", "uint16", 255, 255),
            ("uint8", "uint32", 255, 255),
            ("uint8", "uint64", 255, 255),
            ("uint16", "int8", 5, 5),
            ("uint16", "int8", 65535, -1),
            ("uint16", "int16", 5, 5),
            ("uint16", "int16", 65535, -1),
            ("uint16", "int32", 65535, 65535),
            ("uint16", "int64", 65535, 65535),
            ("uint16", "uint8", 65535, 255),
            ("uint16", "uint16", 65535, 65535),
            ("uint16", "uint32", 65535, 65535),
            ("uint16", "uint64", 65535, 65535),
            ("uint32", "int8", 4, 4),
            ("uint32", "int8", 4294967295, -1),
            ("uint32", "int16", 5, 5),
            ("uint32", "int16", 4294967295, -1),
            ("uint32", "int32", 65535, 65535),
            ("uint32", "int32", 4294967295, -1),
            ("uint32", "int64", 4294967295, 4294967295),
            ("uint32", "uint8", 4, 4),
            ("uint32", "uint8", 65535, 255),
            ("uint32", "uint16", 4294967295, 65535),
            ("uint32", "uint32", 5, 5),
            ("uint32", "uint64", 4294967295, 4294967295),
            ("uint64", "int8", 4, 4),
            ("uint64", "int8", 18446744073709551615, -1),
            ("uint64", "int16", 4, 4),
            ("uint64", "int16", 18446744073709551615, -1),
            ("uint64", "int32", 4, 4),
            ("uint64", "int32", 18446744073709551615, -1),
            ("uint64", "int64", 4, 4),
            ("uint64", "int64", 18446744073709551615, -1),
            ("uint64", "uint8", 5, 5),
            ("uint64", "uint8", 65535, 255),
            ("uint64", "uint16", 4294967295, 65535),
            ("uint64", "uint32", 18446744073709551615, 4294967295),
            ("uint64", "uint64", 5, 5),
        ]

        for src, dest, val, expected in cases:
            codestr = f"""
                from __static__ import {src}, {dest}, box

                def y() -> int:
                    x = {dest}({src}({val}))
                    return box(x)
            """
            with self.subTest(src=src, dest=dest, val=val, expected=expected):
                with self.in_module(codestr, code_gen=StaticCodeGenerator) as mod:
                    y = mod["y"]
                    actual = y()
                    self.assertEqual(
                        actual,
                        expected,
                        f"failing case: {[src, dest, val, actual, expected]}",
                    )

    def test_no_cast_after_box(self):
        codestr = """
            from __static__ import int64, box

            def f(x: int) -> int:
                y = int64(x) + 1
                return box(y)
        """
        with self.in_module(codestr) as mod:
            f = mod["f"]
            self.assertNotInBytecode(f, "CAST")
            self.assertInBytecode(f, "PRIMITIVE_LOAD_CONST", (1, TYPED_INT64))
            self.assertEqual(f(3), 4)

    def test_rand(self):
        codestr = """
        from __static__ import rand, RAND_MAX, box, int64

        def test():
            x: int64 = rand()
            return box(x)
        """
        with self.in_module(codestr, code_gen=StaticCodeGenerator) as mod:
            test = mod["test"]
            self.assertEqual(type(test()), int)

    def test_rand_max_inlined(self):
        codestr = """
            from __static__ import rand, RAND_MAX, box, int64

            def f() -> int:
                x: int64 = rand() // int64(RAND_MAX)
                return box(x)
        """
        with self.in_module(codestr) as mod:
            f = mod["f"]
            self.assertInBytecode(f, "PRIMITIVE_LOAD_CONST")
            self.assertIsInstance(f(), int)

    def test_array_get_primitive_idx(self):
        codestr = """
            from __static__ import Array, int8, box

            def m() -> int:
                content = list(range(121))
                a = Array[int8](content)
                return box(a[int8(111)])
        """
        c = self.compile(codestr, StaticCodeGenerator, modname="foo.py")
        m = self.find_code(c, "m")
        self.assertInBytecode(m, "PRIMITIVE_LOAD_CONST", (111, TYPED_INT8))
        self.assertInBytecode(m, "SEQUENCE_GET", SEQ_ARRAY_INT8)
        with self.in_module(codestr, code_gen=StaticCodeGenerator) as mod:
            m = mod["m"]
            actual = m()
            self.assertEqual(actual, 111)

    def test_array_get_nonprimitive_idx(self):
        codestr = """
            from __static__ import Array, int8, box

            def m() -> int:
                content = list(range(121))
                a = Array[int8](content)
                return box(a[111])
        """
        c = self.compile(codestr, StaticCodeGenerator, modname="foo.py")
        m = self.find_code(c, "m")
        self.assertInBytecode(m, "LOAD_CONST", 111)
        self.assertNotInBytecode(m, "PRIMITIVE_LOAD_CONST")
        self.assertInBytecode(m, "INT_UNBOX")
        self.assertInBytecode(m, "SEQUENCE_GET", SEQ_ARRAY_INT8)
        with self.in_module(codestr, code_gen=StaticCodeGenerator) as mod:
            m = mod["m"]
            actual = m()
            self.assertEqual(actual, 111)

    def test_array_get_failure(self):
        codestr = """
            from __static__ import Array, int8, box

            def m() -> int:
                a = Array[int8]([1, 3, -5])
                return box(a[20])
        """
        with self.in_module(codestr, code_gen=StaticCodeGenerator) as mod:
            m = mod["m"]
            with self.assertRaisesRegex(IndexError, "index out of range"):
                m()

    def test_array_get_negative_idx(self):
        codestr = """
            from __static__ import Array, int8, box

            def m() -> int:
                a = Array[int8]([1, 3, -5])
                return box(a[-1])
        """
        with self.in_module(codestr, code_gen=StaticCodeGenerator) as mod:
            m = mod["m"]
            self.assertEqual(m(), -5)

    def test_array_set_signed(self):
        int_types = [
            "int8",
            "int16",
            "int32",
            "int64",
        ]
        seq_types = {
            "int8": SEQ_ARRAY_INT8,
            "int16": SEQ_ARRAY_INT16,
            "int32": SEQ_ARRAY_INT32,
            "int64": SEQ_ARRAY_INT64,
        }
        signs = ["-", ""]
        value = 77

        for type, sign in itertools.product(int_types, signs):
            codestr = f"""
                from __static__ import Array, {type}

                def m() -> Array[{type}]:
                    a = Array[{type}]([1, 3, -5])
                    a[1] = {sign}{value}
                    return a
            """
            with self.subTest(type=type, sign=sign):
                val = -value if sign else value
                c = self.compile(codestr, StaticCodeGenerator, modname="foo.py")
                m = self.find_code(c, "m")
                self.assertInBytecode(
                    m, "PRIMITIVE_LOAD_CONST", (val, prim_name_to_type[type])
                )
                self.assertInBytecode(m, "LOAD_CONST", 1)
                self.assertInBytecode(m, "SEQUENCE_SET", seq_types[type])
                with self.in_module(codestr, code_gen=StaticCodeGenerator) as mod:
                    m = mod["m"]
                    if sign:
                        expected = -value
                    else:
                        expected = value
                    result = m()
                    self.assertEqual(
                        result,
                        array("q", [1, expected, -5]),
                        f"Failing case: {type}, {sign}",
                    )

    def test_array_set_unsigned(self):
        uint_types = [
            "uint8",
            "uint16",
            "uint32",
            "uint64",
        ]
        value = 77
        seq_types = {
            "uint8": SEQ_ARRAY_UINT8,
            "uint16": SEQ_ARRAY_UINT16,
            "uint32": SEQ_ARRAY_UINT32,
            "uint64": SEQ_ARRAY_UINT64,
        }
        for type in uint_types:
            codestr = f"""
                from __static__ import Array, {type}

                def m() -> Array[{type}]:
                    a = Array[{type}]([1, 3, 5])
                    a[1] = {value}
                    return a
            """
            with self.subTest(type=type):
                c = self.compile(codestr, StaticCodeGenerator, modname="foo.py")
                m = self.find_code(c, "m")
                self.assertInBytecode(
                    m, "PRIMITIVE_LOAD_CONST", (value, prim_name_to_type[type])
                )
                self.assertInBytecode(m, "LOAD_CONST", 1)
                self.assertInBytecode(m, "SEQUENCE_SET", seq_types[type])
                with self.in_module(codestr, code_gen=StaticCodeGenerator) as mod:
                    m = mod["m"]
                    expected = value
                    result = m()
                    self.assertEqual(
                        result, array("q", [1, expected, 5]), f"Failing case: {type}"
                    )

    def test_array_set_negative_idx(self):
        codestr = """
            from __static__ import Array, int8

            def m() -> Array[int8]:
                a = Array[int8]([1, 3, -5])
                a[-2] = 7
                return a
        """
        c = self.compile(codestr, StaticCodeGenerator, modname="foo.py")
        m = self.find_code(c, "m")
        self.assertInBytecode(m, "PRIMITIVE_LOAD_CONST", (7, TYPED_INT8))
        self.assertInBytecode(m, "LOAD_CONST", -2)
        self.assertInBytecode(m, "SEQUENCE_SET", SEQ_ARRAY_INT8)
        with self.in_module(codestr, code_gen=StaticCodeGenerator) as mod:
            m = mod["m"]
            self.assertEqual(m(), array("h", [1, 7, -5]))

    def test_array_set_failure(self) -> object:
        codestr = """
            from __static__ import Array, int8

            def m() -> Array[int8]:
                a = Array[int8]([1, 3, -5])
                a[-100] = 7
                return a
        """
        c = self.compile(codestr, StaticCodeGenerator, modname="foo.py")
        m = self.find_code(c, "m")
        self.assertInBytecode(m, "PRIMITIVE_LOAD_CONST", (7, TYPED_INT8))
        self.assertInBytecode(m, "SEQUENCE_SET", SEQ_ARRAY_INT8)
        with self.in_module(codestr, code_gen=StaticCodeGenerator) as mod:
            m = mod["m"]
            with self.assertRaisesRegex(IndexError, "index out of range"):
                m()

    def test_array_set_failure_invalid_subscript(self):
        codestr = """
            from __static__ import Array, int8

            def x():
                return object()

            def m() -> Array[int8]:
                a = Array[int8]([1, 3, -5])
                a[x()] = 7
                return a
        """
        c = self.compile(codestr, StaticCodeGenerator, modname="foo.py")
        m = self.find_code(c, "m")
        self.assertInBytecode(m, "PRIMITIVE_LOAD_CONST", (7, TYPED_INT8))
        with self.in_module(codestr, code_gen=StaticCodeGenerator) as mod:
            m = mod["m"]
            with self.assertRaisesRegex(TypeError, "array indices must be integers"):
                m()

    def test_fast_len_list(self):
        codestr = """
        def f():
            l = [1, 2, 3, 4, 5, 6, 7]
            return len(l)
        """
        c = self.compile(codestr, StaticCodeGenerator, modname="foo.py")
        f = self.find_code(c, "f")
        self.assertInBytecode(f, "FAST_LEN", FAST_LEN_LIST)
        with self.in_module(codestr, code_gen=StaticCodeGenerator) as mod:
            f = mod["f"]
            self.assertEqual(f(), 7)

    def test_fast_len_str(self):
        codestr = """
        def f():
            l = "my str!"
            return len(l)
        """
        c = self.compile(codestr, StaticCodeGenerator, modname="foo.py")
        f = self.find_code(c, "f")
        self.assertInBytecode(f, "FAST_LEN", FAST_LEN_STR)
        with self.in_module(codestr, code_gen=StaticCodeGenerator) as mod:
            f = mod["f"]
            self.assertEqual(f(), 7)

    def test_fast_len_str_unicode_chars(self):
        codestr = """
        def f():
            l = "\U0001F923"  # ROFL emoji
            return len(l)
        """
        c = self.compile(codestr, StaticCodeGenerator, modname="foo.py")
        f = self.find_code(c, "f")
        self.assertInBytecode(f, "FAST_LEN", FAST_LEN_STR)
        with self.in_module(codestr, code_gen=StaticCodeGenerator) as mod:
            f = mod["f"]
            self.assertEqual(f(), 1)

    def test_fast_len_tuple(self):
        codestr = """
        def f(a, b):
            l = (a, b)
            return len(l)
        """
        c = self.compile(codestr, StaticCodeGenerator, modname="foo.py")
        f = self.find_code(c, "f")
        self.assertInBytecode(f, "FAST_LEN", FAST_LEN_TUPLE)
        with self.in_module(codestr, code_gen=StaticCodeGenerator) as mod:
            f = mod["f"]
            self.assertEqual(f("a", "b"), 2)

    def test_fast_len_set(self):
        codestr = """
        def f(a, b):
            l = {a, b}
            return len(l)
        """
        c = self.compile(codestr, StaticCodeGenerator, modname="foo.py")
        f = self.find_code(c, "f")
        self.assertInBytecode(f, "FAST_LEN", FAST_LEN_SET)
        with self.in_module(codestr, code_gen=StaticCodeGenerator) as mod:
            f = mod["f"]
            self.assertEqual(f("a", "b"), 2)

    def test_fast_len_dict(self):
        codestr = """
        def f():
            l = {1: 'a', 2: 'b', 3: 'c', 4: 'd'}
            return len(l)
        """
        c = self.compile(codestr, StaticCodeGenerator, modname="foo.py")
        f = self.find_code(c, "f")
        self.assertInBytecode(f, "FAST_LEN", FAST_LEN_DICT)
        with self.in_module(codestr, code_gen=StaticCodeGenerator) as mod:
            f = mod["f"]
            self.assertEqual(f(), 4)

    def test_fast_len_conditional_list(self):
        codestr = """
            def f(n: int) -> bool:
                l = [i for i in range(n)]
                if l:
                    return True
                return False
            """
        c = self.compile(codestr, StaticCodeGenerator, modname="foo.py")
        f = self.find_code(c, "f")
        self.assertInBytecode(f, "FAST_LEN", FAST_LEN_LIST)
        with self.in_module(codestr, code_gen=StaticCodeGenerator) as mod:
            f = mod["f"]
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
        c = self.compile(codestr, StaticCodeGenerator, modname="foo.py")
        f = self.find_code(c, "f")
        self.assertInBytecode(f, "FAST_LEN", FAST_LEN_STR)
        with self.in_module(codestr, code_gen=StaticCodeGenerator) as mod:
            f = mod["f"]
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
        c = self.compile(codestr, StaticCodeGenerator, modname="foo.py")
        f = self.find_code(c, "f")
        self.assertInBytecode(f, "FAST_LEN", FAST_LEN_LIST)
        with self.in_module(codestr, code_gen=StaticCodeGenerator) as mod:
            f = mod["f"]
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
        c = self.compile(codestr, StaticCodeGenerator, modname="foo.py")
        f = self.find_code(c, "f")
        self.assertInBytecode(f, "FAST_LEN", FAST_LEN_STR)
        with self.in_module(codestr, code_gen=StaticCodeGenerator) as mod:
            f = mod["f"]
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
        c = self.compile(codestr, StaticCodeGenerator, modname="foo.py")
        f = self.find_code(c, "f")
        self.assertInBytecode(f, "FAST_LEN", FAST_LEN_TUPLE)
        with self.in_module(codestr, code_gen=StaticCodeGenerator) as mod:
            f = mod["f"]
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
        c = self.compile(codestr, StaticCodeGenerator, modname="foo.py")
        f = self.find_code(c, "f")
        self.assertInBytecode(f, "FAST_LEN", FAST_LEN_SET)
        with self.in_module(codestr, code_gen=StaticCodeGenerator) as mod:
            f = mod["f"]
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
        c = self.compile(codestr, StaticCodeGenerator, modname="foo.py")
        f = self.find_code(c, "f")
        self.assertInBytecode(f, "FAST_LEN", FAST_LEN_TUPLE)
        with self.in_module(codestr, code_gen=StaticCodeGenerator) as mod:
            f = mod["f"]
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
        c = self.compile(codestr, StaticCodeGenerator, modname="foo.py")
        f = self.find_code(c, "f")
        self.assertInBytecode(f, "FAST_LEN", FAST_LEN_SET)
        with self.in_module(codestr, code_gen=StaticCodeGenerator) as mod:
            f = mod["f"]
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
        c = self.compile(codestr, StaticCodeGenerator, modname="foo.py")
        f = self.find_code(c, "f")
        self.assertInBytecode(f, "FAST_LEN", FAST_LEN_DICT)
        with self.in_module(codestr, code_gen=StaticCodeGenerator) as mod:
            f = mod["f"]
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
        c = self.compile(codestr, StaticCodeGenerator, modname="foo.py")
        f = self.find_code(c, "f")
        self.assertInBytecode(f, "FAST_LEN", FAST_LEN_LIST | FAST_LEN_INEXACT)
        with self.in_module(codestr, code_gen=StaticCodeGenerator) as mod:
            f = mod["f"]
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
        c = self.compile(codestr, StaticCodeGenerator, modname="foo.py")
        f = self.find_code(c, "f")
        self.assertInBytecode(f, "FAST_LEN", FAST_LEN_STR | FAST_LEN_INEXACT)
        with self.in_module(codestr, code_gen=StaticCodeGenerator) as mod:
            f = mod["f"]
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
        c = self.compile(codestr, StaticCodeGenerator, modname="foo.py")
        f = self.find_code(c, "f")
        self.assertInBytecode(f, "FAST_LEN", FAST_LEN_TUPLE | FAST_LEN_INEXACT)
        with self.in_module(codestr, code_gen=StaticCodeGenerator) as mod:
            f = mod["f"]
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
        c = self.compile(codestr, StaticCodeGenerator, modname="foo.py")
        f = self.find_code(c, "f")
        self.assertInBytecode(f, "FAST_LEN", FAST_LEN_SET | FAST_LEN_INEXACT)
        with self.in_module(codestr, code_gen=StaticCodeGenerator) as mod:
            f = mod["f"]
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
        c = self.compile(codestr, StaticCodeGenerator, modname="foo.py")
        f = self.find_code(c, "f")
        # Since the list is given to z(), do not optimize the check
        # with FAST_LEN
        self.assertNotInBytecode(f, "FAST_LEN")
        with self.in_module(codestr, code_gen=StaticCodeGenerator) as mod:
            f = mod["f"]
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
        c = self.compile(codestr, StaticCodeGenerator, modname="foo.py")
        f = self.find_code(c, "f")
        # Since the list is given to z(), do not optimize the check
        # with FAST_LEN
        self.assertNotInBytecode(f, "FAST_LEN")
        with self.in_module(codestr, code_gen=StaticCodeGenerator) as mod:
            f = mod["f"]
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
        c = self.compile(codestr, StaticCodeGenerator, modname="foo.py")
        f = self.find_code(c, "f")
        # Since the tuple is given to z(), do not optimize the check
        # with FAST_LEN
        self.assertNotInBytecode(f, "FAST_LEN")
        with self.in_module(codestr, code_gen=StaticCodeGenerator) as mod:
            f = mod["f"]
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
        c = self.compile(codestr, StaticCodeGenerator, modname="foo.py")
        f = self.find_code(c, "f")
        # Since the set is given to z(), do not optimize the check
        # with FAST_LEN
        self.assertNotInBytecode(f, "FAST_LEN")
        with self.in_module(codestr, code_gen=StaticCodeGenerator) as mod:
            f = mod["f"]
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
        c = self.compile(codestr, StaticCodeGenerator, modname="foo.py")
        f = self.find_code(c, "f")
        # Since the dict is given to z(), do not optimize the check
        # with FAST_LEN
        self.assertNotInBytecode(f, "FAST_LEN")
        with self.in_module(codestr, code_gen=StaticCodeGenerator) as mod:
            f = mod["f"]
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
        c = self.compile(codestr, StaticCodeGenerator, modname="foo.py")
        f = self.find_code(c, "f")
        self.assertNotInBytecode(f, "FAST_LEN")
        with self.in_module(codestr, code_gen=StaticCodeGenerator) as mod:
            f = mod["f"]
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
        c = self.compile(codestr, StaticCodeGenerator, modname="foo.py")
        f = self.find_code(c, "f")
        self.assertNotInBytecode(f, "FAST_LEN")
        with self.in_module(codestr, code_gen=StaticCodeGenerator) as mod:
            f = mod["f"]
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
        c = self.compile(codestr, StaticCodeGenerator, modname="foo.py")
        f = self.find_code(c, "f")
        self.assertNotInBytecode(f, "FAST_LEN")
        with self.in_module(codestr, code_gen=StaticCodeGenerator) as mod:
            f = mod["f"]
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
        c = self.compile(codestr, StaticCodeGenerator, modname="foo.py")
        f = self.find_code(c, "f")
        self.assertNotInBytecode(f, "FAST_LEN")
        with self.in_module(codestr, code_gen=StaticCodeGenerator) as mod:
            f = mod["f"]
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
        c = self.compile(codestr, StaticCodeGenerator, modname="foo.py")
        f = self.find_code(c, "f")
        self.assertNotInBytecode(f, "FAST_LEN")
        with self.in_module(codestr, code_gen=StaticCodeGenerator) as mod:
            f = mod["f"]
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
        c = self.compile(codestr, StaticCodeGenerator, modname="foo.py")
        f = self.find_code(c, "f")
        self.assertInBytecode(f, "FAST_LEN", FAST_LEN_LIST | FAST_LEN_INEXACT)
        with self.in_module(codestr, code_gen=StaticCodeGenerator) as mod:
            f = mod["f"]
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
        c = self.compile(codestr, StaticCodeGenerator, modname="foo.py")
        f = self.find_code(c, "f")
        self.assertInBytecode(f, "FAST_LEN", FAST_LEN_STR | FAST_LEN_INEXACT)
        with self.in_module(codestr, code_gen=StaticCodeGenerator) as mod:
            f = mod["f"]
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
        c = self.compile(codestr, StaticCodeGenerator, modname="foo.py")
        f = self.find_code(c, "f")
        self.assertInBytecode(f, "FAST_LEN", FAST_LEN_TUPLE | FAST_LEN_INEXACT)
        with self.in_module(codestr, code_gen=StaticCodeGenerator) as mod:
            f = mod["f"]
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
        c = self.compile(codestr, StaticCodeGenerator, modname="foo.py")
        f = self.find_code(c, "f")
        self.assertInBytecode(f, "FAST_LEN", FAST_LEN_DICT | FAST_LEN_INEXACT)
        with self.in_module(codestr, code_gen=StaticCodeGenerator) as mod:
            f = mod["f"]
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
        c = self.compile(codestr, StaticCodeGenerator, modname="foo.py")
        f = self.find_code(c, "f")
        self.assertInBytecode(f, "FAST_LEN", FAST_LEN_SET | FAST_LEN_INEXACT)
        with self.in_module(codestr, code_gen=StaticCodeGenerator) as mod:
            f = mod["f"]
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
        with self.in_module(codestr, code_gen=StaticCodeGenerator) as mod:
            outer = mod["outer"]
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
        with self.in_module(codestr, code_gen=StaticCodeGenerator) as mod:
            outer = mod["outer"]
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
        with self.in_module(codestr, code_gen=StaticCodeGenerator) as mod:
            outer = mod["outer"]
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
        with self.in_module(codestr, code_gen=StaticCodeGenerator) as mod:
            outer = mod["outer"]
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
        with self.in_module(codestr, code_gen=StaticCodeGenerator) as mod:
            outer = mod["outer"]
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
        with self.in_module(codestr, code_gen=StaticCodeGenerator) as mod:
            outer = mod["outer"]
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
        with self.in_module(codestr, code_gen=StaticCodeGenerator) as mod:
            outer = mod["outer"]
            self.assertEqual(outer(3, 2, z="hi"), (3, 2, "hi"))

    def test_str_split(self):
        codestr = """
            def get_str() -> str:
                return "something here"

            def test() -> str:
                a, b = get_str().split(None, 1)
                return b
        """
        with self.in_module(codestr, code_gen=StaticCodeGenerator) as mod:
            test = mod["test"]
            self.assertEqual(test(), "here")

    def test_vtable_shadow_builtin_subclass_after_init(self):
        """Shadowing methods on subclass of list after vtables are inited."""

        class MyList(list):
            pass

        def myreverse(self):
            return 1

        codestr = """
            def f(l: list):
                l.reverse()
                return l
        """
        f = self.find_code(self.compile(codestr), "f")
        self.assertInBytecode(
            f, "INVOKE_METHOD", ((("builtins", "list", "reverse"), 0))
        )
        with self.in_module(codestr, code_gen=StaticCodeGenerator) as mod:
            # Now cause vtables to be inited
            self.assertEqual(mod["f"]([1, 2]), [2, 1])

            # And now patch
            MyList.reverse = myreverse

            self.assertEqual(MyList().reverse(), 1)

    def test_vtable_shadow_builtin_subclass_before_init(self):
        """Shadowing methods on subclass of list before vtables are inited."""
        # Create a subclass of list...
        class MyList(list):
            pass

        def myreverse(self):
            return 1

        # ... and override a slot from list with a non-static func
        MyList.reverse = myreverse

        codestr = """
            def f(l: list):
                l.reverse()
                return l
        """
        f = self.find_code(self.compile(codestr), "f")
        self.assertInBytecode(
            f, "INVOKE_METHOD", ((("builtins", "list", "reverse"), 0))
        )
        with self.in_module(codestr, code_gen=StaticCodeGenerator) as mod:
            # Now cause vtables to be inited
            self.assertEqual(mod["f"]([1, 2]), [2, 1])

        # ... and this should not blow up when we remove the override.
        del MyList.reverse

        self.assertEqual(MyList().reverse(), None)

    def test_vtable_shadow_static_subclass(self):
        """Shadowing methods of a static type before its inited should not bypass typechecks."""
        # Define a static type and shadow a subtype method before invoking.
        codestr = """
            class StaticType:
                def foo(self) -> int:
                    return 1

            class SubType(StaticType):
                pass

            def goodfoo(self):
                return 2

            SubType.foo = goodfoo

            def f(x: StaticType) -> int:
                return x.foo()
        """
        f = self.find_code(self.compile(codestr), "f")
        self.assertInBytecode(
            f, "INVOKE_METHOD", ((("<module>", "StaticType", "foo"), 0))
        )
        with self.in_module(codestr, code_gen=StaticCodeGenerator) as mod:
            SubType = mod["SubType"]
            # Now invoke:
            self.assertEqual(mod["f"](SubType()), 2)

            # And replace the function again, forcing us to find the right slot type:
            def badfoo(self):
                return "foo"

            SubType.foo = badfoo

            with self.assertRaisesRegex(TypeError, "expected int, got str"):
                mod["f"](SubType())

    def test_vtable_shadow_static_subclass_nonstatic_patch(self):
        """Shadowing methods of a static type before its inited should not bypass typechecks."""
        code1 = """
            def nonstaticfoo(self):
                return 2
        """
        with self.in_module(
            code1, code_gen=PythonCodeGenerator, name="nonstatic"
        ) as mod1:
            # Define a static type and shadow a subtype method with a non-static func before invoking.
            codestr = """
                from nonstatic import nonstaticfoo

                class StaticType:
                    def foo(self) -> int:
                        return 1

                class SubType(StaticType):
                    pass

                SubType.foo = nonstaticfoo

                def f(x: StaticType) -> int:
                    return x.foo()

                def badfoo(self):
                    return "foo"
            """
            code = self.compile(codestr)
            f = self.find_code(code, "f")
            self.assertInBytecode(
                f, "INVOKE_METHOD", ((("<module>", "StaticType", "foo"), 0))
            )
            with self.in_module(codestr, code_gen=StaticCodeGenerator) as mod:
                SubType = mod["SubType"]
                badfoo = mod["badfoo"]

                # And replace the function again, forcing us to find the right slot type:
                SubType.foo = badfoo

                with self.assertRaisesRegex(TypeError, "expected int, got str"):
                    mod["f"](SubType())

    def test_vtable_shadow_grandparent(self):
        codestr = """
            class Base:
                def foo(self) -> int:
                    return 1

            class Sub(Base):
                pass

            class Grand(Sub):
                pass

            def f(x: Base) -> int:
                return x.foo()

            def grandfoo(self):
                return "foo"
        """
        f = self.find_code(self.compile(codestr), "f")
        self.assertInBytecode(f, "INVOKE_METHOD", ((("<module>", "Base", "foo"), 0)))
        with self.in_module(codestr, code_gen=StaticCodeGenerator) as mod:
            Grand = mod["Grand"]
            grandfoo = mod["grandfoo"]
            f = mod["f"]

            # init vtables
            self.assertEqual(f(Grand()), 1)

            # patch in an override of the grandparent method
            Grand.foo = grandfoo

            with self.assertRaisesRegex(TypeError, "expected int, got str"):
                f(Grand())

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
        with self.in_module(codestr, code_gen=StaticCodeGenerator) as mod:
            f = mod["f"]
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
        with self.in_module(codestr, code_gen=StaticCodeGenerator) as mod:
            f = mod["f"]
            self.assertNotInBytecode(f, "FOR_ITER")
            self.assertEqual(f(4), [i + 1 for i in range(4)])

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
        with self.in_module(codestr, code_gen=StaticCodeGenerator) as mod:
            f = mod["f"]
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
        with self.in_module(codestr, code_gen=StaticCodeGenerator) as mod:
            f = mod["f"]
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
        with self.in_module(codestr, code_gen=StaticCodeGenerator) as mod:
            f = mod["f"]
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
        with self.in_module(codestr, code_gen=StaticCodeGenerator) as mod:
            f = mod["f"]
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
        with self.in_module(codestr, code_gen=StaticCodeGenerator) as mod:
            f = mod["f"]
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
        with self.in_module(codestr, code_gen=StaticCodeGenerator) as mod:
            f = mod["f"]
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
        with self.in_module(codestr, code_gen=StaticCodeGenerator) as mod:
            f = mod["f"]
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
            f = mod["f"]
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
            f = mod["f"]
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
            f = mod["f"]
            self.assertNotInBytecode(f, "FOR_ITER")
            self.assertInBytecode(f, "REFINE_TYPE", ("builtins", "list"))

    def test_min(self):
        codestr = """
            def f(a: int, b: int) -> int:
                return min(a, b)
        """
        with self.in_module(codestr, code_gen=StaticCodeGenerator) as mod:
            f = mod["f"]
            self.assertInBytecode(f, "COMPARE_OP", "<=")
            self.assertInBytecode(f, "POP_JUMP_IF_FALSE")
            self.assertEqual(f(1, 3), 1)
            self.assertEqual(f(3, 1), 1)

    def test_min_stability(self):
        codestr = """
            def f(a: int, b: int) -> int:
                return min(a, b)
        """
        with self.in_module(codestr, code_gen=StaticCodeGenerator) as mod:
            f = mod["f"]
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
        with self.in_module(codestr, code_gen=StaticCodeGenerator) as mod:
            f = mod["f"]
            self.assertInBytecode(f, "COMPARE_OP", ">=")
            self.assertInBytecode(f, "POP_JUMP_IF_FALSE")
            self.assertEqual(f(1, 3), 3)
            self.assertEqual(f(3, 1), 3)

    def test_max_stability(self):
        codestr = """
            def f(a: int, b: int) -> int:
                return max(a, b)
        """
        with self.in_module(codestr, code_gen=StaticCodeGenerator) as mod:
            f = mod["f"]
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
            self.compile(codestr, StaticCodeGenerator, modname="foo.py")

    def test_extremum_non_specialization_kwarg(self):
        codestr = """
            def f() -> None:
                a = "4"
                b = "5"
                min(a, b, key=int)
        """
        with self.in_module(codestr, code_gen=StaticCodeGenerator) as mod:
            f = mod["f"]
            self.assertNotInBytecode(f, "COMPARE_OP")
            self.assertNotInBytecode(f, "POP_JUMP_IF_FALSE")

    def test_extremum_non_specialization_stararg(self):
        codestr = """
            def f() -> None:
                a = [3, 4]
                min(*a)
        """
        with self.in_module(codestr, code_gen=StaticCodeGenerator) as mod:
            f = mod["f"]
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
        with self.in_module(codestr, code_gen=StaticCodeGenerator) as mod:
            f = mod["f"]
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
        with self.in_module(codestr, code_gen=StaticCodeGenerator) as mod:
            f1 = mod["f1"]
            l = []
            f1(l)
            self.assertEqual(l, ["hi"])

    def test_cbool(self):
        for b in ("True", "False"):
            codestr = f"""
            from __static__ import cbool

            def f() -> int:
                x: cbool = {b}
                if x:
                    return 1
                else:
                    return 2
            """
            with self.subTest(b=b):
                with self.in_module(codestr, code_gen=StaticCodeGenerator) as mod:
                    f = mod["f"]
                    self.assertInBytecode(f, "PRIMITIVE_LOAD_CONST")
                    self.assertInBytecode(
                        f, "STORE_LOCAL", (0, ("__static__", "cbool"))
                    )
                    self.assertInBytecode(f, "POP_JUMP_IF_ZERO")
                    self.assertEqual(f(), 1 if b == "True" else 2)

    def test_cbool_field(self):
        codestr = """
            from __static__ import cbool

            class C:
                def __init__(self, x: cbool) -> None:
                    self.x: cbool = x

            def f(c: C):
                if c.x:
                    return True
                return False
        """
        with self.in_module(codestr) as mod:
            f, C = mod["f"], mod["C"]
            self.assertInBytecode(f, "LOAD_FIELD", ("test_cbool_field", "C", "x"))
            self.assertInBytecode(f, "POP_JUMP_IF_ZERO")
            self.assertIs(C(True).x, True)
            self.assertIs(C(False).x, False)
            self.assertIs(f(C(True)), True)
            self.assertIs(f(C(False)), False)

    def test_cbool_cast(self):
        codestr = """
        from __static__ import cbool

        def f(y: bool) -> int:
            x: cbool = y
            if x:
                return 1
            else:
                return 2
        """
        with self.assertRaisesRegex(TypedSyntaxError, type_mismatch("bool", "cbool")):
            self.compile(codestr, StaticCodeGenerator, modname="foo")

    def test_primitive_compare_returns_cbool(self):
        codestr = """
            from __static__ import cbool, int64

            def f(x: int64, y: int64) -> cbool:
                return x == y
        """
        with self.in_module(codestr) as mod:
            f = mod["f"]
            self.assertIs(f(1, 1), True)
            self.assertIs(f(1, 2), False)

    def test_no_cbool_math(self):
        codestr = """
            from __static__ import cbool

            def f(x: cbool, y: cbool) -> cbool:
                return x + y
        """
        with self.assertRaisesRegex(
            TypedSyntaxError, "cbool is not a valid operand type for add"
        ):
            self.compile(codestr)

    def test_chkdict_del(self):
        codestr = """
        def f():
            x = {}
            x[1] = "a"
            x[2] = "b"
            del x[1]
            return x
        """
        with self.in_module(codestr, code_gen=StaticCodeGenerator) as mod:
            f = mod["f"]
            ret = f()
            self.assertNotIn(1, ret)
            self.assertIn(2, ret)

    def test_final_constant_folding_int(self):
        codestr = """
        from typing import Final

        X: Final[int] = 1337

        def plus_1337(i: int) -> int:
            return i + X
        """
        with self.in_module(codestr, code_gen=StaticCodeGenerator) as mod:
            plus_1337 = mod["plus_1337"]
            self.assertInBytecode(plus_1337, "LOAD_CONST", 1337)
            self.assertNotInBytecode(plus_1337, "LOAD_GLOBAL")
            self.assertEqual(plus_1337(3), 1340)

    def test_final_constant_folding_bool(self):
        codestr = """
        from typing import Final

        X: Final[bool] = True

        def f() -> bool:
            return not X
        """
        with self.in_module(codestr, code_gen=StaticCodeGenerator) as mod:
            f = mod["f"]
            self.assertInBytecode(f, "LOAD_CONST", True)
            self.assertNotInBytecode(f, "LOAD_GLOBAL")
            self.assertFalse(f())

    def test_final_constant_folding_str(self):
        codestr = """
        from typing import Final

        X: Final[str] = "omg"

        def f() -> str:
            return X[1]
        """
        with self.in_module(codestr, code_gen=StaticCodeGenerator) as mod:
            f = mod["f"]
            self.assertInBytecode(f, "LOAD_CONST", "omg")
            self.assertNotInBytecode(f, "LOAD_GLOBAL")
            self.assertEqual(f(), "m")

    def test_final_constant_folding_disabled_on_nonfinals(self):
        codestr = """
        from typing import Final

        X: str = "omg"

        def f() -> str:
            return X[1]
        """
        with self.in_module(codestr, code_gen=StaticCodeGenerator) as mod:
            f = mod["f"]
            self.assertNotInBytecode(f, "LOAD_CONST", "omg")
            self.assertInBytecode(f, "LOAD_GLOBAL", "X")
            self.assertEqual(f(), "m")

    def test_final_constant_folding_disabled_on_nonconstant_finals(self):
        codestr = """
        from typing import Final

        def p() -> str:
            return "omg"

        X: Final[str] = p()

        def f() -> str:
            return X[1]
        """
        with self.in_module(codestr, code_gen=StaticCodeGenerator) as mod:
            f = mod["f"]
            self.assertNotInBytecode(f, "LOAD_CONST", "omg")
            self.assertInBytecode(f, "LOAD_GLOBAL", "X")
            self.assertEqual(f(), "m")

    def test_final_constant_folding_shadowing(self):
        codestr = """
        from typing import Final

        X: Final[str] = "omg"

        def f() -> str:
            X = "lol"
            return X[1]
        """
        with self.in_module(codestr, code_gen=StaticCodeGenerator) as mod:
            f = mod["f"]
            self.assertInBytecode(f, "LOAD_CONST", "lol")
            self.assertNotInBytecode(f, "LOAD_GLOBAL", "omg")
            self.assertEqual(f(), "o")

    def test_final_constant_folding_in_module_scope(self):
        codestr = """
        from typing import Final

        X: Final[int] = 21
        y = X + 3
        """
        c = self.compile(codestr, generator=StaticCodeGenerator, modname="foo.py")
        self.assertNotInBytecode(c, "LOAD_NAME", "X")
        with self.in_module(codestr, code_gen=StaticCodeGenerator) as mod:
            self.assertEqual(mod["y"], 24)

    def test_final_constant_in_module_scope(self):
        codestr = """
        from typing import Final

        X: Final[int] = 21
        """
        with self.in_module(codestr, code_gen=StaticCodeGenerator) as mod:
            self.assertEqual(mod["__final_constants__"], ("X",))

    def test_final_nonconstant_in_module_scope(self):
        codestr = """
        from typing import Final

        def p() -> str:
            return "omg"

        X: Final[str] = p()
        """
        with self.in_module(codestr, code_gen=StaticCodeGenerator) as mod:
            self.assertEqual(mod["__final_constants__"], ())

    def test_double_load_const(self):
        codestr = """
        from __static__ import double

        def t():
            pi: double = 3.14159
        """
        with self.in_module(codestr, code_gen=StaticCodeGenerator) as mod:
            t = mod["t"]
            self.assertInBytecode(t, "PRIMITIVE_LOAD_CONST", (3.14159, TYPED_DOUBLE))
            t()
            self.assert_jitted(t)

    def test_double_box(self):
        codestr = """
        from __static__ import double, box

        def t() -> float:
            pi: double = 3.14159
            return box(pi)
        """
        with self.in_module(codestr, code_gen=StaticCodeGenerator) as mod:
            t = mod["t"]
            self.assertInBytecode(t, "PRIMITIVE_LOAD_CONST", (3.14159, TYPED_DOUBLE))
            self.assertNotInBytecode(t, "CAST")
            self.assertEqual(t(), 3.14159)
            self.assert_jitted(t)

    def test_none_not(self):
        codestr = """
        def t() -> bool:
            x = None
            if not x:
                return True
            else:
                return False
        """
        with self.in_module(codestr, code_gen=StaticCodeGenerator) as mod:
            t = mod["t"]
            self.assertInBytecode(t, "POP_JUMP_IF_TRUE")
            self.assertTrue(t())

    def test_unbox_final_inlined(self):
        for func in ["unbox", "int64"]:
            with self.subTest(func=func):
                codestr = f"""
                from typing import Final
                from __static__ import int64, unbox

                MY_FINAL: Final[int] = 111

                def t() -> bool:
                    i: int64 = 64
                    if i < {func}(MY_FINAL):
                        return True
                    else:
                        return False
                """
                with self.in_module(codestr) as mod:
                    t = mod["t"]
                    self.assertInBytecode(t, "PRIMITIVE_LOAD_CONST", (111, TYPED_INT64))
                    self.assertEqual(t(), True)

    def test_augassign_inexact(self):
        codestr = """
        def something():
            return 3

        def t():
            a: int = something()

            b = 0
            b += a
            return b
        """
        with self.in_module(codestr, code_gen=StaticCodeGenerator) as mod:
            t = mod["t"]
            self.assertInBytecode(t, "INPLACE_ADD")
            self.assertEqual(t(), 3)

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

            def f(self):
                class G:
                    def y(self):
                        pass
                return G.y
        """
        with self.in_module(codestr, code_gen=StaticCodeGenerator) as mod:
            f = mod["f"]
            C = mod["C"]
            self.assertEqual(cinder._get_qualname(f.__code__), "f")

            self.assertEqual(cinder._get_qualname(C.x.__code__), "C.x")
            self.assertEqual(cinder._get_qualname(C.sm.__code__), "C.sm")
            self.assertEqual(cinder._get_qualname(C.cm.__code__), "C.cm")

            self.assertEqual(cinder._get_qualname(C().f().__code__), "C.f.<locals>.G.y")

    def test_refine_optional_name(self):
        codestr = """
        from typing import Optional

        def f(s: Optional[str]) -> bytes:
            return s.encode("utf-8") if s else b""
        """
        with self.in_module(codestr, code_gen=StaticCodeGenerator) as mod:
            f = mod["f"]
            self.assertEqual(f("A"), b"A")
            self.assertEqual(f(None), b"")

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
        with self.in_module(codestr, code_gen=StaticCodeGenerator) as mod:
            fn = mod["fn"]
            self.assertInBytecode(fn, "CALL_FUNCTION")
            self.assertNotInBytecode(fn, "INVOKE_FUNCTION")
            self.assertFalse(fn.__code__.co_flags & CO_STATICALLY_COMPILED)
            self.assertEqual(fn(), None)

            fn2 = mod["fn2"]
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
        with self.in_module(codestr, code_gen=StaticCodeGenerator) as mod:
            C = mod["C"]

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
        with self.in_module(codestr, code_gen=StaticCodeGenerator) as mod:
            C = mod["C"]
            fn = C.fn
            self.assertInBytecode(fn, "CALL_FUNCTION")
            self.assertNotInBytecode(fn, "INVOKE_FUNCTION")
            self.assertFalse(fn.__code__.co_flags & CO_STATICALLY_COMPILED)
            self.assertEqual(fn(), None)

            D = mod["D"]

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
        with self.in_module(codestr, code_gen=StaticCodeGenerator) as mod:
            C = mod["C"]
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

    def test_double_binop(self):
        tests = [
            (1.732, 2.0, "+", 3.732),
            (1.732, 2.0, "-", -0.268),
            (1.732, 2.0, "/", 0.866),
            (1.732, 2.0, "*", 3.464),
        ]

        if cinderjit is not None:
            # test for division by zero
            tests.append((1.732, 0.0, "/", float("inf")))

        for x, y, op, res in tests:
            codestr = f"""
            from __static__ import double, box
            def testfunc(tst):
                x: double = {x}
                y: double = {y}

                z: double = x {op} y
                return box(z)
            """
            with self.subTest(type=type, x=x, y=y, op=op, res=res):
                with self.in_module(codestr) as mod:
                    f = mod["testfunc"]
                    self.assertEqual(f(False), res, f"{type} {x} {op} {y} {res}")

    def test_primitive_stack_spill(self):
        # Create enough locals that some must get spilled to stack, to test
        # shuffling stack-spilled values across basic block transitions, and
        # field reads/writes with stack-spilled values. These can create
        # mem->mem moves that otherwise wouldn't exist, and trigger issues
        # like push/pop not supporting 8 or 32 bits on x64.
        varnames = string.ascii_lowercase[:20]
        sizes = ["uint8", "int16", "int32", "int64"]
        for size in sizes:
            indent = " " * 20
            attrs = f"\n{indent}".join(f"{var}: {size}" for var in varnames)
            inits = f"\n{indent}".join(
                f"{var}: {size} = {val}" for val, var in enumerate(varnames)
            )
            assigns = f"\n{indent}".join(f"val.{var} = {var}" for var in varnames)
            reads = f"\n{indent}".join(f"{var} = val.{var}" for var in varnames)
            indent = " " * 24
            incrs = f"\n{indent}".join(f"{var} += 1" for var in varnames)
            codestr = f"""
                from __static__ import {size}

                class C:
                    {attrs}

                def f(val: C, flag: {size}) -> {size}:
                    {inits}
                    if flag:
                        {incrs}
                    {assigns}
                    {reads}
                    return {' + '.join(varnames)}
            """
            with self.subTest(size=size):
                with self.in_module(codestr) as mod:
                    f, C = mod["f"], mod["C"]
                    c = C()
                    self.assertEqual(f(c, 0), sum(range(len(varnames))))
                    for val, var in enumerate(varnames):
                        self.assertEqual(getattr(c, var), val)

                    c = C()
                    self.assertEqual(f(c, 1), sum(range(len(varnames) + 1)))
                    for val, var in enumerate(varnames):
                        self.assertEqual(getattr(c, var), val + 1)


if __name__ == "__main__":
    unittest.main()
