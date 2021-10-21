import asyncio
import re
from compiler.pycodegen import PythonCodeGenerator
from unittest.mock import Mock, patch

from .common import StaticTestBase


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
