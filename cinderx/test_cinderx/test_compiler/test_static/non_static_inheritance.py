import unittest

from compiler.consts import CO_STATICALLY_COMPILED
from compiler.pycodegen import CinderCodeGenerator
from unittest import skip

from .common import StaticTestBase


class NonStaticInheritanceTests(StaticTestBase):
    def test_static_return_is_resolved_with_multiple_levels_of_inheritance(self):
        codestr = """
            class C:
                def foobar(self, x: int) -> int:
                    return x
                def f(self) -> int:
                    return self.foobar(1)
        """
        with self.in_strict_module(codestr, name="mymod", enable_patching=True) as mod:
            C = mod.C

            class D(C):
                def foobar(self, x: int) -> int:
                    return x + 1

            class E(D):
                def foobar(self, x: int) -> int:
                    return x + 2

            self.assertEqual(D().f(), 2)
            self.assertEqual(E().f(), 3)

    def test_multiple_inheritance_initialization(self):
        """Primarily testing that when we have multiple inheritance that
        we safely initialize all of our v-tables.  Previously we could
        init B2 while initializing the bases for DM, and then we wouldn't
        initialize the classes derived from it."""

        codestr = """
            class C:
                def foobar(self, x: int) -> int:
                    return x
                def f(self) -> int:
                    return self.foobar(1)
                def g(self): pass

            def f(x: C):
                return x.f()
        """
        with self.in_strict_module(
            codestr, name="mymod", enable_patching=True, freeze=False
        ) as mod:
            C = mod.C
            f = mod.f

            class B1(C):
                def f(self):
                    return 10

            class B2(C):
                def f(self):
                    return 20

            class D(B2):
                def f(self):
                    return 30

            class DM(B2, B1):
                pass

            # Force initialization of C down
            C.g = 42
            self.assertEqual(f(B1()), 10)
            self.assertEqual(f(B2()), 20)
            self.assertEqual(f(D()), 30)
            self.assertEqual(f(DM()), 20)

    def test_multiple_inheritance_initialization_invoke_only(self):
        """Primarily testing that when we have multiple inheritance that
        we safely initialize all of our v-tables.  Previously we could
        init B2 while initializing the bases for DM, and then we wouldn't
        initialize the classes derived from it."""

        codestr = """
            class C:
                def foobar(self, x: int) -> int:
                    return x
                def f(self) -> int:
                    return self.foobar(1)
                def g(self): pass

            def f(x: C):
                return x.f()
        """
        with self.in_strict_module(codestr, name="mymod", enable_patching=True) as mod:
            C = mod.C
            f = mod.f

            class B1(C):
                def f(self):
                    return 10

            class B2(C):
                def f(self):
                    return 20

            class D(B2):
                def f(self):
                    return 30

            class DM(B2, B1):
                pass

            # No forced initialization, only invokes
            self.assertEqual(f(C()), 1)
            self.assertEqual(f(B1()), 10)
            self.assertEqual(f(B2()), 20)
            self.assertEqual(f(D()), 30)
            self.assertEqual(f(DM()), 20)

    def test_inherit_abc(self):
        codestr = """
            from abc import ABC

            class C(ABC):
                @property
                def f(self) -> int:
                    return 42

                def g(self) -> int:
                    return self.f
        """
        with self.in_module(codestr) as mod:
            C = mod.C
            a = C()
            self.assertEqual(a.g(), 42)

    def test_static_decorator_non_static_class(self):
        codestr = """
            def mydec(f):
                def wrapper(*args, **kwargs):
                    return f(*args, **kwargs)
                return wrapper

            class B:
                def g(self): pass

            def f(x: B):
                return x.g()
        """
        with self.in_module(codestr) as mod:
            mydec = mod.mydec
            B = mod.B
            f = mod.f

            # force v-table initialization on base
            f(B())

            class D(B):
                @mydec
                def f(self):
                    pass

            self.assertEqual(D().f(), None)
            D.f = lambda self: 42
            self.assertEqual(f(B()), None)
            self.assertEqual(f(D()), None)
            self.assertEqual(D().f(), 42)

    def test_nonstatic_multiple_inheritance_invoke(self):
        """multiple inheritance from non-static classes should
        result in only static classes in the v-table"""

        codestr = """
        def f(x: str):
            return x.encode('utf8')
        """

        class C:
            pass

        class D(C, str):
            pass

        with self.in_module(codestr) as mod:
            self.assertEqual(mod.f(D("abc")), b"abc")

    def test_nonstatic_multiple_inheritance_invoke_static_base(self):
        codestr = """
        class B:
            def f(self):
                return 42

        def f(x: B):
            return x.f()
        """

        class C:
            def f(self):
                return "abc"

        with self.in_module(codestr) as mod:

            class D(C, mod.B):
                pass

            self.assertEqual(mod.f(D()), "abc")

    def test_nonstatic_multiple_inheritance_invoke_static_base_2(self):
        codestr = """
        class B:
            def f(self):
                return 42

        def f(x: B):
            return x.f()
        """

        class C:
            def f(self):
                return "abc"

        with self.in_module(codestr) as mod:

            class D(C, mod.B):
                def f(self):
                    return "foo"

            self.assertEqual(mod.f(D()), "foo")

    def test_no_inherit_multiple_static_bases(self):
        codestr = """
            class A:
                pass

            class B:
                pass
        """
        with self.in_module(codestr) as mod:
            with self.assertRaisesRegex(
                TypeError, r"multiple bases have instance lay-out conflict"
            ):

                class C(mod.A, mod.B):
                    pass

    def test_no_inherit_multiple_static_bases_indirect(self):
        codestr = """
            class A:
                pass

            class B:
                pass
        """
        with self.in_module(codestr) as mod:

            class C(mod.B):
                pass

            with self.assertRaisesRegex(
                TypeError, r"multiple bases have instance lay-out conflict"
            ):

                class D(C, mod.A):
                    pass

    def test_no_inherit_static_and_builtin(self):
        codestr = """
            class A:
                pass
        """
        with self.in_module(codestr) as mod:
            with self.assertRaisesRegex(
                TypeError, r"multiple bases have instance lay-out conflict"
            ):

                class C(mod.A, str):
                    pass

    def test_mutate_sub_sub_class(self):
        """patching non-static class through multiple levels
        of inheritance shouldn't crash"""

        codestr = """
        class B:
            def __init__(self): pass
            def f(self):
                return 42

        def f(b: B):
            return b.f()
        """
        with self.in_module(codestr) as mod:
            # force initialization of the class
            self.assertEqual(mod.f(mod.B()), 42)

            class D1(mod.B):
                def __init__(self):
                    pass

            class D2(D1):
                def __init__(self):
                    pass

            D1.__init__ = lambda self: None
            D2.__init__ = lambda self: None
            self.assertEqual(mod.f(D1()), 42)
            self.assertEqual(mod.f(D2()), 42)

    def test_invoke_class_method_dynamic_base(self):
        bases = """
        class B1: pass
        """
        codestr = """
        from bases import B1
        class D(B1):
            @classmethod
            def f(cls):
                return cls.g()

            @classmethod
            def g(cls):
                return 42

        def f():
            return D.f()
        """

        with self.in_module(
            bases, name="bases", code_gen=CinderCodeGenerator
        ), self.in_module(codestr) as mod:
            f = mod.f
            self.assertEqual(f(), 42)

    def test_no_inherit_static_through_nonstatic(self):
        base = """
            class A:
                pass
        """
        nonstatic = """
            from base import A

            class B(A):
                pass
        """
        static = """
            from nonstatic import B

            class C(B):
                pass
        """
        with self.in_module(base, name="base"), self.in_module(
            nonstatic, name="nonstatic", code_gen=CinderCodeGenerator
        ):
            with self.assertRaisesRegex(
                TypeError,
                r"Static compiler cannot verify that static type 'C' is a "
                r"valid override of static base 'A' because intervening base "
                r"'B' is non-static",
            ):
                self.run_code(static)

    def test_nonstatic_derived_method_in_static_class(self):
        # Having the decorator live in a non-static module ensures that the generated function
        # doesn't have the CO_STATICALLY_COMPILED flag.
        nonstatic = """
            def decorate(f):
                def foo(*args, **kwargs):
                    return f(*args, **kwargs)
                return foo
        """
        static = """
            from nonstatic import decorate
            class C:
               def f(self):
                   return 1

            class D(C):
               @decorate
               def f(self):
                   return 2

            def invoke_f(c: C):
                return c.f()

            def invoke_d_f():
                d = D()
                return d.f()
        """
        with self.in_module(
            nonstatic, name="nonstatic", code_gen=CinderCodeGenerator
        ), self.in_module(static) as mod:
            # Ensure that the decorator results in a non-static function.
            self.assertEqual(mod.D.f.__code__.co_flags & CO_STATICALLY_COMPILED, 0)
            self.assertEqual(mod.invoke_f(mod.C()), 1)
            self.assertEqual(mod.invoke_f(mod.D()), 2)
            # TODO(T131831297): The return value here should be 2 instead of 1.
            self.assertEqual(mod.invoke_d_f(), 1)
            # TODO(T131831297): This should be an INVOKE_METHOD.
            self.assertInBytecode(
                mod.invoke_d_f, "INVOKE_FUNCTION", ((mod.__name__, "C", "f"), 1)
            )

    def test_nonstatic_override_init_subclass(self):
        nonstatic = """
            from static import B

            class B2(B):
                def __init_subclass__(self):
                    # don't call super
                    pass

            class D(B2):
                x = 100
                def __init__(self):
                    pass

        """
        static = """

            class B:
                x: int = 42
                def get_x(self):
                    return self.x
                def set_x(self, value):
                    self.x = value

        """
        with self.in_module(static, name="static") as mod, self.in_module(
            nonstatic, name="nonstatic", code_gen=CinderCodeGenerator
        ) as nonstatic_mod:
            self.assertInBytecode(mod.B.get_x, "INVOKE_METHOD")
            self.assertInBytecode(mod.B.set_x, "INVOKE_METHOD")
            d = nonstatic_mod.D()
            self.assertRaises(TypeError, d.set_x, 100)
            self.assertEqual(d.get_x(), 100)

    def test_nonstatic_override_init_subclass_inst(self):
        nonstatic = """
            from static import B

            class B2(B):
                def __init_subclass__(self):
                    # don't call super
                    pass

            class D(B2):
                def __init__(self):
                    self.x = 100

        """
        static = """
            class B:
                x: int = 42
                def get_x(self):
                    return self.x
                def set_x(self, value):
                    self.x = value

        """
        with self.in_module(static, name="static") as mod, self.in_module(
            nonstatic, name="nonstatic", code_gen=CinderCodeGenerator
        ) as nonstatic_mod:
            self.assertInBytecode(mod.B.get_x, "INVOKE_METHOD")
            self.assertInBytecode(mod.B.set_x, "INVOKE_METHOD")
            d = nonstatic_mod.D()
            d.set_x(200)
            self.assertEqual(d.get_x(), 200)
            self.assertEqual(d.__dict__, {})
            self.assertEqual(mod.B.x, 42)

    def test_nonstatic_call_base_init(self):
        nonstatic = """
            class B:
                def __init_subclass__(cls):
                    cls.foo = 42

        """
        static = """
            from nonstatic import B
            class D(B):
                pass

        """
        with self.in_module(
            nonstatic, name="nonstatic", code_gen=CinderCodeGenerator
        ) as nonstatic_mod, self.in_module(static) as mod:
            self.assertEqual(mod.D.foo, 42)

    def test_nonstatic_call_base_init_other_super(self):
        nonstatic = """
            class B:
                def __init_subclass__(cls):
                    cls.foo = 42

        """
        static = """
            from nonstatic import B
            class D(B):
                def __init__(self):
                    return super().__init__()


        """
        with self.in_module(
            nonstatic, name="nonstatic", code_gen=CinderCodeGenerator
        ) as nonstatic_mod, self.in_module(static) as mod:
            self.assertEqual(mod.D.foo, 42)


if __name__ == "__main__":

    unittest.main()
