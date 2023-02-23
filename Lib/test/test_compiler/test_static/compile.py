from __static__ import chkdict

import ast
import asyncio
import builtins
import cinder
import dis
import inspect
import re
import sys
import unittest
from cinder import StrictModule
from compiler.consts import CO_STATICALLY_COMPILED
from compiler.pycodegen import PythonCodeGenerator
from compiler.static import StaticCodeGenerator
from compiler.static.compiler import Compiler
from compiler.static.types import (
    FAST_LEN_DICT,
    FAST_LEN_INEXACT,
    FAST_LEN_LIST,
    FAST_LEN_SET,
    FAST_LEN_STR,
    FAST_LEN_TUPLE,
    Function,
    Object,
    TypedSyntaxError,
    TypeEnvironment,
    Value,
)
from io import StringIO
from os import path
from types import ModuleType
from typing import Callable, Optional, TypeVar
from unittest import skip, skipIf
from unittest.mock import patch

import xxclassloader

from .common import (
    add_fixed_module,
    bad_ret_type,
    disable_hir_inliner,
    StaticTestBase,
    type_mismatch,
)

try:
    import cinderjit
except ImportError:
    cinderjit = None

RICHARDS_PATH = path.join(
    path.dirname(__file__),
    "..",
    "..",
    "..",
    "..",
    "Tools",
    "benchmarks",
    "richards_static_lib.py",
)


def optional(type: str) -> str:
    return f"Optional[{type}]"


def init_xxclassloader():
    codestr = """
        from typing import Generic, TypeVar, _tp_cache
        from __static__ import set_type_static
        # Setup a test for typing
        T = TypeVar('T')
        U = TypeVar('U')


        class XXGeneric(Generic[T, U]):
            d = {}

            # __class_getitem__ doesn't properly re-write the code so
            # we want CHECK_ARGS to not check for the plain generic type
            def foo(self: object, t: T, u: U) -> str:
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
                set_type_static(XXGeneric.d[elem_type])
                return XXGeneric.d[elem_type]
    """

    comp = Compiler(StaticCodeGenerator)
    # We have to explicitly add_module before compile because we are doing
    # something odd here and compiling a module name that is already present
    # in the compiler's symbol table, so compile will skip decl visit.
    tree = ast.parse(inspect.cleandoc(codestr))
    tree = comp.add_module("xxclassloader", "", tree, optimize=0)
    code = comp.compile("xxclassloader", "", tree, optimize=0)
    d = {"<builtins>": builtins.__dict__}
    add_fixed_module(d)
    exec(code, d, d)

    xxclassloader.XXGeneric = d["XXGeneric"]


class StaticCompilationTests(StaticTestBase):
    @classmethod
    def setUpClass(cls):
        init_xxclassloader()
        cls.type_env = TypeEnvironment()

    @classmethod
    def tearDownClass(cls):
        del xxclassloader.XXGeneric

    def test_static_import_unknown(self) -> None:
        codestr = """
            from __static__ import does_not_exist
        """
        with self.assertRaises(TypedSyntaxError):
            self.compile(codestr, modname="foo")

    def test_static_import_star(self) -> None:
        codestr = """
            from __static__ import *
        """
        with self.assertRaises(TypedSyntaxError):
            self.compile(codestr, modname="foo")

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
            self.compile(codestr, modname="foo")

    def test_unannotated_assign_does_not_declare_type(self) -> None:
        codestr = """
            def f(flag):
                x = None
                if flag:
                    x = "foo"
                reveal_type(x)
        """
        with self.assertRaisesRegex(
            TypedSyntaxError,
            r"'x' has declared type 'dynamic' and local type 'Optional\[str\]'",
        ):
            self.compile(codestr)

    def test_unannotated_assign_no_later_declare(self) -> None:
        codestr = """
            def f(flag):
                x = None
                if flag:
                    x: str = "foo"
        """
        with self.assertRaisesRegex(
            TypedSyntaxError, r"Cannot redefine local variable x"
        ):
            self.compile(codestr)

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
            self.compile(codestr, modname="foo")

    def test_multiple_dynamic_base_class(self) -> None:
        codestr = """
        from something import A, B
        class C(A, B):
            def __init__(self):
                pass
        """
        self.compile(codestr)

    def test_bool_cast(self) -> None:
        codestr = """
            from __static__ import cast
            class D: pass

            def f(x) -> bool:
                y: bool = cast(bool, x)
                return y
        """
        self.compile(codestr, modname="foo")

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

    def test_typing_overload_recognized_decorator(self) -> None:
        codestr = """
            from typing import Optional, overload

            class C:
                @overload
                @classmethod
                def foo(self, x: int) -> int:
                    ...

                @classmethod
                def foo(self, x: Optional[int]) -> Optional[int]:
                    return x

            def f(x: int) -> Optional[int]:
                return C().foo(x)
        """
        self.assertReturns(codestr, "Optional[int]")

    def test_typing_overload_toplevel(self) -> None:
        """Typing overloads are ignored, don't cause member name conflict."""
        codestr = """
            from typing import Optional, overload

            @overload
            def bar(x: int) -> int:
                ...

            def bar(x: Optional[int]) -> Optional[int]:
                return x

            def f(x: int) -> Optional[int]:
                return bar(x)
        """
        self.assertReturns(codestr, "Optional[int]")

    def test_typing_overload_type(self) -> None:
        """Typing overloads are explicitly understood by the static compiler."""
        codestr = """
            from typing import overload

            reveal_type(overload)

        """
        with self.assertRaisesRegex(TypedSyntaxError, r"Type\[overload\]"):
            self.compile(codestr)

    def test_duplicate_function_replaces_class(self) -> None:
        codestr = """
            class X: pass
            def X(): pass
        """
        with self.assertRaisesRegex(
            TypedSyntaxError, "function conflicts with other member X in <module>"
        ):
            self.compile(codestr)

    def test_duplicate_function_replaces_function(self) -> None:
        codestr = """
            def f(): pass
            def f(): pass
        """
        with self.assertRaisesRegex(
            TypedSyntaxError,
            "function 'function <module>.f' conflicts with other member",
        ):
            self.compile(codestr)

    @skipIf(cinderjit is None, "not jitting")
    def test_deep_attr_chain(self):
        """this shouldn't explode exponentially"""
        codestr = """
        def f(x):
            return x.x.x.x.x.x.x

        """

        class C:
            def __init__(self):
                self.x = self

        orig_bind_attr = Object.bind_attr
        call_count = 0

        def bind_attr(*args):
            nonlocal call_count
            call_count += 1
            return orig_bind_attr(*args)

        with patch("compiler.static.types.Object.bind_attr", bind_attr):
            with self.in_module(codestr) as mod:
                f = mod.f
                x = C()
                self.assertEqual(f(x), x)
                # Initially this would be 63 when we were double visiting
                self.assertLess(call_count, 10)

    def test_exact_invoke_function(self):
        codestr = """
            def f() -> str:
                return ", ".join(['1','2','3'])
        """
        f = self.find_code(self.compile(codestr))
        with self.in_module(codestr) as mod:
            f = mod.f
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
            self.assertEqual(mod.f(), 6)

    def test_multiply_list_exact_by_int_reverse(self):
        codestr = """
            def f() -> int:
                l = 2 * [1, 2, 3]
                return len(l)
        """
        f = self.find_code(self.compile(codestr))
        self.assertInBytecode(f, "FAST_LEN", FAST_LEN_LIST)
        with self.in_module(codestr) as mod:
            self.assertEqual(mod.f(), 6)

    def test_compare_subclass(self):
        codestr = """
        class C: pass
        class D(C): pass

        x = C() > D()
        """
        code = self.compile(codestr)
        self.assertInBytecode(code, "COMPARE_OP")

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

        self.compile(codestr)

    def test_global_call_add(self) -> None:
        codestr = """
            X = ord(42)
            def f():
                y = X + 1
        """
        self.compile(codestr, modname="foo")

    def test_type_binder(self) -> None:
        def assert_expr_binds_to(
            exprstr: str, expected_type_callable: Callable[[TypeEnvironment], Value]
        ) -> None:
            value, comp = self.bind_expr(exprstr)
            self.assertEqual(value, expected_type_callable(comp.type_env))

        def assert_stmt_binds_to(
            stmtstr: str,
            expected_type_callable: Callable[[TypeEnvironment], Value],
            getter=lambda stmt: stmt,
        ) -> None:
            value, comp = self.bind_stmt(stmtstr, getter=getter)
            self.assertEqual(value, expected_type_callable(comp.type_env))

        self.assertEqual(repr(self.bind_expr("42")[0]), "<Literal[42]>")
        assert_expr_binds_to(
            "42.0", lambda type_env: type_env.float.exact_type().instance
        )
        assert_expr_binds_to(
            "'abc'", lambda type_env: type_env.str.exact_type().instance
        )
        assert_expr_binds_to(
            "b'abc'",
            lambda type_env: type_env.bytes.instance,
        )
        assert_expr_binds_to(
            "3j",
            lambda type_env: type_env.complex.exact_type().instance,
        )
        assert_expr_binds_to("None", lambda type_env: type_env.none.instance)
        self.assertEqual(repr(self.bind_expr("True")[0]), "<Literal[True]>")
        self.assertEqual(repr(self.bind_expr("False")[0]), "<Literal[False]>")
        assert_expr_binds_to(
            "...",
            lambda type_env: type_env.ellipsis.instance,
        )
        assert_expr_binds_to(
            "f''",
            lambda type_env: type_env.str.exact_type().instance,
        )
        assert_expr_binds_to(
            """
            x = 3
            f'{x}'
            """,
            lambda type_env: type_env.str.exact_type().instance,
        )

        assert_expr_binds_to(
            """
            def foo():
                return 541
            a = foo()
            a
            """,
            lambda type_env: type_env.DYNAMIC,
        )
        assert_expr_binds_to(
            """
            def foo():
                return 541
            a = foo()
            a.b
            """,
            lambda type_env: type_env.DYNAMIC,
        )
        assert_expr_binds_to(
            """
         def foo():
             return 541
         a, b = foo(), foo()
         a + b
         """,
            lambda type_env: type_env.DYNAMIC,
        )

        self.assertEqual(repr(self.bind_expr("1 + 2")[0]), "<Literal[3]>")
        self.assertEqual(repr(self.bind_expr("1 - 2")[0]), "<Literal[-1]>")
        self.assertEqual(repr(self.bind_expr("1 // 2")[0]), "<Literal[0]>")
        self.assertEqual(repr(self.bind_expr("1 * 2")[0]), "<Literal[2]>")
        assert_expr_binds_to(
            "1 / 2",
            lambda type_env: type_env.float.exact_type().instance,
        )
        self.assertEqual(repr(self.bind_expr("1 % 2")[0]), "<Literal[1]>")
        self.assertEqual(repr(self.bind_expr("1 & 2")[0]), "<Literal[0]>")
        self.assertEqual(repr(self.bind_expr("1 | 2")[0]), "<Literal[3]>")
        self.assertEqual(repr(self.bind_expr("1 ^ 2")[0]), "<Literal[3]>")
        self.assertEqual(repr(self.bind_expr("1 << 2")[0]), "<Literal[4]>")
        self.assertEqual(repr(self.bind_expr("100 >> 2")[0]), "<Literal[25]>")

        self.assertEqual(repr(self.bind_stmt("x = 1")[0]), "<Literal[1]>")
        # self.assertEqual(self.bind_stmt("x: foo = 1").target.comp_type, DYNAMIC)
        assert_stmt_binds_to(
            """
            def foo():
                 return 4
            x = foo()
            x += 1
            """,
            lambda type_env: type_env.DYNAMIC,
        )
        assert_expr_binds_to(
            """
            def foo():
                return 4
            a, b = foo(), foo()
            a or b
            """,
            lambda type_env: type_env.DYNAMIC,
        )
        assert_expr_binds_to(
            """
            def foo():
                return 4
            a = foo()
            +a
            """,
            lambda type_env: type_env.DYNAMIC,
        )

        assert_expr_binds_to(
            """
            def foo():
                return 4
            a = foo()
            not a
           """,
            lambda type_env: type_env.bool.instance,
        )
        assert_expr_binds_to("lambda: 42", lambda type_env: type_env.DYNAMIC)
        assert_expr_binds_to(
            """
            def foo():
                return 4
            a, b, c = foo(), foo(), foo()
            a if b else c
            """,
            lambda type_env: type_env.DYNAMIC,
        )
        assert_expr_binds_to(
            """
            def foo():
               return 4
            a, b = foo(), foo()
            a > b
            """,
            lambda type_env: type_env.DYNAMIC,
        )
        assert_expr_binds_to(
            """
            def foo():
                return 4
            x = foo()
            x()
        """,
            lambda type_env: type_env.DYNAMIC,
        )
        assert_expr_binds_to(
            """
            def foo():
                return 4
            x, y = foo()
            x(y)
        """,
            lambda type_env: type_env.DYNAMIC,
        )
        assert_expr_binds_to(
            """
            def foo():
                return 4
            x, y = foo()
            x[y]
        """,
            lambda type_env: type_env.DYNAMIC,
        )
        assert_expr_binds_to(
            """
            def foo():
                return 4
            x, y = foo()
            x[1:2]
        """,
            lambda type_env: type_env.DYNAMIC,
        )
        assert_expr_binds_to(
            """
            def foo():
                return 4
            x, y = foo()
            x[1:2:3]
        """,
            lambda type_env: type_env.DYNAMIC,
        )
        assert_expr_binds_to(
            """
            def foo():
                return 4
            x, y = foo()
            x[:]
        """,
            lambda type_env: type_env.DYNAMIC,
        )

        assert_expr_binds_to(
            "{}",
            lambda type_env: type_env.dict.exact_type().instance,
        )
        assert_expr_binds_to(
            "{2:3}",
            lambda type_env: type_env.dict.exact_type().instance,
        )
        assert_expr_binds_to(
            "{1,2}",
            lambda type_env: type_env.set.exact_type().instance,
        )
        assert_expr_binds_to(
            "[]",
            lambda type_env: type_env.list.exact_type().instance,
        )
        assert_expr_binds_to(
            "[1,2]",
            lambda type_env: type_env.list.exact_type().instance,
        )
        assert_expr_binds_to(
            "(1,2)",
            lambda type_env: type_env.tuple.exact_type().instance,
        )

        assert_expr_binds_to(
            "[x for x in [1,2,3]]",
            lambda type_env: type_env.list.exact_type().instance,
        )
        assert_expr_binds_to(
            "{x for x in [1,2,3]}",
            lambda type_env: type_env.set.exact_type().instance,
        )
        assert_expr_binds_to(
            "{x:x for x in [1,2,3]}",
            lambda type_env: type_env.dict.exact_type().instance,
        )
        assert_expr_binds_to(
            "(x for x in [1,2,3])",
            lambda type_env: type_env.DYNAMIC,
        )

        def body_get(stmt):
            return stmt.body[0].value

        self.assertEqual(
            repr(self.bind_stmt("def f(): return 42", getter=body_get)[0]),
            "<Literal[42]>",
        )
        assert_stmt_binds_to(
            "def f(): yield 42",
            lambda type_env: type_env.DYNAMIC,
            getter=body_get,
        )
        assert_stmt_binds_to(
            "def f(x): yield from x",
            lambda type_env: type_env.DYNAMIC,
            getter=body_get,
        )
        assert_stmt_binds_to(
            "async def f(x): await x",
            lambda type_env: type_env.DYNAMIC,
            getter=body_get,
        )

        assert_expr_binds_to("object", lambda type_env: type_env.object.exact_type())

        self.assertEqual(
            repr(self.bind_expr("1 + 2", optimize=True)[0]),
            "<Literal[3]>",
        )

    def test_type_attrs(self):
        attrs = self.type_env.type.__dict__.keys()
        obj_attrs = self.type_env.object.__dict__.keys()
        self.assertEqual(set(attrs), set(obj_attrs))

    def test_type_exact(self) -> None:
        self.assertIs(self.type_env.list.exact(), self.type_env.list)
        self.assertIs(
            self.type_env.list.exact_type().exact(), self.type_env.list.exact_type()
        )

        self.assertIs(self.type_env.list.exact_type(), self.type_env.list.exact_type())
        self.assertIs(
            self.type_env.list.exact_type().exact_type(),
            self.type_env.list.exact_type(),
        )

    def test_type_inexact(self) -> None:
        self.assertIs(self.type_env.list.inexact(), self.type_env.list)
        self.assertIs(
            self.type_env.list.exact_type().inexact(), self.type_env.list.exact_type()
        )

        self.assertIs(self.type_env.list.inexact_type(), self.type_env.list)
        self.assertIs(
            self.type_env.list.exact_type().inexact_type(), self.type_env.list
        )

    def test_type_is_exact(self) -> None:
        self.assertTrue(self.type_env.function.is_exact)
        self.assertTrue(self.type_env.method.is_exact)
        self.assertTrue(self.type_env.member.is_exact)
        self.assertTrue(self.type_env.builtin_method_desc.is_exact)
        self.assertTrue(self.type_env.builtin_method.is_exact)
        self.assertTrue(self.type_env.slice.is_exact)
        self.assertTrue(self.type_env.none.is_exact)
        self.assertTrue(self.type_env.str.exact_type().is_exact)
        self.assertTrue(self.type_env.bool.is_exact)
        self.assertTrue(self.type_env.int.exact_type().is_exact)
        self.assertTrue(self.type_env.float.exact_type().is_exact)
        self.assertTrue(self.type_env.complex.exact_type().is_exact)
        self.assertTrue(self.type_env.ellipsis.is_exact)
        self.assertTrue(self.type_env.dict.exact_type().is_exact)
        self.assertTrue(self.type_env.tuple.exact_type().is_exact)
        self.assertTrue(self.type_env.set.exact_type().is_exact)
        self.assertTrue(self.type_env.list.exact_type().is_exact)
        self.assertTrue(self.type_env.dynamic.is_exact)

        self.assertFalse(self.type_env.type.is_exact)
        self.assertFalse(self.type_env.object.is_exact)
        self.assertFalse(self.type_env.str.is_exact)
        self.assertFalse(self.type_env.int.is_exact)
        self.assertFalse(self.type_env.float.is_exact)
        self.assertFalse(self.type_env.complex.is_exact)
        self.assertFalse(self.type_env.bytes.is_exact)
        self.assertFalse(self.type_env.dict.is_exact)
        self.assertFalse(self.type_env.tuple.is_exact)
        self.assertFalse(self.type_env.set.is_exact)
        self.assertFalse(self.type_env.list.is_exact)
        self.assertFalse(self.type_env.base_exception.is_exact)
        self.assertFalse(self.type_env.exception.is_exact)
        self.assertFalse(self.type_env.static_method.is_exact)
        self.assertFalse(self.type_env.named_tuple.is_exact)

    def test_bind_instance(self) -> None:
        mod, comp = self.bind_module("class C: pass\na: C = C()")
        assign = mod.body[1]
        types = comp.modules["foo"].types
        self.assertEqual(types[assign.target].name, "foo.C")
        self.assertEqual(repr(types[assign.target]), "<foo.C>")

    def test_bind_func_def(self) -> None:
        mod, comp = self.bind_module(
            """
            def f(x: object = None, y: object = None):
                pass
        """
        )
        modtable = comp.modules["foo"]
        self.assertTrue(isinstance(modtable.get_child("f"), Function))

    def test_strict_module(self) -> None:
        code = """
            def f(a):
                x: bool = a
        """
        acomp = self.compile_strict(code)
        x = self.find_code(acomp, "f")
        self.assertInBytecode(x, "CAST", ("builtins", "bool", "!"))

    def test_strict_module_constant(self) -> None:
        code = """
            def f(a):
                x: bool = a
        """
        acomp = self.compile_strict(code)
        x = self.find_code(acomp, "f")
        self.assertInBytecode(x, "CAST", ("builtins", "bool", "!"))

    def test_strict_module_isinstance(self):
        code = """
            from typing import Optional

            def foo(tval: Optional[object]) -> str:
                if isinstance(tval, str):
                    return tval
                return "hi"
        """
        self.compile_strict(code)

    def test_strict_module_mutable(self):
        code = """
            from __strict__ import mutable

            @mutable
            class C:
                def __init__(self, x):
                    self.x = 1
        """
        with self.in_module(code) as mod:
            self.assertInBytecode(mod.C.__init__, "STORE_FIELD")

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
        bcomp = self.compiler(a=acode, b=bcode).compile_module("b")
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

        code = self.compile(codestr, modname="foo")
        x = self.find_code(code, "x")
        self.assertInBytecode(x, "INVOKE_METHOD", (("foo", "C", "f"), 0))

        with self.in_module(codestr) as mod:
            x, C = mod.x, mod.C
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

        code = self.compile(codestr, modname="foo")
        x = self.find_code(code, "x")
        self.assertInBytecode(x, "INVOKE_METHOD", (("foo", "C", "f"), 0))

        with self.in_module(codestr) as mod:
            a, b = mod.a, mod.b
            self.assertEqual(a, 2)
            self.assertEqual(b, 4)

    def test_invoke_base_inited(self):
        """when the base class v-table is initialized before a derived
        class we still have a properly initialized v-table for the
        derived type"""

        codestr = """
            class B:
                def f(self):
                    return 42

            X = B().f()

            class D(B):
                def g(self):
                    return 100

            def g(x: D):
                return x.g()
        """
        with self.in_module(codestr) as mod:
            self.assertEqual(mod.X, 42)
            d = mod.D()
            self.assertEqual(mod.g(d), 100)

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

        code = self.compile(codestr, modname="foo")
        x = self.find_code(code, "x")
        self.assertInBytecode(x, "INVOKE_METHOD", (("foo", "C", "f"), 0))

        with self.in_module(codestr) as mod:
            a = mod.a
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

        code = self.compile(codestr, modname="foo")
        x = self.find_code(code, "x")
        self.assertInBytecode(x, "INVOKE_METHOD", (("foo", "C", "f"), 0))

        for compiler in [PythonCodeGenerator, StaticCodeGenerator]:
            with self.in_module(codestr, code_gen=compiler) as mod:
                x, C = mod.x, mod.C
                self.assertEqual(x(C()), 2)

                class Callable:
                    def __call__(self_, obj=None):
                        self.assertEqual(obj, None)
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

        code = self.compile(codestr, modname="foo")
        x = self.find_code(code, "x")
        self.assertInBytecode(x, "INVOKE_METHOD", (("foo", "C", "f"), 0))

        for compiler in [PythonCodeGenerator, StaticCodeGenerator]:
            with self.in_module(codestr, code_gen=compiler) as mod:
                x, C = mod.x, mod.C
                self.assertEqual(x(C()), 2)

                class Callable:
                    def __call__(self_, obj=None):
                        self.assertEqual(obj, None)
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

        code = self.compile(codestr, modname="foo")
        x = self.find_code(code, "x")
        self.assertInBytecode(x, "INVOKE_METHOD", (("foo", "C", "f"), 0))

        with self.in_module(codestr) as mod:
            x, C = mod.x, mod.C
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

        code = self.compile(codestr, modname="foo")
        x = self.find_code(code, "x")
        self.assertInBytecode(x, "INVOKE_METHOD", (("foo", "C", "f"), 0))

        with self.in_module(codestr) as mod:
            x, C = mod.x, mod.C
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

        code = self.compile(codestr, modname="foo")
        x = self.find_code(code, "x")
        self.assertInBytecode(x, "INVOKE_METHOD", (("foo", "C", "f"), 0))

        with self.in_module(codestr) as mod:
            x, C = mod.x, mod.C
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

        code = self.compile(codestr, modname="foo")
        x = self.find_code(code, "x")
        self.assertInBytecode(x, "INVOKE_METHOD", (("foo", "C", "f"), 0))

        for compiler in [PythonCodeGenerator, StaticCodeGenerator]:
            with self.in_module(codestr, code_gen=compiler) as mod:
                x, C = mod.x, mod.C
                self.assertEqual(x(C()), 2)

                class Callable:
                    def __call__(self):
                        return 42

                class Descr:
                    def __get__(self, inst, ctx):
                        return Callable()

                    def __call__(self):
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

        code = self.compile(codestr, modname="foo")
        x = self.find_code(code, "x")
        self.assertInBytecode(x, "INVOKE_METHOD", (("foo", "C", "f"), 0))

        with self.in_module(codestr) as mod:
            x, C = mod.x, mod.C
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

        code = self.compile(codestr, modname="foo")
        x = self.find_code(code, "x")
        self.assertInBytecode(x, "INVOKE_METHOD", (("foo", "C", "f"), 0))

        with self.in_module(codestr) as mod:
            x, C = mod.x, mod.C
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

        code = self.compile(codestr, modname="test_annotated_function_derived")
        x = self.find_code(code, "x")
        self.assertInBytecode(
            x, "INVOKE_METHOD", (("test_annotated_function_derived", "C", "f"), 0)
        )

        with self.in_module(codestr) as mod:
            x = mod.x
            self.assertEqual(x(mod.C()), 2)
            self.assertEqual(x(mod.D()), 4)
            self.assertEqual(x(mod.E()), 2)

    def test_unknown_annotation(self):
        codestr = """
            def foo():
               return 3
            def f(a):
                x: foo = a
                return x.bar
        """
        self.compile(codestr)

        class C:
            bar = 42

        f = self.run_code(codestr)["f"]
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
        code = self.compile(codestr, modname="foo")

        b_init = self.find_code(self.find_code(code, "B"), "__init__")
        self.assertInBytecode(b_init, "STORE_FIELD", ("foo", "B", "value"))

        f = self.find_code(self.find_code(code, "D"), "f")
        self.assertInBytecode(f, "LOAD_FIELD", ("foo", "B", "value"))

        with self.in_module(codestr) as mod:
            D = mod.D
            d = D(42)
            self.assertEqual(d.f(), 42)

    def test_untyped_attr(self):
        codestr = """
        def foo(x):
            y = x.load
            x.store = 42
            del x.delete
        """
        with self.in_module(codestr) as mod:
            self.assertInBytecode(mod.foo, "LOAD_ATTR", "load")
            self.assertInBytecode(mod.foo, "STORE_ATTR", "store")
            self.assertInBytecode(mod.foo, "DELETE_ATTR", "delete")

    def test_incompat_override_method_ret_type(self):
        codestr = """
            class A:
                def m(self) -> str:
                    return "hello"

            class B(A):
                def m(self) -> int:
                    return 0
        """
        with self.assertRaisesRegex(
            TypedSyntaxError,
            "<module>.B.m overrides <module>.A.m inconsistently. "
            "Returned type `int` is not a subtype of the overridden return `str`",
        ):
            self.compile(codestr)

    def test_incompat_override_method_num_args(self):
        codestr = """
            class A:
                def m(self) -> int:
                    return 42

            class B(A):
                def m(self, x: int) -> int:
                    return 0
        """
        with self.assertRaisesRegex(
            TypedSyntaxError,
            "<module>.B.m overrides <module>.A.m inconsistently. Number of arguments differ",
        ):
            self.compile(codestr)

    def test_incompat_override_method_arg_type(self):
        codestr = """
            class A:
                def m(self, x: str) -> int:
                    return 42

            class B(A):
                def m(self, x: int) -> int:
                    return 0
        """
        with self.assertRaisesRegex(
            TypedSyntaxError,
            "<module>.B.m overrides <module>.A.m inconsistently. "
            "Parameter x of type `int` is not a supertype of the overridden parameter `str`",
        ):
            self.compile(codestr)

    def test_incompat_override_method_arg_name(self):
        codestr = """
            class A:
                def m(self, x: str) -> int:
                    return 42

            class B(A):
                def m(self, y: str) -> int:
                    return 0
        """
        with self.assertRaisesRegex(
            TypedSyntaxError,
            "<module>.B.m overrides <module>.A.m inconsistently. "
            "Positional argument 2 named `x` is overridden as `y`",
        ):
            self.compile(codestr)

    def test_incompat_override_method_starargs(self):
        codestr = """
            class A:
                def m(self) -> int:
                    return 42

            class B(A):
                def m(self, *args) -> int:
                    return 0
        """
        with self.assertRaisesRegex(
            TypedSyntaxError,
            "<module>.B.m overrides <module>.A.m inconsistently. "
            "Functions differ by including \\*args",
        ):
            self.compile(codestr)

    def test_incompat_override_method_num_kwonly_args(self):
        codestr = """
            class A:
                def m(self) -> int:
                    return 42

            class B(A):
                def m(self, *, x: int) -> int:
                    return 0
        """
        with self.assertRaisesRegex(
            TypedSyntaxError,
            "<module>.B.m overrides <module>.A.m inconsistently. Number of arguments differ",
        ):
            self.compile(codestr)

    def test_incompat_override_method_kwonly_name(self):
        codestr = """
            class A:
                def m(self, *, y: int) -> int:
                    return 42

            class B(A):
                def m(self, *, x: int) -> int:
                    return 0
        """
        with self.assertRaisesRegex(
            TypedSyntaxError,
            "<module>.B.m overrides <module>.A.m inconsistently. Keyword only argument `y` is overridden as `x`",
        ):
            self.compile(codestr)

    def test_compat_override_method_extra_kwarg(self):
        codestr = """
            class A:
                def m(self, **kwargs) -> int:
                    return 42

            class B(A):
                def m(self, a: int = 1, **kwargs) -> int:
                    return 0

            def invoke(o: A) -> int:
                return o.m()
        """
        with self.in_module(codestr) as mod:
            a = mod.A()
            b = mod.B()

            self.assertEqual(mod.invoke(a), 42)
            self.assertEqual(mod.invoke(b), 0)

    def test_compat_override_redeclared_kwarg(self):
        codestr = """
            import __static__

            class A:
                def m(
                    self, x: int | None = None, **kwargs: object
                ) -> int:
                    return 1

            class B(A):
                def m(
                    self, x: int | None = None, **kwargs: object
                ) -> int:
                    return 2

            def invoke(o: A) -> int:
                return o.m()
        """
        with self.in_module(codestr) as mod:
            a = mod.A()
            b = mod.B()

            self.assertEqual(mod.invoke(a), 1)
            self.assertEqual(mod.invoke(b), 2)

    def test_incompat_override_method_kwonly_mismatch(self):
        codestr = """
            class A:
                def m(self, x: str) -> int:
                    return 42

            class B(A):
                def m(self, *, x: str) -> int:
                    return 0
        """
        with self.assertRaisesRegex(
            TypedSyntaxError,
            "<module>.B.m overrides <module>.A.m inconsistently. `x` differs by keyword only vs positional",
        ):
            self.compile(codestr)

    def test_incompat_override_method_starkwargs(self):
        codestr = """
            class A:
                def m(self) -> int:
                    return 42

            class B(A):
                def m(self, **args) -> int:
                    return 0
        """
        with self.assertRaisesRegex(
            TypedSyntaxError,
            "<module>.B.m overrides <module>.A.m inconsistently. "
            "Functions differ by including \\*\\*kwargs",
        ):
            self.compile(codestr)

    def test_incompat_override_method_arg_type_okay(self):
        codestr = """
            class A:
                def m(self, x: str) -> int:
                    return 42

            class B(A):
                def m(self, x: object) -> int:
                    return 0
        """
        self.compile(codestr)

    def test_incompat_override_init_okay(self):
        codestr = """
            class A:
                def __init__(self) -> None:
                    pass

            class B(A):
                def __init__(self, x: int) -> None:
                    pass

            def f(x: A):
                x.__init__()
        """
        with self.in_module(codestr) as mod:
            f = mod.f
            # calling __init__ directly shouldn't use INVOKE_METHOD
            # as we allow users to override this inconsistently
            self.assertNotInBytecode(f, "INVOKE_METHOD")

    def test_incompat_override(self):
        codestr = """
        class C:
            x: int

        class D(C):
            def x(self): pass
        """
        with self.assertRaises(TypedSyntaxError):
            self.compile(codestr, modname="foo")

    def test_redefine_type(self):
        codestr = """
            class C: pass
            class D: pass

            def f(a):
                x: C = C()
                x: D = D()
        """
        with self.assertRaises(TypedSyntaxError):
            self.compile(codestr, modname="foo")

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
            self.compile(codestr, modname="foo")

    def test_optional_no_error(self):
        codestr = """
            from typing import Optional

            def f():
                x: Optional[Exception] = None
                return x.__class__
        """

        with self.in_module(codestr) as mod:
            self.assertEqual(mod.f(), type(None))

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
            self.compile(codestr)

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
            self.compile(codestr)

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
        self.compile(codestr, modname="foo")

    def test_nonoptional_load(self):
        codestr = """
            class C:
                def __init__(self, y: int):
                    self.y = y

            def f(c: C) -> int:
                return c.y
        """
        code = self.compile(codestr, modname="foo")
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
        self.compile(codestr, modname="foo")

    def test_optional_assign_subclass_opt(self):
        codestr = """
            from typing import Optional
            class B: pass
            class D(B): pass

            def f(x: Optional[D]):
                a: Optional[B] = x
        """
        self.compile(codestr, modname="foo")

    def test_optional_assign_none(self):
        codestr = """
            from typing import Optional
            class B: pass

            def f(x: Optional[B]):
                a: Optional[B] = None
        """
        self.compile(codestr, modname="foo")

    def test_error_mixed_math(self):
        with self.assertRaises(TypedSyntaxError):
            self.compile(
                """
                from __static__ import ssize_t
                def f():
                    y = 1
                    x: ssize_t = 42 + y
                """
            )

    def test_error_incompat_return(self):
        with self.assertRaises(TypedSyntaxError):
            self.compile(
                """
                class D: pass
                class C:
                    def __init__(self):
                        self.x = None

                    def f(self) -> "C":
                        return D()
                """
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
                C = mod.C
                f = mod.f
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
                    f = mod.f
                    f()
                    self.assert_jitted(f)

    def test_cast_wrong_args(self):
        codestr = """
            from __static__ import cast
            def f():
                cast(42)
        """
        with self.assertRaises(TypedSyntaxError):
            self.compile(codestr)

    def test_cast_unknown_type(self):
        codestr = """
            from __static__ import cast
            def f():
                cast(abc, 42)
        """
        with self.assertRaises(TypedSyntaxError):
            self.compile(codestr)

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
                C = mod.C
                f = mod.f
                self.assertTrue(isinstance(f(C()), C))
                self.assertEqual(f(None), None)
                self.assert_jitted(f)

    def test_code_flags(self):
        codestr = """
        def func():
            print("hi")

        func()
        """
        module = self.compile(codestr)
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
        with self.in_module(codestr) as mod:
            f = mod.func
            self.assertEqual(f(), 2)

    def test_invoke_str_method(self):
        codestr = """
        def func():
            a = 'a b c'
            return a.split()

        """
        with self.in_module(codestr) as mod:
            f = mod.func
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
        with self.in_module(codestr) as mod:
            f = mod.func
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
        with self.in_module(codestr) as mod:
            f = mod.func
            self.assertNotInBytecode(f, "INVOKE_FUNCTION")
            self.assertNotInBytecode(f, "INVOKE_METHOD")
            self.assertEqual(f(), ["", " b c"])

    def test_invoke_int_method(self):
        codestr = """
        def func():
            a = 42
            return a.bit_length()

        """
        with self.in_module(codestr) as mod:
            f = mod.func
            self.assertInBytecode(
                f, "INVOKE_FUNCTION", (("builtins", "int", "!", "bit_length"), 1)
            )
            self.assertEqual(f(), 6)

    def test_invoke_method_non_static_base(self):
        codestr = """
        class C(Exception):
            def f(self):
                return 42

            def g(self):
                return self.f()
        """

        with self.in_module(codestr) as mod:
            C = mod.C
            self.assertEqual(C().g(), 42)

    def test_invoke_builtin_func(self):
        codestr = """
        from xxclassloader import foo
        from __static__ import int64, box

        def func():
            a: int64 = foo()
            return box(a)

        """
        with self.in_module(codestr) as mod:
            f = mod.func
            self.assertInBytecode(f, "INVOKE_FUNCTION", (((mod.__name__, "foo"), 0)))
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
            with self.in_module(codestr) as mod:
                test = mod.test
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
        with self.in_module(codestr) as mod:
            f = mod.func
            self.assertInBytecode(f, "INVOKE_FUNCTION", (((mod.__name__, "bar"), 1)))
            self.assertEqual(f(), 42)
            self.assert_jitted(f)

    def test_invoke_func_unexistent_module(self):
        codestr = """
        from xxclassloader import bar
        from __static__ import int64, box

        def func():
            a: int64 = bar(42)
            return box(a)

        """
        with self.in_module(codestr) as mod:
            # remove xxclassloader from sys.modules during this test
            xxclassloader = sys.modules["xxclassloader"]
            del sys.modules["xxclassloader"]
            try:
                func = mod.func
                self.assertInBytecode(
                    func, "INVOKE_FUNCTION", (((mod.__name__, "bar"), 1))
                )
                self.assertEqual(func(), 42)
                self.assert_jitted(func)
            finally:
                sys.modules["xxclassloader"] = xxclassloader

    def test_invoke_meth_o(self):
        codestr = """
        from xxclassloader import spamobj

        def func():
            a = spamobj[int]()
            a.setstate_untyped(42)
            return a.getstate()

        """
        with self.in_module(codestr) as mod:
            f = mod.func

            self.assertInBytecode(
                f,
                "INVOKE_FUNCTION",
                (
                    (
                        "xxclassloader",
                        "spamobj",
                        (("builtins", "int"),),
                        "!",
                        "setstate_untyped",
                    ),
                    2,
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
        with self.in_module(codestr) as mod:
            f = mod.func
            self.assertEqual(f(), "42abc")

    def test_verify_positional_args(self):
        codestr = """
            def x(a: int, b: str) -> None:
                pass
            x("a", 2)
        """
        with self.assertRaisesRegex(
            TypedSyntaxError,
            r"str received for positional arg 'a', expected int",
        ):
            self.compile(codestr)

    def test_verify_too_many_args(self):
        codestr = """
            def x():
                return 42

            x(1)
        """
        with self.assertRaisesRegex(
            TypedSyntaxError,
            r"Mismatched number of args for function <module>.x. Expected 0, got 1",
        ):
            self.compile(codestr)

    def test_verify_positional_args_unordered(self):
        codestr = """
            def x(a: int, b: str) -> None:
                return y(a, b)
            def y(a: int, b: str) -> None:
                pass
        """
        self.compile(codestr)

    def test_verify_kwargs(self):
        codestr = """
            def x(a: int=1, b: str="hunter2") -> None:
                return
            x(b="lol", a=23)
        """
        self.compile(codestr)

    def test_verify_kwdefaults(self):
        codestr = """
            def x(*, b: str="hunter2"):
                return b
            z = x(b="lol")
        """
        with self.in_module(codestr) as mod:
            self.assertEqual(mod.z, "lol")

    def test_verify_kwdefaults_no_value(self):
        codestr = """
            def x(*, b: str="hunter2"):
                return b
            a = x()
        """
        module = self.compile(codestr)
        # we don't yet support optimized dispatch to kw-only functions
        self.assertInBytecode(module, "CALL_FUNCTION")
        with self.in_module(codestr) as mod:
            self.assertEqual(mod.a, "hunter2")

    def test_verify_kwdefaults_with_value(self):
        codestr = """
            def x(*, b: str="hunter2"):
                return b
            a = x(b="hunter3")
        """
        module = self.compile(codestr)
        # TODO(T87420170): Support invokes here.
        self.assertNotInBytecode(module, "INVOKE_FUNCTION")
        with self.in_module(codestr) as mod:
            self.assertEqual(mod.a, "hunter3")

    def test_verify_lambda(self):
        codestr = """
            x = lambda x: x
            a = x("hi")
        """
        with self.in_module(codestr) as mod:
            self.assertEqual(mod.a, "hi")

    def test_verify_lambda_keyword_only(self):
        codestr = """
            x = lambda *, x: x
            a = x(x="hi")
        """
        with self.in_module(codestr) as mod:
            self.assertEqual(mod.a, "hi")

    def test_verify_lambda_vararg(self):
        codestr = """
            x = lambda *x: x[1]
            a = x(1, "hi")
        """
        with self.in_module(codestr) as mod:
            self.assertEqual(mod.a, "hi")

    def test_verify_lambda_kwarg(self):
        codestr = """
            x = lambda **kwargs: kwargs["key"]
            a = x(key="hi")
        """
        with self.in_module(codestr) as mod:
            self.assertEqual(mod.a, "hi")

    def test_verify_arg_dynamic_type(self):
        codestr = """
            def x(v:str):
                return 'abc'
            def y(v):
                return x(v)
        """
        with self.in_module(codestr) as mod:
            y = mod.y
            with self.assertRaises(TypeError):
                y(42)
            self.assertEqual(y("foo"), "abc")

    def test_verify_arg_unknown_type(self):
        codestr = """
            from unknown import foo
            def x(x:foo):
                return 3
            x('abc')
        """
        module = self.compile(codestr)
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
            f = mod.f
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
            f = mod.f
            self.assertInBytecode(f, "INVOKE_METHOD", (("builtins", "dict", "get"), 1))
            self.assertEqual(f({}), None)

    def test_verify_kwarg_unknown_type(self):
        codestr = """
            from unknown import foo
            def x(x:foo):
                return 0
            x(x='abc')
        """
        module = self.compile(codestr)
        self.assertInBytecode(module, "INVOKE_FUNCTION")
        x = self.find_code(module)
        self.assertInBytecode(x, "CHECK_ARGS", ())

    def test_verify_kwdefaults_too_many(self):
        codestr = """
            def x(*, b: str="hunter2") -> None:
                return
            x('abc')
        """
        # We do not verify types for calls that we can't do direct invokes.
        self.compile(codestr)

    def test_verify_kwdefaults_too_many_class(self):
        codestr = """
            class C:
                def x(self, *, b: str="hunter2") -> None:
                    return
            C().x('abc')
        """
        # We do not verify types for calls that we can't do direct invokes.
        self.compile(codestr)

    def test_verify_kwonly_failure(self):
        codestr = """
            def x(*, a: int=1, b: str="hunter2") -> None:
                return
            x(a="hi", b="lol")
        """
        # We do not verify types for calls that we can't do direct invokes.
        self.compile(codestr)

    def test_verify_kwonly_self_loaded_once(self):
        codestr = """
            class C:
                def x(self, *, a: int=1) -> int:
                    return 43

            def f():
                return C().x(a=1)
        """
        with self.in_module(codestr) as mod:
            f = mod.f
            io = StringIO()
            dis.dis(f, file=io)
            self.assertEqual(1, io.getvalue().count("TP_ALLOC"))

    def test_call_function_unknown_ret_type(self):
        codestr = """
            from __future__ import annotations
            from builtins import open

            def g() -> open:
                return 42

            def testfunc():
                return g()
        """
        with self.in_module(codestr) as mod:
            f = mod.testfunc
            self.assertEqual(f(), 42)

    def test_verify_kwargs_failure(self):
        codestr = """
            def x(a: int=1, b: str="hunter2") -> None:
                return
            x(a="hi", b="lol")
        """
        with self.assertRaisesRegex(
            TypedSyntaxError, r"str received for keyword arg 'a', expected int"
        ):
            self.compile(codestr)

    def test_verify_mixed_args(self):
        codestr = """
            def x(a: int=1, b: str="hunter2", c: int=14) -> None:
                return
            x(12, c=56, b="lol")
        """
        self.compile(codestr)

    def test_kwarg_cast(self):
        codestr = """
            def x(a: int=1, b: str="hunter2", c: int=14) -> None:
                return

            def g(a):
                x(b=a)
        """
        code = self.find_code(self.compile(codestr), "g")
        self.assertInBytecode(code, "CAST", ("builtins", "str"))

    def test_kwarg_nocast(self):
        codestr = """
            def x(a: int=1, b: str="hunter2", c: int=14) -> None:
                return

            def g():
                x(b='abc')
        """
        code = self.find_code(self.compile(codestr), "g")
        self.assertNotInBytecode(code, "CAST", ("builtins", "str"))

    def test_verify_mixed_args_kw_failure(self):
        codestr = """
            def x(a: int=1, b: str="hunter2", c: int=14) -> None:
                return
            x(12, c="hi", b="lol")
        """
        with self.assertRaisesRegex(
            TypedSyntaxError, r"str received for keyword arg 'c', expected int"
        ):
            self.compile(codestr)

    def test_verify_mixed_args_positional_failure(self):
        codestr = """
            def x(a: int=1, b: str="hunter2", c: int=14) -> None:
                return
            x("hi", b="lol")
        """
        with self.assertRaisesRegex(
            TypedSyntaxError,
            r"str received for positional arg 'a', expected int",
        ):
            self.compile(codestr)

    # Same tests as above, but for methods.
    def test_verify_positional_args_method(self):
        codestr = """
            class C:
                def x(self, a: int, b: str) -> None:
                    pass
            C().x(2, "hi")
        """
        self.compile(codestr)

    def test_verify_positional_args_failure_method(self):
        codestr = """
            class C:
                def x(self, a: int, b: str) -> None:
                    pass
            C().x("a", 2)
        """
        with self.assertRaisesRegex(
            TypedSyntaxError,
            r"str received for positional arg 'a', expected int",
        ):
            self.compile(codestr)

    def test_verify_mixed_args_method(self):
        codestr = """
            class C:
                def x(self, a: int=1, b: str="hunter2", c: int=14) -> None:
                    return
            C().x(12, c=56, b="lol")
        """
        self.compile(codestr)

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
            x = mod.X
            self.assertEqual(x, 1)
        compiled = self.compile(codestr)
        self.assertEqual(compiled.co_nlocals, 1)

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
            test = mod.test
            test()
            x = mod.X
            self.assertEqual(x, 3)

    def test_verify_mixed_args_kw_failure_method(self):
        codestr = """
            class C:
                def x(self, a: int=1, b: str="hunter2", c: int=14) -> None:
                    return
            C().x(12, c=b'lol', b="lol")
        """
        with self.assertRaisesRegex(
            TypedSyntaxError, r"bytes received for keyword arg 'c', expected int"
        ):
            self.compile(codestr)

    def test_verify_mixed_args_positional_failure_method(self):
        codestr = """
            class C:
                def x(self, a: int=1, b: str="hunter2", c: int=14) -> None:
                    return
            C().x("hi", b="lol")
        """
        with self.assertRaisesRegex(
            TypedSyntaxError,
            r"str received for positional arg 'a', expected int",
        ):
            self.compile(codestr)

    def test_generic_kwargs_unsupported(self):
        # definition is allowed, we just don't do an optimal invoke
        codestr = """
        def f(a: int, b: str, **my_stuff) -> None:
            pass

        def g():
            return f(1, 'abc', x="y")
        """
        with self.in_module(codestr) as mod:
            g = mod.g
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
            g = mod.g
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
            g = mod.g
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
            g = mod.g
            self.assertInBytecode(g, "CALL_METHOD", 3)

    def test_kwargs_get(self):
        codestr = """
            def test(**foo):
                print(foo.get('bar'))
        """

        with self.in_module(codestr) as mod:
            test = mod.test
            self.assertInBytecode(
                test, "INVOKE_FUNCTION", (("builtins", "dict", "get"), 2)
            )

    def test_varargs_count(self):
        codestr = """
            def test(*foo):
                print(foo.count('bar'))
        """

        with self.in_module(codestr) as mod:
            test = mod.test
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

        with self.in_module(codestr) as mod:
            test = mod.testfunc
            self.assertEqual(test(), (2,))

    def test_kwargs_call(self):
        codestr = """
            def g(**foo):
                return foo

            def testfunc():
                return g(x=2)
        """

        with self.in_module(codestr) as mod:
            test = mod.testfunc
            self.assertEqual(test(), {"x": 2})

    def test_pydict_arg_annotation(self):
        codestr = """
            from __static__ import PyDict
            def f(d: PyDict[str, int]) -> str:
                # static python ignores the untrusted generic types
                return d[3]
        """
        with self.in_module(codestr) as mod:
            self.assertEqual(mod.f({3: "foo"}), "foo")

    def test_unknown_type_unary(self):
        codestr = """
            def x(y):
                z = -y
        """
        f = self.find_code(self.compile(codestr, modname="foo"))
        self.assertInBytecode(f, "UNARY_NEGATIVE")

    def test_unknown_type_binary(self):
        codestr = """
            def x(a, b):
                z = a + b
        """
        f = self.find_code(self.compile(codestr, modname="foo"))
        self.assertInBytecode(f, "BINARY_ADD")

    def test_unknown_type_compare(self):
        codestr = """
            def x(a, b):
                z = a > b
        """
        f = self.find_code(self.compile(codestr, modname="foo"))
        self.assertInBytecode(f, "COMPARE_OP")

    def test_async_func_ret_type(self):
        codestr = """
            async def x(a) -> int:
                return a
        """
        f = self.find_code(self.compile(codestr, modname="foo"))
        self.assertInBytecode(f, "CAST")

    def test_async_func_arg_types(self):
        codestr = """
            async def f(x: int):
                pass
        """
        f = self.find_code(self.compile(codestr))
        self.assertInBytecode(f, "CHECK_ARGS", (0, ("builtins", "int")))

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
            C = mod.C

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
        with self.in_module(codestr) as mod:
            D = mod.D
            counter = [0]
            d = D(counter)

            C = mod.C
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
        with self.in_module(codestr) as mod:
            C = mod.C
            a = C("abc")
            del a.x
            with self.assertRaises(AttributeError):
                a.x

    def test_attr_generic_optional(self):
        codestr = """
            from typing import Optional
            def f(x: Optional):
                return x.foo
        """

        with self.assertRaisesRegex(
            TypedSyntaxError, "cannot access attribute from unbound Union"
        ):
            self.compile(codestr, modname="foo")

    def test_assign_generic_optional(self):
        codestr = """
            from typing import Optional
            def f():
                x: Optional = 42
        """

        with self.assertRaisesRegex(
            TypedSyntaxError, type_mismatch("Literal[42]", "Optional[T]")
        ):
            self.compile(codestr, modname="foo")

    def test_assign_generic_optional_2(self):
        codestr = """
            from typing import Optional
            def f():
                x: Optional = 42 + 1
        """

        with self.assertRaises(TypedSyntaxError):
            self.compile(codestr, modname="foo")

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
            self.compile(codestr, modname="foo")

    def test_list_of_dynamic(self):
        codestr = """
            from threading import Thread
            from typing import List

            def f(threads: List[Thread]) -> int:
                return len(threads)
        """
        f = self.find_code(self.compile(codestr), "f")
        self.assertInBytecode(f, "FAST_LEN")

    def test_typed_swap(self):
        codestr = """
            def test(a):
                x: int
                y: str
                x, y = 1, a
        """

        f = self.find_code(self.compile(codestr, modname="foo"))
        self.assertInBytecode(f, "CAST", ("builtins", "str"))
        self.assertNotInBytecode(f, "CAST", ("builtins", "int"))

    def test_typed_swap_2(self):
        codestr = """
            def test(a):
                x: int
                y: str
                x, y = a, 'abc'

        """

        f = self.find_code(self.compile(codestr, modname="foo"))
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

        f = self.find_code(self.compile(codestr, modname="foo"), "test")
        self.assertInBytecode(f, "CAST", ("builtins", "int"))
        self.assertNotInBytecode(f, "CAST", ("builtins", "str"))

    def test_typed_swap_list(self):
        codestr = """
            def test(a):
                x: int
                y: str
                [x, y] = a, 'abc'
        """

        f = self.find_code(self.compile(codestr, modname="foo"))
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

        f = self.find_code(self.compile(codestr, modname="foo"))
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

        f = self.find_code(self.compile(codestr, modname="foo"))
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

        f = self.find_code(self.compile(codestr, modname="foo"))
        self.assertInBytecode(f, "CAST", ("builtins", "str"))
        # Currently because the tuple gets turned into a constant this is less than
        # ideal:
        self.assertInBytecode(f, "CAST", ("builtins", "int", "!"))

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

        self.compile(codestr, modname="foo")

    def test_return_outside_func(self):
        codestr = """
            return 42
        """
        with self.assertRaisesRegex(SyntaxError, "'return' outside function"):
            self.compile(codestr, modname="foo")

    def test_double_decl(self):
        codestr = """
            def f():
                x: int
                x: str
        """
        with self.assertRaisesRegex(
            TypedSyntaxError, "Cannot redefine local variable x"
        ):
            self.compile(codestr, modname="foo")

        codestr = """
            def f():
                x = 42
                x: str
        """
        with self.assertRaisesRegex(
            TypedSyntaxError, "Cannot redefine local variable x"
        ):
            self.compile(codestr, modname="foo")

        codestr = """
            def f():
                x = 42
                x: int
        """
        with self.assertRaisesRegex(
            TypedSyntaxError, "Cannot redefine local variable x"
        ):
            self.compile(codestr, modname="foo")

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
        """

        self.compile(codestr)

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

        self.compile(codestr, modname="foo")

    def test_if_else_optional_return_two_branches(self):
        codestr = """
            from typing import Optional
            class C:
                def __init__(self):
                    self.field = self

            def f(x: Optional[C], a):
                if x is None:
                    if a:
                        return 0
                    else:
                        return 2
                return x.field
        """

        self.compile(codestr, modname="foo")

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

        self.compile(codestr, modname="foo")

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

        self.compile(codestr, modname="foo")

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

        self.compile(codestr, modname="foo")

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

        code = self.compile(codestr, modname="foo")
        f = self.find_code(code, "testfunc")
        self.assertInBytecode(f, "INVOKE_FUNCTION", (("foo", "D", "f"), 1))
        with self.in_module(codestr) as mod:
            test = mod.testfunc
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

        with self.in_module(codestr) as mod:
            g = mod.g
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
        with self.assertRaisesRegex(TypedSyntaxError, "dynamic"):
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

        code = self.compile(codestr, modname="foo")
        f = self.find_code(code, "testfunc")
        self.assertInBytecode(f, "INVOKE_METHOD", (("foo", "B", "f"), 0))
        with self.in_module(codestr) as mod:
            test = mod.testfunc
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

        code = self.compile(codestr, modname="foo")
        f = self.find_code(code, "testfunc")
        self.assertInBytecode(f, "INVOKE_FUNCTION", (("foo", "D", "f"), 1))
        with self.in_module(codestr) as mod:
            test = mod.testfunc
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

        code = self.compile(codestr, modname="foo")
        f = self.find_code(code, "testfunc")
        self.assertInBytecode(f, "INVOKE_FUNCTION", (("foo", "B", "f"), 1))
        with self.in_module(codestr) as mod:
            test = mod.testfunc
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

        self.type_error(codestr, "Name `x` is not defined.")

    def test_assign_test_var(self):
        codestr = """
            from typing import Optional

            def f(x: Optional[int]) -> int:
                if x is None:
                    x = 1
                return x
        """

        self.compile(codestr, modname="foo")

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

        code = self.compile(codestr, modname="foo")
        f = self.find_code(code, "testfunc")
        self.assertInBytecode(f, "INVOKE_METHOD", (("foo", "B", "f"), 0))
        with self.in_module(codestr) as mod:
            test = mod.testfunc
            self.assertEqual(test(False), 42)

    def test_assign_while_test_var(self):
        codestr = """
            from typing import Optional

            def f(x: Optional[int]) -> int:
                while x is None:
                    x = 1
                return x
        """
        self.compile(codestr, modname="foo")

    def test_assign_while_returns(self):
        codestr = """
            from typing import Optional

            def f(x: Optional[int]) -> int:
                while x is None:
                    return 1
                return x
        """
        self.compile(codestr, modname="foo")

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
        self.compile(codestr, modname="foo")

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
        self.compile(codestr, modname="foo")

    def test_continue_condition(self):
        codestr = """
            from typing import Optional

            def f(x: Optional[str]) -> str:
                while True:
                    if x is None:
                        continue
                    return x
        """
        self.compile(codestr, modname="foo")

    def test_break_condition(self):
        codestr = """
            from typing import Optional

            def f(x: Optional[str]) -> str:
                while True:
                    if x is None:
                        break
                    return x
                return ""
        """
        self.compile(codestr)

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

        code = self.compile(codestr, modname="foo")
        f = self.find_code(code, "testfunc")
        self.assertInBytecode(f, "INVOKE_FUNCTION", (("foo", "D", "f"), 1))
        with self.in_module(codestr) as mod:
            test = mod.testfunc
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

        code = self.compile(codestr, modname="foo")
        f = self.find_code(code, "testfunc")
        self.assertInBytecode(f, "INVOKE_METHOD", (("foo", "B", "f"), 0))
        with self.in_module(codestr) as mod:
            test = mod.testfunc
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

        code = self.compile(codestr, modname="foo")
        f = self.find_code(code, "testfunc")
        self.assertInBytecode(f, "INVOKE_METHOD", (("foo", "B", "f"), 0))
        with self.in_module(codestr) as mod:
            test = mod.testfunc
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

        code = self.compile(codestr, modname="foo")
        f = self.find_code(code, "testfunc")
        self.assertInBytecode(f, "INVOKE_METHOD", (("foo", "B", "f"), 0))
        with self.in_module(codestr) as mod:
            test = mod.testfunc
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

        code = self.compile(codestr, modname="foo")
        f = self.find_code(code, "testfunc")
        self.assertInBytecode(f, "INVOKE_METHOD", (("foo", "B", "f"), 0))
        with self.in_module(codestr) as mod:
            test = mod.testfunc
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
            bad_ret_type("Optional[int]", "None"),
        ):
            self.compile(codestr, modname="foo")

    def test_none_compare(self):
        codestr = """
            def f(x: int | None):
                if x > 1:
                    x = 1
                return x
        """
        with self.assertRaisesRegex(
            TypedSyntaxError,
            r"'>' not supported between 'Optional\[int\]' and 'Literal\[1\]'",
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
            r"'>' not supported between 'Literal\[1\]' and 'Optional\[int\]'",
        ):
            self.compile(codestr)

    def test_global_int(self):
        codestr = """
            X: int =  60 * 60 * 24
        """

        with self.in_module(codestr) as mod:
            X = mod.X
            self.assertEqual(X, 60 * 60 * 24)

    def test_with_traceback(self):
        codestr = """
            def f():
                x = Exception()
                return x.with_traceback(None)
        """

        with self.in_module(codestr) as mod:
            f = mod.f
            self.assertEqual(type(f()), Exception)
            self.assertInBytecode(
                f,
                "INVOKE_FUNCTION",
                (("builtins", "BaseException", "with_traceback"), 2),
            )

    def test_assign_num_to_object(self):
        codestr = """
            def f():
                x: object = 42
        """

        with self.in_module(codestr) as mod:
            f = mod.f
            self.assertNotInBytecode(f, "CAST", ("builtins", "object"))

    def test_assign_num_to_dynamic(self):
        codestr = """
            def foo():
                return 3

            def f():
                x: foo = 42
        """

        with self.in_module(codestr) as mod:
            f = mod.f
            self.assertNotInBytecode(f, "CAST", ("builtins", "object"))

    def test_assign_dynamic_to_object(self):
        codestr = """
            def f(C):
                x: object = C()
        """

        with self.in_module(codestr) as mod:
            f = mod.f
            self.assertNotInBytecode(f, "CAST", ("builtins", "object"))

    def test_assign_dynamic_to_dynamic(self):
        codestr = """
            def unknown():
                return 3

            def f(C):
                x: unknown = C()
        """

        with self.in_module(codestr) as mod:
            f = mod.f
            self.assertNotInBytecode(f, "CAST", ("builtins", "object"))

    def test_assign_constant_to_object(self):
        codestr = """
            def f():
                x: object = 42 + 1
        """

        with self.in_module(codestr) as mod:
            f = mod.f
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
        self.compile(codestr, modname="foo")

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
        self.compile(codestr, modname="foo")

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
        self.compile(codestr, modname="foo")

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
        self.compile(codestr, modname="foo")

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

        self.compile(codestr, modname="foo")

    def test_assign_try_except_redeclare_unknown_type(self):
        codestr = """
            def testfunc(unknown_exception):
                e: int
                try:
                    pass
                except unknown_exception as e:
                    pass
                return 42
        """

        self.compile(codestr, modname="foo")

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

        code = self.compile(codestr, modname="foo")
        f = self.find_code(code, "testfunc")
        self.assertInBytecode(f, "INVOKE_METHOD", (("foo", "B", "f"), 0))
        with self.in_module(codestr) as mod:
            test = mod.testfunc
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

        code = self.compile(codestr, modname="foo")
        f = self.find_code(code, "testfunc")
        self.assertInBytecode(f, "INVOKE_METHOD", (("foo", "B", "f"), 0))
        with self.in_module(codestr) as mod:
            test = mod.testfunc
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

        code = self.compile(codestr, modname="foo")
        f = self.find_code(code, "testfunc")
        self.assertInBytecode(f, "INVOKE_METHOD", (("foo", "B", "f"), 0))
        with self.in_module(codestr) as mod:
            test = mod.testfunc
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

        code = self.compile(codestr, modname="foo")
        f = self.find_code(code, "testfunc")
        self.assertInBytecode(f, "INVOKE_FUNCTION", (("foo", "B", "f"), 1))
        with self.in_module(codestr) as mod:
            test = mod.testfunc
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

        code = self.compile(codestr, modname="foo")
        f = self.find_code(code, "testfunc")
        self.assertInBytecode(f, "INVOKE_METHOD", (("foo", "B", "f"), 0))
        with self.in_module(codestr) as mod:
            test = mod.testfunc
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

        code = self.compile(codestr, modname="foo")
        f = self.find_code(code, "testfunc")
        self.assertInBytecode(f, "INVOKE_FUNCTION", (("foo", "B", "f"), 1))
        with self.in_module(codestr) as mod:
            test = mod.testfunc
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

        code = self.compile(codestr, modname="foo")
        f = self.find_code(code, "testfunc")
        self.assertInBytecode(f, "INVOKE_METHOD", (("foo", "B", "f"), 0))
        with self.in_module(codestr) as mod:
            test = mod.testfunc
            self.assertEqual(test(), 42)

    def test_if_optional_reassign(self):
        codestr = """
        from typing import Optional
        class C: pass

        def testfunc(abc: Optional[C]):
            if abc is not None:
                abc = None
        """
        self.compile(codestr, modname="foo")

    def test_unknown_imported_annotation(self):
        codestr = """
            from unknown_mod import foo

            def testfunc():
                x: foo = 42
                return x
        """
        self.compile(codestr, modname="foo")

    def test_if_optional_cond(self):
        codestr = """
            from typing import Optional
            class C:
                def __init__(self):
                    self.field = 42

            def f(x: Optional[C]):
                return x.field if x is not None else None
        """

        self.compile(codestr, modname="foo")

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

        self.compile(codestr, modname="foo")

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

        self.compile(codestr, modname="foo")

    def test_none_attribute_error(self):
        codestr = """
            def f():
                x = None
                return x.foo
        """

        with self.assertRaisesRegex(
            TypedSyntaxError, "'NoneType' object has no attribute 'foo'"
        ):
            self.compile(codestr, modname="foo")

    def test_none_call(self):
        codestr = """
            def f():
                x = None
                return x()
        """

        with self.assertRaisesRegex(
            TypedSyntaxError, "'NoneType' object is not callable"
        ):
            self.compile(codestr, modname="foo")

    def test_none_subscript(self):
        codestr = """
            def f():
                x = None
                return x[0]
        """

        with self.assertRaisesRegex(
            TypedSyntaxError, "'NoneType' object is not subscriptable"
        ):
            self.compile(codestr, modname="foo")

    def test_none_unaryop(self):
        codestr = """
            def f():
                x = None
                return -x
        """

        with self.assertRaisesRegex(
            TypedSyntaxError, "bad operand type for unary -: 'NoneType'"
        ):
            self.compile(codestr, modname="foo")

    def test_assign_type_propagation(self):
        codestr = """
            def test() -> int:
                x = 5
                return x
        """
        self.compile(codestr, modname="foo")

    def test_assign_subtype_handling(self):
        codestr = """
            class B: pass
            class D(B): pass

            def f():
                b: B = B()
                b = D()
                b = B()
        """
        self.compile(codestr, modname="foo")

    def test_assign_subtype_handling_fail(self):
        codestr = """
            class B: pass
            class D(B): pass

            def f():
                d: D = D()
                d = B()
        """
        with self.assertRaisesRegex(TypedSyntaxError, type_mismatch("foo.B", "foo.D")):
            self.compile(codestr, modname="foo")

    def test_assign_chained(self):
        codestr = """
            def test() -> str:
                x: str = "hi"
                y = x = "hello"
                return y
        """
        self.compile(codestr, modname="foo")

    def test_assign_chained_failure_wrong_target_type(self):
        codestr = """
            def test() -> str:
                x: int = 1
                y = x = "hello"
                return y
        """
        with self.assertRaisesRegex(TypedSyntaxError, type_mismatch("str", "int")):
            self.compile(codestr, modname="foo")

    @skipIf(cinderjit is not None, "can't report error from JIT")
    def test_load_uninit_module(self):
        """verify we don't crash if we receive a module w/o a dictionary"""
        codestr = """
        from typing import Optional
        class C:
            def __init__(self):
                self.x: Optional[C] = None

        """
        with self.in_module(codestr) as mod:
            C = mod.C

            class UninitModule(ModuleType):
                def __init__(self):
                    # don't call super init
                    pass

            sys.modules[mod.__name__] = UninitModule()
            with self.assertRaisesRegex(
                TypeError,
                r"bad name provided for class loader: \('"
                + mod.__name__
                + r"', 'C'\), not a class",
            ):
                C()

    def test_module_subclass(self):
        codestr = """
        from typing import Optional
        class C:
            def __init__(self):
                self.x: Optional[C] = None

        """
        with self.in_module(codestr) as mod:
            C = mod.C

            class CustomModule(ModuleType):
                def __getattr__(self, name):
                    if name == "C":
                        return C

            sys.modules[mod.__name__] = CustomModule(mod.__name__)
            c = C()
            self.assertEqual(c.x, None)

    def test_invoke_and_raise_shadow_frame_strictmod(self):
        codestr = """
        from __static__.compiler_flags import shadow_frame

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
                ((mod.__name__, "x"), 0),
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
            B = mod.B
            f = mod.f

            class D(B):
                def f(self):
                    return self

            f(D())

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
            B = mod.B
            D = mod.D
            f = mod.f

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
            B = mod.B
            f = mod.f

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
                f = mod.f
                self.assertInBytecode(f, "INVOKE_METHOD")
                a = mod.A()
                self.assertEqual(f(a), 1)
                # x is a data descriptor, it takes precedence
                a.__dict__["x"] = 100
                self.assertEqual(f(a), 1)
                # but methods are normal descriptors, instance
                # attributes should take precedence
                a.__dict__["f"] = lambda: 42
                self.assertEqual(f(a), 42)

    def test_method_prologue(self):
        codestr = """
        def f(x: str):
            return 42
        """
        with self.in_module(codestr) as mod:
            f = mod.f
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
            f = mod.f
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
            f = mod.f
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
            f = mod.f
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
            f = mod.f
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
            f = mod.f
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
            f = mod.f
            self.assertInBytecode(f, "CHECK_ARGS", ())
            self.assertEqual(f("abc"), 42)

    def test_method_prologue_kwonly(self):
        codestr = """
        def f(*, x: str):
            return 42
        """
        with self.in_module(codestr) as mod:
            f = mod.f
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
            f = mod.f
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
            f = mod.f
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
            f = mod.f
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
            f = mod.f
            self.assertInBytecode(f, "CHECK_ARGS", ())
            f(x=42)

    def test_package_no_parent(self):
        codestr = """
            class C:
                def f(self):
                    return 42
        """
        with self.in_module(codestr, name="package_no_parent.child") as mod:
            C = mod.C
            self.assertInBytecode(
                C.f, "CHECK_ARGS", (0, ("package_no_parent.child", "C"))
            )
            self.assertEqual(C().f(), 42)

    def test_direct_super_init(self):
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
            r"None received for positional arg 'self', expected <module>.C",
        ):
            self.compile(codestr)

    def test_class_unknown_attr(self):
        codestr = f"""
            class C:
                pass

            def f():
                return C.foo
        """
        with self.in_module(codestr) as mod:
            f = mod.f
            self.assertInBytecode(f, "LOAD_ATTR", "foo")

    def test_class_unknown_decorator(self):
        codestr = """
            def dec(f):
                return f
            @dec
            class C:
                @dec
                def foo(self) -> int:
                    return 3

                def f(self):
                    return self.foo()
        """
        with self.in_module(codestr, name="mymod") as mod:
            C = mod.C
            self.assertEqual(C().f(), 3)

    def test_descriptor_access(self):
        codestr = f"""
            class Obj:
                abc: int

            class C:
                x: Obj

            def f():
                return C.x.abc
        """
        with self.in_module(codestr) as mod:
            f = mod.f
            self.assertInBytecode(f, "LOAD_ATTR", "abc")
            self.assertNotInBytecode(f, "LOAD_FIELD")

    @skipIf(not path.exists(RICHARDS_PATH), "richards not found")
    def test_richards(self):
        with open(RICHARDS_PATH) as f:
            codestr = f.read()
        with self.in_module(codestr) as mod:
            Richards = mod.Richards
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
        with self.in_module(codestr) as mod:
            C = mod.C
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
        with self.in_module(codestr) as mod:
            C = mod.C
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
        with self.in_module(codestr) as mod:
            testfunc = mod.testfunc
            self.assertInBytecode(testfunc, "LOAD_FIELD", (mod.__name__, "C", "x"))

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
        with self.in_module(codestr) as mod:
            C = mod.C
            self.assertInBytecode(
                C.f,
                "LOAD_FIELD",
                (mod.__name__, "C", "x"),
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
        with self.in_module(codestr) as mod:
            C = mod.C
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
        with self.in_module(codestr) as mod:
            testfunc = mod.testfunc
            self.assertNotInBytecode(testfunc, "LOAD_FIELD", (mod.__name__, "C", "x"))

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
            bad_ret_type("Optional[int]", "int"),
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
            bad_ret_type("Optional[int]", "int"),
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
                return 0
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
        with self.in_module(codestr) as mod:
            C = mod.C
            x = C("abc")
            self.assertInBytecode(C.__eq__, "CHECK_ARGS", (0, (mod.__name__, "C")))
            self.assertNotEqual(x, x)

    def test_ret_type_cast(self):
        codestr = """
            from typing import Any

            def testfunc(x: str, y: str) -> bool:
                return x == y
        """
        with self.in_module(codestr) as mod:
            f = mod.testfunc
            self.assertEqual(f("abc", "abc"), True)
            self.assertInBytecode(f, "CAST", ("builtins", "bool", "!"))

    def test_bool_int(self):
        codestr = """
            def f():
                x: int = True
                return x
        """
        self.compile(codestr)

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
        with self.in_module(codestr) as mod:
            C = mod.C
            c = C()
            self.assertEqual(c.x(), False)
            self.assertEqual(c.y(), True)

    def test_bind_none_compare_op(self):
        codestr = """
            from typing import Any

            def has_none(x) -> bool:
                return None in x

            def has_no_none(x) -> bool:
                return None not in x
        """
        with self.in_module(codestr) as mod:
            has_none = mod.has_none
            self.assertFalse(has_none([]))
            self.assertTrue(has_none([None]))
            self.assertFalse(has_none([1, 2, 3]))
            self.assertNotInBytecode(has_none, "CAST")

            has_no_none = mod.has_no_none
            self.assertTrue(has_no_none([]))
            self.assertFalse(has_no_none([None]))
            self.assertTrue(has_no_none([1, 2, 3]))
            self.assertNotInBytecode(has_no_none, "CAST")

    def test_visit_if_else(self):
        codestr = """
            x = 0
            if x:
                pass
            else:
                def f(): return 42
        """
        with self.in_module(codestr) as mod:
            self.assertEqual(mod.f(), 42)

    def test_decorated_function_ignored_class(self):
        codestr = """
            class C:
                @property
                def x(self):
                    return lambda: 42

                def y(self):
                    return self.x()

        """
        with self.in_module(codestr) as mod:
            C = mod.C
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
        with self.in_module(codestr) as mod:
            C = mod.C
            g = mod.g
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
        with self.in_module(codestr) as mod:
            f = mod.f
            self.assertInBytecode(f, "INVOKE_FUNCTION", ((mod.__name__, "C", "f"), 0))
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
        with self.in_module(codestr) as mod:
            f = mod.f
            self.assertInBytecode(
                f,
                "INVOKE_FUNCTION",
                ((mod.__name__, "C", "f"), 0),
            )
            self.assertEqual(f(), 42)

    def test_static_function_override(self):
        codestr = """
            class A:
                @staticmethod
                def m() -> int:
                    return 42

            class B(A):
                @staticmethod
                def m() -> int:
                    return 0

            def make_a() -> A:
                return B()

            def f() -> int:
                return make_a().m()
        """
        with self.in_module(codestr) as mod:
            f = mod.f
            self.assertInBytecode(f, "INVOKE_METHOD")
            self.assertEqual(f(), 0)

    def test_static_function_final_class(self):
        codestr = """
            from typing import final

            @final
            class A:
                @staticmethod
                def m() -> int:
                    return 42

            def make_a() -> A:
                return A()

            def f() -> int:
                return make_a().m()
        """
        with self.in_module(codestr) as mod:
            f = mod.f
            self.assertNotInBytecode(f, "INVOKE_METHOD")
            self.assertEqual(f(), 42)

    def test_static_function_incompat_override(self):
        codestr = """
            class A:
                @staticmethod
                def m() -> int:
                    return 42

            class B(A):
                @staticmethod
                def m() -> str:
                    return 0
        """
        with self.assertRaisesRegex(
            TypedSyntaxError,
            "<module>.B.m overrides <module>.A.m inconsistently. "
            "Returned type `str` is not a subtype of the overridden return `int`",
        ):
            self.compile(codestr)

    def test_static_function_incompat_override_arg(self):
        codestr = """
            class A:
                @staticmethod
                def m(a: int) -> int:
                    return 42

            class B(A):
                @staticmethod
                def m(a: str) -> int:
                    return 0
        """
        with self.assertRaisesRegex(
            TypedSyntaxError,
            "<module>.B.m overrides <module>.A.m inconsistently. "
            "Parameter a of type `str` is not a supertype of the overridden parameter `int`",
        ):
            self.compile(codestr)

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
            self.compile(codestr, modname="foo")

    def test_spamobj_error(self):
        codestr = """
            from xxclassloader import spamobj

            def f():
                x = spamobj[int]()
                return x.error(1)
        """
        with self.in_module(codestr) as mod:
            f = mod.f
            with self.assertRaisesRegex(TypeError, "no way!"):
                f()

    def test_spamobj_no_error(self):
        codestr = """
            from xxclassloader import spamobj

            def testfunc():
                x = spamobj[int]()
                return x.error(0)
        """
        with self.in_module(codestr) as mod:
            f = mod.testfunc
            self.assertEqual(f(), None)

    def test_generic_type_box_box(self):
        codestr = """
            from xxclassloader import spamobj

            def testfunc():
                x = spamobj[str]()
                return (x.getint(), )
        """

        with self.assertRaisesRegex(
            TypedSyntaxError, type_mismatch("int64", "dynamic")
        ):
            self.compile(codestr)

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

        code = self.compile(codestr, modname="foo")
        f = self.find_code(code, "testfunc")
        self.assertInBytecode(
            f,
            "INVOKE_FUNCTION",
            (
                (
                    (
                        "xxclassloader",
                        "spamobj",
                        (("builtins", "str"),),
                        "!",
                        "setstate",
                    ),
                    2,
                )
            ),
        )
        with self.in_module(codestr) as mod:
            test = mod.testfunc
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

        code = self.compile(codestr, modname="foo")
        f = self.find_code(code, "testfunc")
        self.assertInBytecode(
            f,
            "INVOKE_FUNCTION",
            (
                (
                    (
                        "xxclassloader",
                        "spamobj",
                        (("builtins", "str"),),
                        "!",
                        "setstate",
                    ),
                    2,
                )
            ),
        )
        with self.in_module(codestr) as mod:
            test = mod.testfunc
            self.assertEqual(test(), None)

    def test_check_override_typed_builtin_method(self):
        codestr = """
            from xxclassloader import spamobj

            class Child(spamobj):
                def setstr(self, _s: int, /) -> None:
                    pass
        """
        self.type_error(
            codestr,
            r"<module>.Child.setstr overrides xxclassloader.spamobj\[T\].setstr inconsistently. "
            r"Parameter  of type `int` is not a supertype of the overridden parameter `str`",
        )

    def test_assign_module_global(self):
        codestr = """
            x: int = 1

            def f():
                global x
                x = "foo"
        """
        with self.assertRaisesRegex(TypedSyntaxError, type_mismatch("str", "int")):
            self.compile(codestr)

    def test_inferred_module_global_assign_subclass(self):
        codestr = """
            class MyList(list):
                pass

            x = []

            def f(new_x: list) -> list:
                global x
                x = new_x
                return x
        """
        with self.in_module(codestr) as mod:
            f, MyList = mod.f, mod.MyList
            x = []
            self.assertIs(f(x), x)
            y = MyList()
            self.assertIs(f(y), y)

    def test_named_tuple(self):
        codestr = """
            from typing import NamedTuple

            class C(NamedTuple):
                x: int
                y: str

            def myfunc(x: C):
                return x.x
        """
        with self.in_module(codestr) as mod:
            f = mod.myfunc
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
            r"Literal\[42\] received for positional arg 1, expected str",
        ):
            self.compile(codestr)

    def test_generic_optional_type_param(self):
        codestr = """
            from xxclassloader import spamobj

            def testfunc():
                x = spamobj[str]()
                x.setstateoptional(None)
        """

        self.compile(codestr)

    def test_generic_optional_type_param_2(self):
        codestr = """
            from xxclassloader import spamobj

            def testfunc():
                x = spamobj[str]()
                x.setstateoptional('abc')
        """

        self.compile(codestr)

    def test_generic_optional_type_param_error(self):
        codestr = """
            from xxclassloader import spamobj

            def testfunc():
                x = spamobj[str]()
                x.setstateoptional(42)
        """

        with self.assertRaisesRegex(
            TypedSyntaxError,
            r"Literal\[42\] received for positional arg 1, expected Optional\[str\]",
        ):
            self.compile(codestr)

    def test_async_method_override(self):
        codestr = """
            class C:
                async def f(self) -> int:
                    return 1

            def f(x: C):
                return x.f()
        """
        with self.in_strict_module(codestr) as mod:

            class D(mod.C):
                async def f(self):
                    return "not an int"

            self.assertInBytecode(
                mod.f,
                "INVOKE_METHOD",
                ((mod.__name__, "C", "f"), 0),
            )

            d = D()
            with self.assertRaises(TypeError):
                asyncio.run(mod.f(d))

    def test_async_method_override_narrowing(self):
        codestr = """
            class Num(int):
                pass

            class C:
                async def f(self) -> int:
                    return 0

            class D(C):
                async def f(self) -> Num:
                    return Num(0)
        """
        with self.in_strict_module(codestr) as mod:
            d = mod.D()
            try:
                d.f().send(None)
            except StopIteration as e:
                res = e.args[0]
                self.assertIsInstance(res, mod.Num)
                self.assertEqual(res, 0)

    def test_async_method_override_widening(self):
        codestr = """
            from typing import Optional

            class C:
                async def f(self) -> int:
                    return 0

            class D(C):
                async def f(self) -> Optional[int]:
                    return 0
        """
        with self.assertRaisesRegex(
            TypedSyntaxError,
            r"Returned type `static.InferredAwaitable\[Optional\[int\]\]` is not "
            r"a subtype of the overridden return `static.InferredAwaitable\[int\]`",
        ):
            self.compile(codestr, modname="foo")

    def test_async_method_override_future_correct_type(self):
        codestr = """
            class C:
                async def f(self) -> int:
                    return 42

                def g(self):
                    return self.f()
        """
        with self.in_strict_module(codestr) as mod:
            loop = asyncio.new_event_loop()

            class D(mod.C):
                def f(self):
                    fut = loop.create_future()
                    fut.set_result(100)
                    return fut

            d = D()
            for i in range(100):
                try:
                    d.g().send(None)
                except StopIteration as e:
                    self.assertEqual(e.args[0], 100)
            loop.close()

    def test_async_method_override_future_incorrect_type(self):
        codestr = """
            class C:
                async def f(self) -> int:
                    return 42

                def g(self):
                    return self.f()
        """
        with self.in_module(codestr) as mod:
            loop = asyncio.new_event_loop()

            class D(mod.C):
                def f(self):
                    fut = loop.create_future()
                    fut.set_result("not an int")
                    return fut

            d = D()
            with self.assertRaises(TypeError):
                d.g().send(None)
            loop.close()

    def test_async_method_immediate_await(self):
        codestr = """
            class C:
                async def f(self) -> bool:
                    return True

            async def f(x: C):
                if await x.f():
                    return 0
                return 1
        """
        with self.in_strict_module(codestr) as mod:

            class D(mod.C):
                async def f(self):
                    return False

            d = D()
            self.assertEqual(asyncio.run(mod.f(d)), 1)

    def test_async_method_immediate_await_incorrect_type(self):
        codestr = """
            class C:
                async def f(self) -> bool:
                    return True

            async def f(x: C):
                if await x.f():
                    return 0
                return 1
        """
        with self.in_strict_module(codestr) as mod:

            class D(mod.C):
                async def f(self):
                    return "not an int"

            d = D()
            with self.assertRaises(TypeError):
                asyncio.run(mod.f(d))

    def test_async_method_incorrect_type(self):
        codestr = """
            class C:
                async def f(self) -> int:
                    return 1

            async def f(x: C):
                a = x.f()
                b = 2
                c = await a
                return b + c
        """
        with self.in_strict_module(codestr) as mod:

            class D(mod.C):
                async def f(self):
                    return "not an int"

            d = D()
            with self.assertRaises(TypeError):
                asyncio.run(mod.f(d))

    def test_async_method_incorrect_type_suspended(self):
        codestr = """
            import asyncio

            class C:
                async def f(self) -> int:
                    return 1

            async def f(x: C):
                return await x.f()
        """
        with self.in_strict_module(codestr) as mod:

            class D(mod.C):
                async def f(self):
                    await asyncio.sleep(0)
                    return "not an int"

            d = D()
            with self.assertRaises(TypeError):
                asyncio.run(mod.f(d))

    def test_async_method_throw_exception(self):
        codestr = """
            class C:
                async def f(self) -> int:
                    return 42

                async def g(self):
                    coro = self.f()
                    return coro.throw(IndexError("ERROR"))
        """
        with self.in_module(codestr) as mod:

            class D(mod.C):
                async def f(self):
                    return 0

            coro = D().g()
            with self.assertRaises(IndexError):
                coro.send(None)

    def test_async_method_throw(self):
        codestr = """
            class C:
                async def f(self) -> int:
                    return 42

                async def g(self):
                    coro = self.f()
                    return coro.throw(StopIteration(100))
        """
        with self.in_module(codestr) as mod:
            loop = asyncio.new_event_loop()

            class D(mod.C):
                def f(self):
                    return loop.create_future()

            coro = D().g()
            try:
                coro.send(None)
            except RuntimeError as e:
                self.assertEqual(e.__cause__.args[0], 100)
            loop.close()

    def test_async_method_throw_incorrect_type(self):
        codestr = """
            class C:
                async def f(self) -> int:
                    return 42

                async def g(self):
                    coro = self.f()
                    return coro.throw(StopIteration("not an int"))
        """
        with self.in_module(codestr) as mod:
            loop = asyncio.new_event_loop()

            class D(mod.C):
                def f(self):
                    return loop.create_future()

            coro = D().g()
            with self.assertRaises(TypeError):
                coro.send(None)
            loop.close()

    def test_async_method_close(self):
        codestr = """
            class C:
                async def f(self) -> int:
                    return 42

                async def g(self):
                    coro = self.f()
                    return coro.close()
        """
        with self.in_module(codestr) as mod:

            class D(mod.C):
                async def f(self):
                    return 0

            coro = D().g()
            try:
                coro.send(None)
            except StopIteration as e:
                self.assertEqual(e.args, ())

    def test_invoke_frozen_type(self):
        codestr = """
            class C:
                @staticmethod
                def f():
                    return 42

            def g():
                return C.f()
        """
        with self.in_module(codestr, freeze=True) as mod:
            g = mod.g
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
            self.assertInBytecode(g, "INVOKE_FUNCTION", ((mod.__name__, "f"), 0))

    def test_cast_optional_nonexact_type(self):
        codestr = """
            def f(x):
                a: str | None = x
                return 0
        """
        with self.in_module(codestr, freeze=True) as mod:
            f = mod.f
            self.assertInBytecode(f, "CAST")
            for i in range(100):
                self.assertEqual(f("AA" if i % 2 == 0 else None), 0)

    def test_cast_exact_shadowcode(self):
        codestr = """
            from typing import Annotated
            def f(x) -> int:
                a: Annotated[int, "Exact"] = x
                return a
        """
        with self.in_module(codestr, freeze=True) as mod:
            f = mod.f
            self.assertInBytecode(f, "CAST")
            for i in range(100):
                self.assertEqual(f(i), i)

    def test_cast_optional_exact_shadowcode(self):
        codestr = """
            from typing import Annotated
            def f(x) -> int | None:
                a: Annotated[int | None, "Exact"] = x
                return a
        """
        with self.in_module(codestr, freeze=True) as mod:
            f = mod.f
            self.assertInBytecode(f, "CAST")
            for i in range(100):
                x = i if i % 2 == 0 else None
                self.assertEqual(f(x), x)

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
            self.assertInBytecode(g, "INVOKE_FUNCTION", ((mod.__name__, "f"), 1))

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
            self.assertInBytecode(g, "INVOKE_FUNCTION", ((mod.__name__, "f"), 2))

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
                ((mod.__name__, "target"), 6),
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
                ((mod.__name__, "target"), 7),
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
            self.assertInBytecode(g, "INVOKE_FUNCTION", ((mod.__name__, "f11"), 0))

    @disable_hir_inliner
    def test_invoke_strict_module_deep_unjitable(self):
        codestr = """
            def f12(): return 42
            def f11():
                class C: pass
                return f12()
            def f10(): return f11()
            def f9(): return f10()
            def f8(): return f9()
            def f7(): return f8()
            def f6(): return f7()
            def f5(): return f6()
            def f4(): return f5()
            def f3(): return f4()
            def f2(): return f3()
            def f1(): return f2()

            def g(x):
                if x: return 0

                return f1()
        """
        with self.in_strict_module(codestr) as mod:
            g = mod.g
            self.assertEqual(g(True), 0)
            # we should have done some level of pre-jitting
            self.assert_not_jitted(mod.f10)
            self.assert_not_jitted(mod.f11)
            self.assert_not_jitted(mod.f12)
            [self.assert_jitted(getattr(mod, f"f{i}")) for i in range(1, 10)]
            self.assertEqual(g(False), 42)
            self.assertInBytecode(
                g,
                "INVOKE_FUNCTION",
                ((mod.__name__, "f1"), 0),
            )

    def test_invoke_strict_module_deep_unjitable_many_args(self):
        codestr = """
            def f0(): return 42
            X = 42
            def f1(a, b, c, d, e, f, g, h):
                global X; X = 42; del X
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
                ((mod.__name__, "f11"), 0),
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
                ((mod.__name__, "fib"), 1),
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
                ((mod.__name__, "fib1"), 1),
            )
            self.assertInBytecode(
                fib1,
                "INVOKE_FUNCTION",
                ((mod.__name__, "fib"), 1),
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
                ((mod.__name__, "f"), 0),
            )

    def test_module_level_final_decl(self):
        codestr = """
        from typing import Final

        x: Final
        """
        with self.assertRaisesRegex(
            TypedSyntaxError, "Must assign a value when declaring a Final"
        ):
            self.compile(codestr, modname="foo")

    def test_frozenset_constant(self):
        codestr = """
        from __static__ import inline

        @inline
        def i(s: str) -> bool:
            return i in {"a", "b"}

        def t() -> bool:
            return i("p")
        """
        self.compile(codestr, modname="foo")

    def test_exact_float_type(self):
        codestr = """
        def foo():
            f = float("1.0")
            reveal_type(f)
        """
        with self.assertRaisesRegex(
            TypedSyntaxError,
            r"reveal_type\(f\): 'Exact\[float\]'",
        ):
            self.compile(codestr)

    def test_default_type_error(self):
        codestr = """
        def foo(x: int = "") -> int:
            return x
        """
        self.type_error(codestr, r"type mismatch: str cannot be assigned to int")

    def test_default_type_error_with_non_defaults(self):
        codestr = """
        def foo(non_default: int, x: int = "") -> int:
            return non_default + x
        """
        self.type_error(codestr, r"type mismatch: str cannot be assigned to int")

    def test_default_type_error_with_positional_only_arguments(self):
        codestr = """
        def foo(x: int = "", /) -> int:
            return x
        """
        self.type_error(codestr, r"type mismatch: str cannot be assigned to int")

    def test_default_type_error_with_keywords(self):
        codestr = """
        def foo(x: int, *, y: int, z: int = "") -> int:
            return x + y + z
        """
        self.type_error(codestr, r"type mismatch: str cannot be assigned to int")

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
        with self.in_module(codestr) as mod:
            f = mod.f
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
        for enable_patching in [False, True]:
            with self.subTest(enable_patching=enable_patching):
                with self.in_module(codestr, enable_patching=enable_patching) as mod:
                    g = mod.g
                    if not enable_patching:
                        self.assertInBytecode(g, "LOAD_CONST", 3)
                    else:
                        self.assertInBytecode(
                            g, "INVOKE_FUNCTION", ((mod.__name__, "f"), 2)
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
            g = mod.g
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
            g = mod.g
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
            g = mod.g
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
            g = mod.g
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
            g = mod.g
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
            g = mod.g
            self.assertInBytecode(g, "INVOKE_FUNCTION", (((mod.__name__, "f"), 2)))

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
            g = mod.g
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
            g = mod.g
            self.assertInBytecode(g, "PRIMITIVE_BOX")
            self.assertEqual(g(3), 3)

    def test_inline_arg_type_mismatch(self):
        codestr = """
            from __static__ import inline

            @inline
            def f(x: int) -> bool:
                return x == 1

            def g(arg: str) -> bool:
                return f(arg)
        """
        with self.assertRaisesRegex(
            TypedSyntaxError, r"str received for positional arg 'x', expected int"
        ):
            self.compile(codestr)

    def test_inline_return_type_mismatch(self):
        codestr = """
            from __static__ import inline

            @inline
            def f() -> int:
                return 1

            def g() -> str:
                return f()
        """
        with self.assertRaisesRegex(TypedSyntaxError, bad_ret_type("int", "str")):
            self.compile(codestr)

    def test_type_type_final(self):
        codestr = """
        class A(type):
            pass
        """
        self.compile(codestr)

    def test_inlined_nodes_have_line_info(self):
        self.type_error(
            """
            from __static__ import int64, cbool, inline

            @inline
            def x(i: int64) -> cbool:
                return i == "foo"

            def foo(i: int64) -> cbool:
                return x(i)
            """,
            "",
            at="i ==",
        )

    def test_compare_with_attr(self):
        codestr = """
        from __static__ import cbool

        class C:
            def __init__(self) -> None:
                self.running: cbool = False

            def f(self) -> int:
                return 2 if not self.running else 1
        """
        with self.in_module(codestr) as mod:
            C = mod.C
            c = C()
            self.assertEqual(c.f(), 2)

    def test_chained_compare(self):
        for jumpif in [False, True]:
            with self.subTest(jumpif=jumpif):
                if jumpif:
                    pre = ""
                    test = "0 < x < 10"
                else:
                    pre = "y = 0 < x < 10"
                    test = "y"
                codestr = f"""
                    def f(x):
                        {pre}
                        if {test}:
                            return 1
                        return 0
                """
                with self.in_module(codestr) as mod:
                    f = mod.f
                    self.assertEqual(f(0), 0)
                    self.assertEqual(f(1), 1)
                    self.assertEqual(f(9), 1)
                    self.assertEqual(f(10), 0)

    def test_compile_nested_class(self):
        codestr = """
            from typing import ClassVar
            class Outer:
                class Inner:
                    c: ClassVar[int] = 1
        """
        self.compile(codestr)

        codestr = """
            from typing import ClassVar
            class Outer:
                class Inner1:
                    c: ClassVar[int] = 1
                    class Inner2:
                        c: ClassVar[int] = 2
                        class Inner3:
                            c: ClassVar[int] = 3
        """
        self.compile(codestr)

    def test_except_inexact(self):
        codestr = """
        def f(unknown_exception):
            try:
                raise Exception()
            except Exception as e:
                e = unknown_exception()
                return e
        """
        with self.in_module(codestr) as mod:
            self.assertEqual(type(mod.f(SyntaxError)), SyntaxError)

    def test_compile_nested_class_in_fn(self):
        codestr = """

        def fn():
            class C:
                c: int = 1
            return C()

        """

        with self.in_module(codestr) as mod:
            f = mod.fn
            self.assertNotInBytecode(f, "TP_ALLOC")
            self.assertEqual(f().c, 1)

    def test_nested_comprehension_compiles(self):
        codestr = """
        def foo():
            return [[1, [2,3]]]

        def bar():
            return [(a + b + c) for (a, [b, c]) in foo()]
        """
        with self.in_module(codestr) as mod:
            self.assertEqual(mod.bar(), [6])

    def test_nested_dict_comprehension_compiles(self):
        codestr = """
        def foo():
            return [[1, [2,3]]]

        def bar():
            return {a:b+c for (a, [b, c]) in foo()}
        """
        with self.in_module(codestr) as mod:
            self.assertEqual(mod.bar(), {1: 5})

    def test_dunder_name(self):
        codestr = """
        class MyClass:
            @property
            def name(self) -> str:
                return type(self).__name__
        """
        with self.in_module(codestr) as mod:
            self.assertNotInBytecode(mod.MyClass.name.fget, "CAST")
            self.assertEqual(mod.MyClass().name, "MyClass")

    def test_unbox_binop_lhs_literal(self):
        nonstatic_codestr = """
        def f():
            return 42
        """
        with self.in_module(
            nonstatic_codestr, code_gen=PythonCodeGenerator
        ) as nonstatic_mod:
            codestr = f"""
            from __static__ import int64, box

            import {nonstatic_mod.__name__}

            def g() -> int:
                r = int64(1 + int({nonstatic_mod.__name__}.f()))
                return box(r)
            """
            with self.in_module(codestr) as mod:
                self.assertEqual(mod.g(), 43)

    def test_unbox_binop_rhs_literal(self):
        nonstatic_codestr = """
        def f():
            return 42
        """
        with self.in_module(
            nonstatic_codestr, code_gen=PythonCodeGenerator
        ) as nonstatic_mod:
            codestr = f"""
            from __static__ import int64, box

            import {nonstatic_mod.__name__}

            def g() -> int:
                r = int64(int({nonstatic_mod.__name__}.f()) - 1)
                return box(r)
            """
            with self.in_module(codestr) as mod:
                self.assertEqual(mod.g(), 41)

    def test_jump_threading_optimization(self):
        codestr = """
        def f(a, b, c, d) -> bool:
           if (a or b) and (c or d):
               return True
           return False
        """
        with self.in_module(codestr) as mod:
            for v in range(16):
                a = bool(v & 0x8)
                b = bool(v & 0x4)
                c = bool(v & 0x2)
                d = bool(v & 0x1)
                self.assertEqual(mod.f(a, b, c, d), (a | b) & (c | d))


if __name__ == "__main__":
    unittest.main()
