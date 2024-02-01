import asyncio
import cinder
import re
from contextlib import contextmanager
from textwrap import dedent
from unittest import skip, skipIf
from unittest.mock import MagicMock, Mock, patch

from cinderx.compiler.pycodegen import PythonCodeGenerator

from test.support.import_helper import import_module

from .common import StaticTestBase

try:
    import cinderjit
except ImportError:
    cinderjit = None

xxclassloader = import_module("xxclassloader")


@contextmanager
def save_restore_knobs():
    prev = cinder.getknobs()
    yield
    cinder.setknobs(prev)


class StaticPatchTests(StaticTestBase):
    def test_patch_function(self):
        codestr = """
            def f():
                return 42

            def g():
                return f()
        """
        with self.in_module(codestr) as mod:
            g = mod.g
            for i in range(100):
                g()
            with patch(f"{mod.__name__}.f", autospec=True, return_value=100) as p:
                self.assertEqual(g(), 100)

    def test_patch_async_function(self):
        codestr = """
            class C:
                async def f(self) -> int:
                    return 42

                def g(self):
                    return self.f()
        """
        with self.in_module(codestr) as mod:
            C = mod.C
            c = C()
            for i in range(100):
                try:
                    c.g().send(None)
                except StopIteration as e:
                    self.assertEqual(e.args[0], 42)

            with patch(f"{mod.__name__}.C.f", autospec=True, return_value=100) as p:
                try:
                    c.g().send(None)
                except StopIteration as e:
                    self.assertEqual(e.args[0], 100)

    def test_patch_async_method_incorrect_type(self):
        codestr = """
            class C:
                async def f(self) -> int:
                    return 42

                def g(self):
                    return self.f()
        """
        with self.in_module(codestr) as mod:
            C = mod.C
            c = C()
            for i in range(100):
                try:
                    c.g().send(None)
                except StopIteration as e:
                    self.assertEqual(e.args[0], 42)

            with patch(f"{mod.__name__}.C.f", autospec=True, return_value="not an int"):
                with self.assertRaises(TypeError):
                    c.g().send(None)

    def test_patch_async_method_raising(self):
        codestr = """
            class C:
                async def f(self) -> int:
                    return 42

                def g(self):
                    return self.f()
        """

        def raise_error(self):
            raise IndexError("failure!")

        with self.in_module(codestr) as mod:
            C = mod.C
            c = C()
            for i in range(100):
                try:
                    c.g().send(None)
                except StopIteration as e:
                    self.assertEqual(e.args[0], 42)

            with patch(f"{mod.__name__}.C.f", raise_error):
                with self.assertRaises(IndexError):
                    c.g().send(None)

    def test_patch_async_method_non_coroutine(self):
        codestr = """
            class C:
                async def f(self) -> int:
                    return 42

                def g(self):
                    return self.f()
        """

        loop = asyncio.new_event_loop()

        def future_return(self):
            fut = loop.create_future()
            fut.set_result(100)
            return fut

        with self.in_module(codestr) as mod:
            C = mod.C
            c = C()
            for i in range(100):
                try:
                    c.g().send(None)
                except StopIteration as e:
                    self.assertEqual(e.args[0], 42)

            with patch(f"{mod.__name__}.C.f", future_return):
                asyncio.run(c.g())

        loop.close()

    def test_patch_parentclass_slot(self):
        codestr = """
        class A:
            def f(self) -> int:
                return 3

        class B(A):
            pass

        def a_f_invoker() -> int:
            return A().f()

        def b_f_invoker() -> int:
            return B().f()
        """
        with self.in_module(codestr) as mod:
            A = mod.A
            a_f_invoker = mod.a_f_invoker
            b_f_invoker = mod.b_f_invoker
            setattr(A, "f", lambda _: 7)

            self.assertEqual(a_f_invoker(), 7)
            self.assertEqual(b_f_invoker(), 7)

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
        with self.in_module(codestr) as mod:
            g = mod.g
            f = mod.f
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
        with self.in_module(codestr) as mod:
            g = mod.g
            for i in range(100):
                g()
            with patch(
                f"{mod.__name__}.f",
                autospec=True,
                return_value=100,
            ) as p:
                mod.__dict__[42] = 1
                self.assertEqual(g(), 100)

    def test_patch_function_deleted_func(self):
        codestr = """
            def f():
                return 42

            def g():
                return f()
        """
        with self.in_module(codestr) as mod:
            g = mod.g
            for i in range(100):
                g()
            del mod.f
            with self.assertRaisesRegex(
                TypeError,
                re.escape(
                    "bad name provided for class loader, "
                    + f"'f' doesn't exist in ('{mod.__name__}', 'f')"
                ),
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
        with self.in_module(codestr) as mod:
            g = mod.g
            for i in range(100):
                self.assertEqual(g(), 42)
            with patch(f"{mod.__name__}.C.f", autospec=True, return_value=100) as p:
                self.assertEqual(g(), 100)

    def test_patch_staticmethod_with_staticmethod(self):
        codestr = """
            class C:
                @staticmethod
                def f():
                    return 42

            def g():
                return C.f()
        """
        with self.in_module(codestr) as mod:
            g = mod.g
            for i in range(100):
                self.assertEqual(g(), 42)

            @staticmethod
            def new():
                return 100

            mod.C.f = new
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
        with self.in_module(codestr) as mod:
            g = mod.g
            for i in range(100):
                g()
            with patch(f"{mod.__name__}.C.f", return_value=100) as p:
                self.assertEqual(g(), 100)

    @save_restore_knobs()
    def test_patch_function_non_autospec(self):
        codestr = """
            from typing import final

            @final
            class C:
                def f(self):
                    return 42

            def g(x: C):
                return x.f()
        """
        with self.in_module(codestr) as mod:
            g = mod.g
            c = mod.C()
            with patch(f"{mod.__name__}.C.f", return_value=100) as p:
                self.assertEqual(g(c), 100)
                self.assertEqual(len(p.call_args[0]), 0)

    @save_restore_knobs()
    def test_patch_coro_non_autospec(self):
        codestr = """
            from typing import final

            @final
            class C:
                async def f(self) -> int:
                    return 42

            def g(x: C):
                return x.f()
        """
        with self.in_module(codestr) as mod:
            g = mod.g
            c = mod.C()
            with patch(f"{mod.__name__}.C.f", return_value=100) as p:
                try:
                    g(c).send(None)
                except StopIteration as e:
                    self.assertEqual(e.args[0], 100)
                self.assertEqual(len(p.call_args[0]), 0)

    def test_patch_classmethod_non_autospec(self):
        codestr = """
            from typing import final, Type

            @final
            class C:
                @classmethod
                def f(cls):
                    return 42

            def g(x: C):
                return x.f()

            def g2():
                return C.f()
        """
        with self.in_module(codestr) as mod:
            g = mod.g
            c = mod.C()

            with patch(f"{mod.__name__}.C.f", return_value=100) as p:
                self.assertEqual(g(c), 100)
                self.assertEqual(len(p.call_args[0]), 0)

            g2 = mod.g2
            with patch(f"{mod.__name__}.C.f", return_value=100) as p:
                self.assertEqual(g2(), 100)
                self.assertEqual(len(p.call_args[0]), 0)

    @save_restore_knobs()
    def test_patch_function_module_non_autospec(self):
        codestr = """
            from typing import final

            class C:
                    pass

            def f(self: C):
                return 42

            def g(x: C):
                return f(x)
        """
        with self.in_strict_module(codestr, enable_patching=True) as mod:
            g = mod.g
            c = mod.C()
            p = MagicMock(return_value=100)
            mod.patch(f"f", p)
            self.assertEqual(g(c), 100)
            self.assertEqual(len(p.call_args[0]), 1)
            self.assertEqual(p.call_args[0][0], c)

    @save_restore_knobs()
    def test_patch_function_descriptor(self):
        class Patch:
            def __init__(self):
                self.call = None
                self.get_called = False

            def __get__(self, inst, ctx) -> object:
                self.get_called = True
                return self

            def __call__(self, *args, **kwargs):
                self.call = args, kwargs
                return 100

        codestr = """
            from typing import final

            @final
            class C:
                def f(self):
                    return 42

            def g(x: C):
                return x.f()
        """
        with self.in_module(codestr) as mod:
            g = mod.g
            C = mod.C
            c = mod.C()
            p = Patch()
            C.f = p
            self.assertEqual(g(c), 100)
            self.assertEqual(p.get_called, True)
            self.assertEqual(p.call, ((), {}))

    def test_patch_primitive_ret_type(self):
        for type_name, value, patched in [
            ("cbool", True, False),
            ("cbool", False, True),
            ("int8", 0, 1),
            ("int16", 0, 1),
            ("int32", 0, 1),
            ("int64", 0, 1),
            ("uint8", 0, 1),
            ("uint16", 0, 1),
            ("uint32", 0, 1),
            ("uint64", 0, 1),
        ]:
            with self.subTest(type_name=type, value=value, patched=patched):
                codestr = f"""
                    from __static__ import {type_name}, box
                    class C:
                        def f(self) -> {type_name}:
                            return {value!r}

                    def g():
                        return box(C().f())
                """
                with self.in_module(codestr) as mod:
                    g = mod.g
                    for i in range(100):
                        self.assertEqual(g(), value)
                    with patch(f"{mod.__name__}.C.f", return_value=patched) as p:
                        self.assertEqual(g(), patched)

    def test_patch_primitive_ret_type_overflow(self):
        codestr = f"""
            from __static__ import int8, box
            class C:
                def f(self) -> int8:
                    return 1

            def g():
                return box(C().f())
        """
        with self.in_module(codestr) as mod:
            g = mod.g
            for i in range(100):
                self.assertEqual(g(), 1)
            with patch(f"{mod.__name__}.C.f", return_value=256) as p:
                with self.assertRaisesRegex(
                    OverflowError,
                    "unexpected return type from C.f, expected "
                    "int8, got out-of-range int \\(256\\)",
                ):
                    g()

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
            self.assertInBytecode(g, "INVOKE_FUNCTION", ((mod.__name__, "f"), 0))
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
            self.assertInBytecode(g, "INVOKE_FUNCTION", ((mod.__name__, "f"), 0))
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
        with self.in_module(codestr) as mod:
            g = mod.g
            C = mod.C
            orig = C.f
            C.f = lambda *args: args
            for i in range(100):
                v = g()
                self.assertEqual(type(v), tuple)
                self.assertEqual(type(v[0]), C)
            C.f = orig
            self.assertEqual(g(), None)

    def test_patch_property_with_instance_and_override_dict(self):
        codestr = """
            class C:
                @property
                def f(self):
                    return 42

                def get_f(self):
                    return self.f
        """

        with self.in_module(codestr) as mod:
            C = mod.C

            class D(C):
                pass

            C.f = 42
            a = D()
            a.f = 100
            self.assertEqual(a.get_f(), 100)

    def test_patch_static_to_static(self):
        codestr = """
            class C:
                def f(self) -> str:
                    return 'ABC'

                def g(self) -> int:
                    return 42

                def get_lower_f(self):
                    return self.f().lower()
        """

        with self.in_module(codestr) as mod:
            C = mod.C
            C.f = C.g
            with self.assertRaisesRegex(
                TypeError, "unexpected return type from C.f, expected str, got int"
            ):
                self.assertEqual(C().get_lower_f(), "ABC")

    def test_static_not_inherited_from_nonstatic(self):
        codestr = """
            class C:
                def f(self) -> str:
                    return 'ABC'

                def g(self) -> int:
                    return 42

                def get_lower_f(self):
                    return self.f().lower()
        """

        with self.in_module(codestr) as mod:
            C = mod.C

            class D(C):
                f = C.g

            class E(D):
                pass

            with self.assertRaisesRegex(
                TypeError, "unexpected return type from E.f, expected str, got int"
            ):
                self.assertEqual(E().get_lower_f(), "ABC")

    def test_patch_method_mock(self):
        codestr = """
            class C:
                def f(self, a):
                    pass

                def g(self):
                    return self.f(42)
        """

        with self.in_module(codestr) as mod:
            C = mod.C
            orig = C.f
            with patch(f"{mod.__name__}.C.f") as p:
                C().g()
                self.assertEqual(p.call_args_list[0][0], (42,))

    def test_patch_async_method_mock(self):
        codestr = """
            class C:
                async def f(self, a):
                    pass

                async def g(self):
                    return await self.f(42)
        """

        with self.in_module(codestr) as mod:
            C = mod.C
            with patch(f"{mod.__name__}.C.f") as p:
                asyncio.run(C().g())
                self.assertEqual(p.call_args_list[0][0], (42,))

    def test_patch_async_method_descr(self):
        codestr = """
            class C:
                async def f(self, a):
                    return 'abc'

                async def g(self):
                    return await self.f(42)
        """

        with self.in_module(codestr) as mod:
            C = mod.C

            class Descr:
                def __get__(self, inst, ctx):
                    return self.f

                async def f(self, a):
                    return a

            C.f = Descr()
            self.assertEqual(asyncio.run(C().g()), 42)

    def test_patch_async_static_method(self):
        codestr = """
            from typing import final

            @final
            class C:
                @staticmethod
                async def f():
                    return 'abc'

                async def g(self):
                    return await self.f()
        """

        with self.in_module(codestr) as mod:
            C = mod.C
            with patch(f"{mod.__name__}.C.f") as p:
                asyncio.run(C().g())
                self.assertEqual(p.call_args_list[0][0], ())

    def test_patch_method_ret_none_error(self):
        codestr = """
            class C:
                def f(self) -> None:
                    pass

            def g():
                return C().f()
        """
        with self.in_module(codestr) as mod:
            g = mod.g
            C = mod.C
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
        with self.in_module(codestr) as mod:
            g = mod.g
            C = mod.C
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
        with self.in_module(codestr) as mod:
            g = mod.g
            C = mod.C
            C.f = lambda *args: "abc"
            with self.assertRaisesRegex(
                TypeError, "unexpected return type from C.f, expected int, got str"
            ):
                v = g()

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
        with self.in_module(codestr) as mod:
            # Now cause vtables to be inited
            self.assertEqual(mod.f([1, 2]), [2, 1])

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
        with self.in_module(codestr) as mod:
            # Now cause vtables to be inited
            self.assertEqual(mod.f([1, 2]), [2, 1])

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
        with self.in_module(codestr) as mod:
            SubType = mod.SubType
            # Now invoke:
            self.assertEqual(mod.f(SubType()), 2)

            # And replace the function again, forcing us to find the right slot type:
            def badfoo(self):
                return "foo"

            SubType.foo = badfoo

            with self.assertRaisesRegex(TypeError, "expected int, got str"):
                mod.f(SubType())

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
            with self.in_module(codestr) as mod:
                SubType = mod.SubType
                badfoo = mod.badfoo

                # And replace the function again, forcing us to find the right slot type:
                SubType.foo = badfoo

                with self.assertRaisesRegex(TypeError, "expected int, got str"):
                    mod.f(SubType())

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
        with self.in_module(codestr) as mod:
            Grand = mod.Grand
            grandfoo = mod.grandfoo
            f = mod.f

            # init vtables
            self.assertEqual(f(Grand()), 1)

            # patch in an override of the grandparent method
            Grand.foo = grandfoo

            with self.assertRaisesRegex(TypeError, "expected int, got str"):
                f(Grand())

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

        code = self.compile(codestr, modname="foo")
        x = self.find_code(code, "x")
        self.assertInBytecode(x, "INVOKE_METHOD", (("foo", "C", "f"), 0))

        with self.in_module(codestr) as mod:
            x, C = mod.x, mod.C
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
            B = mod.B
            f = mod.f
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
            B = mod.B
            D = mod.D
            f = mod.f
            b = B()
            d = D()
            self.assertEqual(f(b), b)
            self.assertEqual(f(d), d)
            del B.f

            with self.assertRaises(AttributeError):
                f(b)
            with self.assertRaises(AttributeError):
                f(d)

    def test_override_remove_overridden_base_method(self):
        codestr = """
        from typing import Optional
        class B:
            def f(self) -> "B":
                return self

        class D(B):
            def f(self) -> "B":
                return self

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
            del B.f
            del D.f

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
            B = mod.B
            D = mod.D
            f = mod.f
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
            B = mod.B
            f = mod.f
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
            B = mod.B
            f = mod.f
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
            B = mod.B
            D = mod.D
            f = mod.f

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
            B = mod.B
            D = mod.D
            f = mod.f

            b = B()
            d = D()
            self.assertEqual(f(b), b)
            self.assertEqual(f(d), d)

            B.f = lambda self: None
            self.assertEqual(f(b), None)
            self.assertEqual(f(d), None)

    def test_patch_final_bad_ret_heap_type(self):
        codestr = """
            from typing import final

            class A:
                def __init__(self):
                    self.x: int = 42
            class B:
                def __init__(self):
                    self.y = 'abc'

            @final
            class C:
                def f(self) -> A:
                    return A()
                def g(self) -> int:
                    return self.f().x
        """
        with self.in_module(codestr) as mod:
            C = mod.C
            B = mod.B
            c = C()
            C.f = lambda self: B()

            with self.assertRaisesRegex(
                TypeError, "unexpected return type from C.f, expected A, got B"
            ):
                c.g()

    def test_patch_final_bad_ret(self):
        codestr = """
            from typing import final

            @final
            class C:
                def f(self) -> int:
                    return 42
                def g(self) -> int:
                    return self.f()
        """
        with self.in_module(codestr) as mod:
            C = mod.C
            c = C()
            C.f = lambda self: "abc"

            with self.assertRaisesRegex(
                TypeError, "unexpected return type from C.f, expected int, got str"
            ):
                c.g()

            C.f = lambda self: 1.0

            with self.assertRaisesRegex(
                TypeError, "unexpected return type from C.f, expected int, got float"
            ):
                c.g()

    def test_patch_final_bad_ret_del(self):
        codestr = """
            from typing import final

            @final
            class C:
                def f(self) -> int:
                    return 42
                def g(self) -> int:
                    return self.f()
        """
        with self.in_module(codestr) as mod:
            C = mod.C
            c = C()
            C.f = lambda self: "abc"

            for i in range(100):
                with self.assertRaisesRegex(
                    TypeError, "unexpected return type from C.f, expected int, got str"
                ):
                    c.g()

            del C.f

            with self.assertRaisesRegex(TypeError, "C.f has been deleted"):
                c.g()

    def test_patch_final_async_function(self):
        codestr = """
            from typing import final

            @final
            class C:
                async def f(self) -> int:
                    return 42

                def g(self):
                    return self.f()
        """
        with self.in_module(codestr) as mod:
            C = mod.C
            c = C()
            for i in range(100):
                try:
                    c.g().send(None)
                except StopIteration as e:
                    self.assertEqual(e.args[0], 42)

            with patch(f"{mod.__name__}.C.f", autospec=True, return_value=100) as p:
                try:
                    c.g().send(None)
                except StopIteration as e:
                    self.assertEqual(e.args[0], 100)

    def test_patch_final_classmethod(self):
        codestr = """
            from typing import final

            @final
            class C:
                @classmethod
                def f(cls) -> int:
                    return 42

                @classmethod
                def g(cls) -> int:
                    return cls.f()
        """
        with self.in_module(codestr) as mod:
            C = mod.C

            with patch.object(C, "f", wraps=C.f) as p:
                self.assertEqual(C.f(), 42)
                self.assertInBytecode(C.g, "INVOKE_FUNCTION")
                # Ensure that the invoke in g() also hits the patched function.
                self.assertEqual(C.g(), 42)

    def test_patch_final_async_classmethod(self):
        codestr = """
            from typing import final

            @final
            class C:
                @classmethod
                async def f(cls) -> int:
                    return 44

                @classmethod
                async def g(cls) -> int:
                    return await cls.f()
        """
        with self.in_module(codestr) as mod:
            C = mod.C
            with patch.object(C, "f", wraps=C.f) as p:
                self.assertEqual(asyncio.run(C.f()), 44)
                self.assertInBytecode(C.g, "INVOKE_FUNCTION")
                # Ensure that the invoke in g() also hits the patched function.
                self.assertEqual(asyncio.run(C.g()), 44)

    def test_patch_classmethod(self):
        codestr = """
            class C:
                @classmethod
                def f(cls) -> int:
                    return 42

                @classmethod
                def g(cls) -> int:
                    return cls.f()
        """
        with self.in_module(codestr) as mod:
            C = mod.C

            with patch.object(C, "f", wraps=C.f) as p:
                self.assertEqual(C.f(), 42)
                self.assertInBytecode(C.g, "INVOKE_METHOD")
                # Ensure that the invoke in g() also hits the patched function.
                self.assertEqual(C.g(), 42)

    def test_patch_async_classmethod(self):
        codestr = """
            class C:
                @classmethod
                async def f(cls) -> int:
                    return 44

                @classmethod
                async def g(cls) -> int:
                    return await cls.f()
        """
        with self.in_module(codestr) as mod:
            C = mod.C
            with patch.object(C, "f", wraps=C.f) as p:
                self.assertEqual(asyncio.run(C.f()), 44)
                self.assertInBytecode(C.g, "INVOKE_METHOD")
                # Ensure that the invoke in g() also hits the patched function.
                self.assertEqual(asyncio.run(C.g()), 44)

    def test_patch_final_async_method_incorrect_type(self):
        codestr = """
            from typing import final

            @final
            class C:
                async def f(self) -> int:
                    return 42

                def g(self):
                    return self.f()
        """
        with self.in_module(codestr) as mod:
            C = mod.C
            c = C()
            for i in range(100):
                try:
                    c.g().send(None)
                except StopIteration as e:
                    self.assertEqual(e.args[0], 42)

            with patch(f"{mod.__name__}.C.f", autospec=True, return_value="not an int"):
                with self.assertRaises(TypeError):
                    c.g().send(None)

    def test_patch_property_bad_ret(self):
        codestr = """
            class C:
                @property
                def f(self) -> int:
                    return 42
                def g(self) -> int:
                    return self.f
        """
        with self.in_module(codestr) as mod:
            C = mod.C
            c = C()
            C.f = property(lambda self: "abc")

            with self.assertRaisesRegex(
                TypeError, "unexpected return type from C.f, expected int, got str"
            ):
                c.g()

    def test_patch_property_bad_ret_final(self):
        codestr = """
            from typing import final
            @final
            class C:
                @property
                def f(self) -> int:
                    return 42
                def g(self) -> int:
                    return self.f
        """
        with self.in_module(codestr) as mod:
            C = mod.C
            c = C()
            C.f = property(lambda self: "abc")

            with self.assertRaisesRegex(
                TypeError, "unexpected return type from C.f, expected int, got str"
            ):
                c.g()

    def test_primitive_boxing_with_patching_leaves_original_values_intact(self):
        codestr = """
            from __static__ import int64
            def takes_int64(x: int64) -> None:
                pass
            def foo(b: bool) -> int64:
                x: int64 = 42
                if b:
                    takes_int64(x)
                x = 43  # Ensure that we don't hit the assert of x having type Long|CInt64
                return x
        """
        with self.in_strict_module(codestr, enable_patching=True) as mod:
            f = mod.foo
            self.assertEqual(f(True), 43)

    def test_no_inline_with_patching(self):
        codestr = """
            from __static__ import int64, cbool, inline

            @inline
            def x(i: int64) -> cbool:
                return i == 1

            def foo(i: int64) -> cbool:
                return x(i)
        """
        with self.in_module(codestr, optimize=2, enable_patching=True) as mod:
            foo = mod.foo
            self.assertEqual(foo(0), False)
            self.assertEqual(foo(1), True)
            self.assertEqual(foo(2), False)
            self.assertNotInBytecode(foo, "STORE_LOCAL")
            self.assertInBytecode(foo, "INVOKE_FUNCTION")

    def test_patch_namespace_local(self):
        acode = """
            def f() -> int:
                return 1
        """
        bcode = """
            from a import f

            def g():
                return f() * 10
        """
        comp = self.compiler(a=acode, b=bcode)
        with comp.in_module("b") as bmod:
            self.assertEqual(bmod.g(), 10)

            bmod.f = lambda: 2

            self.assertEqual(bmod.g(), 20)

    def test_patch_namespace_re_export(self):
        acode = """
            def f() -> int:
                return 1
        """
        bcode = """
            from a import f
        """
        ccode = """
            import b

            def g():
                return b.f() * 10
        """
        comp = self.compiler(a=acode, b=bcode, c=ccode)
        with comp.in_module("c") as cmod:
            self.assertEqual(cmod.g(), 10)

            cmod.b.f = lambda: 2

            self.assertEqual(cmod.g(), 20)

    def test_patch_namespace_origin(self):
        acode = """
            def f() -> int:
                return 1
        """
        bcode = """
            import a

            def g():
                return a.f() * 10
        """
        comp = self.compiler(a=acode, b=bcode)
        with comp.in_module("b") as bmod:
            self.assertEqual(bmod.g(), 10)

            bmod.a.f = lambda: 2

            self.assertEqual(bmod.g(), 20)

    def test_patch_namespace_locally_reassigned(self):
        acode = """
            def f() -> int:
                return 1
        """
        for kind in ["import_as", "assign"]:
            with self.subTest(kind=kind):
                imp = (
                    "from a import f as ff"
                    if kind == "import_as"
                    else "import a; ff = a.f"
                )
                bcode = f"""
                    {imp}
                    def g():
                        return ff() * 10
                """
                comp = self.compiler(a=acode, b=bcode)
                with comp.in_module("b") as bmod:
                    self.assertEqual(bmod.g(), 10)

                    bmod.ff = lambda: 2

                    self.assertEqual(bmod.g(), 20)

    def test_double_patch_final_property(self):
        codestr = """
            from typing import final

            @final
            class C:
                def f(self) -> int:
                    return self.prop

                @property
                def prop(self) -> int:
                    return 1
        """
        with self.in_module(codestr) as mod:
            c = mod.C()
            self.assertEqual(c.f(), 1)
            mod.C.prop = property(lambda s: 2)
            self.assertEqual(c.f(), 2)
            mod.C.prop = property(lambda s: 3)
            self.assertEqual(c.f(), 3)

    def test_double_patch_inherited_property(self):
        codestr = """
            class B:
                def f(self) -> int:
                    return self.prop

                @property
                def prop(self) -> int:
                    return 1

            class C(B):
                pass
        """
        with self.in_module(codestr) as mod:
            c = mod.C()
            self.assertEqual(c.f(), 1)
            mod.C.prop = property(lambda s: 2)
            self.assertEqual(c.f(), 2)
            mod.C.prop = property(lambda s: 3)
            self.assertEqual(c.f(), 3)

    def test_patch_property_custom_patch_before_use(self):
        codestr = """
            class C:
                @property
                def prop(self) -> int:
                    return 1

            def f(x: C):
                return x.prop
        """

        class Desc:
            def __get__(self, inst, ctx):
                return 42

        with self.in_module(codestr) as mod:
            mod.C.prop = Desc()
            self.assertEqual(mod.f(mod.C()), 42)

    def test_patch_property_custom_desc(self):
        codestr = """
            class C:
                @property
                def prop(self) -> int:
                    return 1

            def f(x: C):
                return x.prop
        """

        class Desc:
            def __get__(self, inst, ctx):
                return 42

        with self.in_module(codestr) as mod:
            self.assertEqual(mod.f(mod.C()), 1)
            mod.C.prop = Desc()
            self.assertEqual(mod.f(mod.C()), 42)

    def test_patch_property_custom_desc_set(self):
        codestr = """
            class C:
                def __init__(self):
                    self.value = 0

                @property
                def prop(self) -> int:
                    return self.value

                @prop.setter
                def prop(self, value) -> None:
                    self.value = value

            def f(x: C):
                x.prop = 42
        """
        called = False

        class Desc:
            def __get__(self, inst, ctx):
                return 42

            def __set__(self, inst, value):
                nonlocal called
                called = True

        with self.in_module(codestr) as mod:
            c = mod.C()
            mod.f(c)
            self.assertEqual(c.value, 42)
            mod.C.prop = Desc()
            mod.f(c)
            self.assertEqual(c.value, 42)
            self.assertTrue(called)

    def test_patch_property_custom_desc_bad_ret(self):
        codestr = """
            class C:
                @property
                def prop(self) -> int:
                    return 1

            def f(x: C):
                return x.prop
        """

        class Desc:
            def __get__(self, inst, ctx):
                return "abc"

        with self.in_module(codestr) as mod:
            self.assertEqual(mod.f(mod.C()), 1)
            mod.C.prop = Desc()
            self.assertRaises(
                TypeError,
                "unexpected return type from C.prop, expected int, got str",
                mod.f,
                mod.C(),
            )

    def test_patch_readonly_property_with_settable(self):
        codestr = """
            class C:
                @property
                def prop(self) -> int:
                    return 1

                def f(self):
                    self.prop = 3
        """
        with self.in_module(codestr) as mod:
            c = mod.C()
            self.assertEqual(c.prop, 1)
            calls = []

            def _set(self, val):
                calls.append(val)

            mod.C.prop = property(lambda s: 2, _set)
            c.f()
            self.assertEqual(calls, [3])

    def test_patch_settable_property_with_readonly(self):
        codestr = """
            class C:
                def __init__(self, prop: int) -> None:
                    self._prop = prop

                @property
                def prop(self) -> int:
                    return self._prop

                @prop.setter
                def prop(self, value: int) -> None:
                    self._prop = value

                def f(self, prop: int) -> None:
                    self.prop = prop
        """
        with self.in_module(codestr) as mod:
            c = mod.C(2)
            self.assertEqual(c.prop, 2)
            mod.C.prop = property(lambda s: s._prop * 10)
            self.assertEqual(c.prop, 20)
            with self.assertRaisesRegex(AttributeError, r"can't set attribute"):
                c.f(3)

    def test_patch_property_del(self):
        codestr = """
            class C:
                def __init__(self, prop: int) -> None:
                    self._prop = prop

                @property
                def prop(self) -> int:
                    return self._prop

                @prop.setter
                def prop(self, val: int) -> None:
                    self._prop = val

                def get(self) -> int:
                    return self.prop

                def set(self, val: int) -> None:
                    self.prop = val
        """
        with self.in_module(codestr) as mod:
            c = mod.C(1)
            self.assertEqual(c.get(), 1)
            c.set(2)
            self.assertEqual(c.get(), 2)
            del mod.C.prop
            with self.assertRaisesRegex(
                AttributeError, "'C' object has no attribute 'prop'"
            ):
                c.prop
            with self.assertRaisesRegex(
                AttributeError, "'C' object has no attribute 'prop'"
            ):
                c.prop = 2
            with self.assertRaisesRegex(
                AttributeError, "'C' object has no attribute 'prop'"
            ):
                c.get()
            with self.assertRaisesRegex(
                AttributeError, "'C' object has no attribute 'prop'"
            ):
                c.set(3)

    def test_patch_method_del(self):
        codestr = """
            class C:
                def f(self) -> int:
                    return 1

                def g(self) -> int:
                    return self.f()
        """
        with self.in_module(codestr) as mod:
            c = mod.C()
            self.assertEqual(c.g(), 1)
            del mod.C.f
            with self.assertRaisesRegex(
                AttributeError, "'C' object has no attribute 'f'"
            ):
                c.f()
            with self.assertRaisesRegex(
                AttributeError, "'C' object has no attribute 'f'"
            ):
                c.g()

    def test_patch_property_del_on_base(self):
        codestr = """
            class B:
                def __init__(self, prop: int) -> None:
                    self._prop = prop

                @property
                def prop(self) -> int:
                    return self._prop

            class C(B):
                def get(self) -> int:
                    return self.prop
        """
        with self.in_module(codestr) as mod:
            c = mod.C(1)
            self.assertEqual(c.get(), 1)
            del mod.B.prop
            with self.assertRaisesRegex(
                AttributeError, "'C' object has no attribute 'prop'"
            ):
                c.prop
            with self.assertRaisesRegex(
                AttributeError, "'C' object has no attribute 'prop'"
            ):
                c.get()

    def test_patch_cached_property_with_descr(self):
        codestr = """
        from cinder import cached_property

        class C:
            @cached_property
            def x(self) -> int:
                return 3

        def f(c: C) -> int:
            return c.x
        """
        with self.in_strict_module(codestr, freeze=False) as mod:
            setattr(mod.C, "x", 42)
            self.assertEqual(mod.C().x, 42)
            self.assertEqual(mod.f(mod.C()), 42)

    def test_property_patch_with_bad_type(self):
        codestr = """
        class C:
            @property
            def x(self) -> int:
                return 3

        """
        with self.in_strict_module(codestr, freeze=False) as mod:
            with self.assertRaisesRegex(
                TypeError, "Cannot assign a str, because C.x is expected to be a int"
            ):
                setattr(mod.C, "x", "42")

            # ensures that the value was not patched
            self.assertEqual(mod.C().x, 3)

    def test_property_patch_with_good_type(self):
        codestr = """
        class C:
            @property
            def x(self) -> int:
                return 3

        def f(c: C) -> int:
            return c.x
        """
        with self.in_strict_module(codestr, freeze=False) as mod:
            c = mod.C()
            setattr(mod.C, "x", 42)
            self.assertEqual(c.x, 42)
            self.assertEqual(mod.f(c), 42)

    def test_cached_property_patch_with_bad_type(self):
        codestr = """
        from cinder import cached_property

        class C:
            @cached_property
            def x(self) -> int:
                return 3

        def f(c: C) -> int:
            return c.x
        """
        with self.in_strict_module(codestr, freeze=False) as mod:
            with self.assertRaisesRegex(
                TypeError, "Cannot assign a str, because C.x is expected to be a int"
            ):
                setattr(mod.C, "x", "42")

    def test_cached_property_patch_with_good_type(self):
        codestr = """
        from cinder import cached_property


        class C:
            @cached_property
            def x(self) -> int:
                return 3

        def f(c: C) -> int:
            return c.x
        """
        with self.in_strict_module(codestr, freeze=False) as mod:
            c = mod.C()
            setattr(mod.C, "x", 42)
            self.assertEqual(c.x, 42)
            self.assertEqual(mod.f(c), 42)

    def test_cached_property_patch_with_none(self):
        codestr = """
        from cinder import cached_property
        from typing import Optional

        class C:
            @cached_property
            def x(self) -> Optional[int]:
                return 3

        def f(c: C) -> Optional[int]:
            return c.x
        """
        with self.in_strict_module(codestr, freeze=False) as mod:
            c = mod.C()
            setattr(mod.C, "x", None)
            self.assertEqual(c.x, None)
            self.assertEqual(mod.f(c), None)

    def test_invoke_after_patch_nonstatic_base(self):
        nonstaticcodestr = """
        class B:
            pass
        """
        with self.in_module(
            nonstaticcodestr, code_gen=PythonCodeGenerator
        ) as nonstatic_module:
            codestr = f"""
            from {nonstatic_module.__name__} import B

            class C(B):

                def p(self) -> int:
                    return self.q()

                def q(self) -> int:
                    return 3

            class D(C):
                pass
            """
            with self.in_strict_module(codestr, freeze=False) as mod:
                D = mod.D
                C = mod.C

                # First initialize D for patching, this populates D's vt_original
                setattr(D, "r", 4)

                d = D()
                # Next, invoke a method on the base-class (C) through the patched subclass (D)
                self.assertEqual(d.p(), 3)

    def test_patch_static_function_in_strict_module(self):
        codestr = """
            def f() -> int:
                return 1

            def g() -> int:
                return f()
        """
        for call_first in [True, False]:
            with self.subTest(call_first=call_first):
                with self.in_strict_module(codestr, enable_patching=True) as mod:
                    if call_first:
                        self.assertEqual(mod.g(), 1)
                    mod.patch("f", lambda: "foo")
                    with self.assertRaisesRegex(TypeError, r"expected int, got str"):
                        mod.g()
                    mod.patch("f", lambda: 2)
                    self.assertEqual(mod.g(), 2)

    def test_patch_static_function_in_strict_module_cross_module(self):
        defmod_code = """
            def f() -> int:
                return 1
        """
        midmod_code = """
            from defmod import f
        """
        usemod_code = """
            from midmod import f as x

            def g() -> int:
                return x()
        """
        for val in [2, "foo"]:
            for patch_mods in [
                {"defmod"},
                {"midmod"},
                {"usemod"},
                {"defmod", "usemod"},
            ]:
                with self.subTest(val=val, patch_mods=patch_mods):
                    compiler = self.strict_patch_compiler(
                        defmod=defmod_code, midmod=midmod_code, usemod=usemod_code
                    )
                    module_gen = compiler.gen_modules()
                    defmod = next(module_gen)
                    if "defmod" in patch_mods:
                        defmod.patch("f", lambda: val)
                    midmod = next(module_gen)
                    if "midmod" in patch_mods:
                        midmod.patch("f", lambda: val)
                    usemod = next(module_gen)
                    if "usemod" in patch_mods:
                        usemod.patch("x", lambda: val)
                    if isinstance(val, int):
                        self.assertEqual(usemod.g(), val)
                    else:
                        with self.assertRaises(TypeError):
                            usemod.g()

    def test_patch_strict_module_previously_nonexistent_attr(self):
        with self.in_strict_module("", enable_patching=True) as mod:
            mod.patch("f", lambda: 1)
            self.assertEqual(mod.f(), 1)

    def test_async_cached_property_patch_with_bad_type(self):
        codestr = """
        from cinder import async_cached_property

        class C:
            @async_cached_property
            async def x(self) -> int:
                return 3

        async def f(c: C) -> int:
            return await c.x
        """
        with self.in_strict_module(codestr, freeze=False) as mod:

            with self.assertRaisesRegex(
                TypeError, "Cannot assign a str, because C.x is expected to be a int"
            ):
                setattr(mod.C, "x", "42")

        # Ensure the exact same behavior in non-static code
        with self.in_module(codestr, code_gen=PythonCodeGenerator) as mod:
            setattr(mod.C, "x", "42")

            with self.assertRaisesRegex(
                TypeError, "object str can't be used in 'await' expression"
            ):
                asyncio.run(mod.f(mod.C()))

    def test_async_cached_property_patch_with_bad_return_type(self):
        codestr = """
        from cinder import async_cached_property

        class C:
            @async_cached_property
            async def x(self) -> int:
                return 3

        async def f(c: C) -> int:
            return await c.x
        """

        class TestAwaitableProperty:
            def __next__(self):
                raise StopIteration("zzz")

            def __await__(self):
                return self

            def __get__(self, _, __=None):
                return self

        with self.in_strict_module(codestr, freeze=False) as mod:
            setattr(mod.C, "x", TestAwaitableProperty())

            with self.assertRaisesRegex(
                TypeError,
                "unexpected return type from awaitable_wrapper.x, expected int, got str",
            ):
                asyncio.run(mod.f(mod.C()))

            async def awaiter(c):
                return await c.x

            # This works, because it goes through LOAD_ATTR, and non-static code isn't type-checked
            self.assertEqual(asyncio.run(awaiter(mod.C())), "zzz")

    def test_async_cached_property_patch_with_good_return_type(self):
        codestr = """
        from cinder import async_cached_property

        class C:
            @async_cached_property
            async def x(self) -> int:
                return 3

        async def f(c: C) -> int:
            return await c.x
        """

        class TestAwaitableProperty:
            def __next__(self):
                raise StopIteration(131)

            def __await__(self):
                return self

            def __get__(self, _, __=None):
                return self

        with self.in_strict_module(codestr, freeze=False) as mod:
            setattr(mod.C, "x", TestAwaitableProperty())

            self.assertEqual(asyncio.run(mod.f(mod.C())), 131)

            async def awaiter(c):
                return await c.x

            self.assertEqual(asyncio.run(awaiter(mod.C())), 131)

    def test_async_cached_property_patch_with_good_return_type_already_invoked(self):
        codestr = """
        from cinder import async_cached_property

        class C:
            @async_cached_property
            async def x(self) -> int:
                return 3

        async def f(c: C) -> int:
            return await c.x
        """

        class TestAwaitableProperty:
            def __next__(self):
                raise StopIteration(131)

            def __await__(self):
                return self

            def __get__(self, _, __=None):
                return self

        with self.in_strict_module(codestr, freeze=False) as mod:
            self.assertEqual(asyncio.run(mod.f(mod.C())), 3)

            setattr(mod.C, "x", TestAwaitableProperty())

            self.assertEqual(asyncio.run(mod.f(mod.C())), 131)

            async def awaiter(c):
                return await c.x

            self.assertEqual(asyncio.run(awaiter(mod.C())), 131)

    def test_thunk_traversal(self):
        codestr = """
            def f():
                return 42

            def g():
                return f()
        """
        with self.in_strict_module(codestr, enable_patching=True) as mod:
            g = mod.g
            self.assertEqual(g(), 42)

            # Causes a thunk to be created
            mod.patch("f", Mock(return_value=100))
            self.assertEqual(g(), 100)

            # This triggers a traversal of the thunk using its tp_traverse
            xxclassloader.traverse_heap()

    def test_patch_staticmethod(self):
        codestr = """
            from typing import final

            @final
            class C:
                @staticmethod
                def f(x: int):
                    return 42 + x

            def g():
                return C.f(3)
        """
        with self.in_strict_module(codestr, freeze=False, enable_patching=True) as mod:
            r = mod.g()
            self.assertEqual(r, 45)

            with patch(f"{mod.__name__}.C.f", return_value=100):
                r = mod.g()
                self.assertEqual(r, 100)

            r = mod.g()
            self.assertEqual(r, 45)

    def test_patch_classmethod_2(self):
        codestr = """
            from typing import final

            @final
            class C:
                @classmethod
                def f(cls, x: int):
                    return 42 + x

            def g():
                return C().f(3)
        """
        with self.in_strict_module(codestr, freeze=False, enable_patching=True) as mod:
            r = mod.g()
            self.assertEqual(r, 45)

            with patch(f"{mod.__name__}.C.f", return_value=100):
                r = mod.g()
                self.assertEqual(r, 100)

            r = mod.g()
            self.assertEqual(r, 45)

    def test_patch_parent_class(self):
        static_codestr = """
            from abc import ABCMeta
            from typing import final

            class C(metaclass=ABCMeta):
                @staticmethod
                def f(a: int):
                    return 42 + a

            class D(C, metaclass=ABCMeta):
                pass

            @final
            class E(D):
                def g(self) -> None:
                    pass

            def p():
                return E().g()
        """
        with self.in_strict_module(
            static_codestr, freeze=False, enable_patching=True
        ) as mod:

            class F(mod.D):
                pass

            # This should trigger creation of v-table for E, D & C, and F
            mod.p()

            # Try patching C's staticmethod. This will crash if F's v-table
            # isn't initialized.
            setattr(mod.C, "f", lambda x: 100)

    def test_set_code_raises_runtime_error(self):
        codestr = """
            def f():
                return 42

            def g():
                return f()
        """
        with self.in_module(codestr) as mod:
            with self.assertRaises(RuntimeError) as ctx:
                mod.f.__code__ = mod.g.__code__
            self.assertEqual(
                str(ctx.exception), "Cannot modify __code__ of Static Python function"
            )

    def test_patch_fn_with_primitive_args(self):
        codestr = """
        import __static__
        from __static__ import int64, cbool, box

        def fn(i: int64) -> cbool:
            return i + 1 == 2

        def call_fn():
            r = fn(int64(4))
        """

        with self.in_strict_module(codestr, freeze=False, enable_patching=True) as mod:

            def fn2(i: int):
                return i == 0

            mod.patch("fn", fn2)
            mod.call_fn()

    def test_patch_fn_with_optional_ret(self):
        codestr = """
        import __static__
        from __static__ import int64, cbool, box

        def fn(i: int) -> int | None:
            if i == 0:
                return None
            return i

        def call_fn():
            z = fn(42)
            if z is None:
                return True
            return False
        """

        with self.in_strict_module(codestr, freeze=False, enable_patching=True) as mod:

            def fn2(i: int) -> int | None:
                if i == 42:
                    return None
                return i

            mod.patch("fn", fn2)
            self.assertTrue(mod.call_fn())

    def test_patch_property_with_primitive_ret(self):
        codestr = """
        import __static__
        from __static__ import int64, cbool, box

        class C:
            @property
            def p(self) -> int64:
                return 2

            @property
            def q(self) -> int64:
                return 42

        def call_prop() -> int:
            c = C()
            r = c.p
            return box(r)
        """

        with self.in_strict_module(codestr, freeze=False, enable_patching=True) as mod:
            mod.C.p = mod.C.q

            self.assertEqual(mod.call_prop(), 42)
