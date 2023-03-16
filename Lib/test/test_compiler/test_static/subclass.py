import unittest
from compiler.static.types import TypeEnvironment

from unittest import skip

from .common import StaticTestBase


class SubclassTests(StaticTestBase):
    type_env: TypeEnvironment = TypeEnvironment()

    def test_union_isinstance_tuple(self):
        self.assertReturns(
            """
            class A: pass
            class B: pass
            class C: pass

            def f(x: A, y: B, z: C):
                o = x or y or z
                if isinstance(o, (A, B)):
                    return 1
                return o
            """,
            "foo.C",
        )

    def test_issubclass_builtin_types(self):
        self.assertTrue(self.type_env.int.is_subclass_of(self.type_env.int))
        self.assertFalse(self.type_env.int.is_subclass_of(self.type_env.bool))
        self.assertFalse(self.type_env.int.is_subclass_of(self.type_env.str))

        self.assertTrue(self.type_env.bool.is_subclass_of(self.type_env.int))
        self.assertTrue(self.type_env.bool.is_subclass_of(self.type_env.bool))
        self.assertFalse(self.type_env.bool.is_subclass_of(self.type_env.str))

        self.assertFalse(self.type_env.str.is_subclass_of(self.type_env.int))
        self.assertFalse(self.type_env.str.is_subclass_of(self.type_env.bool))
        self.assertTrue(self.type_env.str.is_subclass_of(self.type_env.str))

    def test_issubclass_with_awaitable_covariant(self):
        mod, comp = self.bind_module("class Num(int): pass", 0)
        num = comp.modules["foo"].get_child("Num")
        awaitable_bool = comp.type_env.get_generic_type(
            comp.type_env.awaitable,
            (comp.type_env.bool,),
        )
        awaitable_int = comp.type_env.get_generic_type(
            comp.type_env.awaitable,
            (comp.type_env.int,),
        )
        awaitable_num = comp.type_env.get_generic_type(comp.type_env.awaitable, (num,))

        self.assertTrue(awaitable_bool.is_subclass_of(awaitable_bool))
        self.assertTrue(awaitable_bool.is_subclass_of(awaitable_int))
        self.assertFalse(awaitable_bool.is_subclass_of(awaitable_num))

        self.assertFalse(awaitable_int.is_subclass_of(awaitable_bool))
        self.assertTrue(awaitable_int.is_subclass_of(awaitable_int))
        self.assertFalse(awaitable_int.is_subclass_of(awaitable_num))

        self.assertFalse(awaitable_num.is_subclass_of(awaitable_bool))
        self.assertTrue(awaitable_num.is_subclass_of(awaitable_int))
        self.assertTrue(awaitable_num.is_subclass_of(awaitable_num))

    def test_issubclass_with_union_self(self):
        int_or_str = self.type_env.get_generic_type(
            self.type_env.union, (self.type_env.int, self.type_env.str)
        )
        self.assertFalse(int_or_str.is_subclass_of(self.type_env.int))
        self.assertFalse(int_or_str.is_subclass_of(self.type_env.str))
        self.assertTrue(self.type_env.int.is_subclass_of(int_or_str))
        self.assertTrue(self.type_env.str.is_subclass_of(int_or_str))

    def test_issubclass_with_union_self_and_source(self):
        type_env = TypeEnvironment()
        int_or_str = type_env.get_generic_type(
            self.type_env.union,
            (self.type_env.int, self.type_env.str),
        )
        str_or_tuple = type_env.get_generic_type(
            self.type_env.union,
            (self.type_env.str, self.type_env.tuple),
        )
        int_or_str_or_tuple = type_env.get_generic_type(
            self.type_env.union,
            (self.type_env.int, self.type_env.str, self.type_env.tuple),
        )
        self.assertFalse(int_or_str_or_tuple.is_subclass_of(int_or_str))
        self.assertFalse(int_or_str_or_tuple.is_subclass_of(str_or_tuple))
        self.assertFalse(int_or_str.is_subclass_of(str_or_tuple))
        self.assertTrue(int_or_str.is_subclass_of(int_or_str_or_tuple))
        self.assertFalse(str_or_tuple.is_subclass_of(int_or_str))
        self.assertTrue(str_or_tuple.is_subclass_of(int_or_str_or_tuple))

    def test_union_with_subclass_returns_superclass(self):
        bool_or_int = TypeEnvironment().get_generic_type(
            self.type_env.union,
            (self.type_env.bool, self.type_env.int),
        )
        self.assertIs(bool_or_int, self.type_env.int)

    def test_checkedlist_subclass(self):
        checked_list_str = self.type_env.get_generic_type(
            self.type_env.checked_list,
            (self.type_env.str,),
        )
        self.assertTrue(checked_list_str.is_subclass_of(self.type_env.object))

    def test_cannot_subclass_static_classes_in_nonstatic_code(self):
        from __static__ import int8

        with self.assertRaisesRegex(
            TypeError, "type 'int8' is not an acceptable base type"
        ):

            class D(int8):
                pass

    def test_init_subclass(self):
        codestr = """
            class B:
                def __init_subclass__(cls):
                    pass
        """
        with self.in_module(codestr) as mod:

            class D(mod.B):
                pass

    def test_base_class_inited(self):
        codestr = """
            class B:
                def f(self) -> int:
                    return 42

            def f(b: B) -> int:
                return b.f()
        """
        with self.in_module(codestr, name="base") as mod:
            b = mod.B()
            self.assertEqual(mod.f(b), 42)
            codestr = """
                from __static__ import int8
                from base import B
                class D(B):
                    def __init__(self, a: int8, b: int8) -> None:
                        self.a: int8 = a
                        self.b: int8 = b
            """
            with self.in_module(codestr) as derived:
                pass


if __name__ == "__main__":
    unittest.main()
