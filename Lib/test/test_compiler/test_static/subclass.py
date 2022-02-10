import unittest
from compiler.static.type_env import TypeEnvironment
from compiler.static.types import (
    BUILTIN_TYPES,
)

from .common import StaticTestBase


class SubclassTests(StaticTestBase):
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
        self.assertTrue(BUILTIN_TYPES.int.is_subclass_of(BUILTIN_TYPES.int))
        self.assertFalse(BUILTIN_TYPES.int.is_subclass_of(BUILTIN_TYPES.bool))
        self.assertFalse(BUILTIN_TYPES.int.is_subclass_of(BUILTIN_TYPES.str))

        self.assertTrue(BUILTIN_TYPES.bool.is_subclass_of(BUILTIN_TYPES.int))
        self.assertTrue(BUILTIN_TYPES.bool.is_subclass_of(BUILTIN_TYPES.bool))
        self.assertFalse(BUILTIN_TYPES.bool.is_subclass_of(BUILTIN_TYPES.str))

        self.assertFalse(BUILTIN_TYPES.str.is_subclass_of(BUILTIN_TYPES.int))
        self.assertFalse(BUILTIN_TYPES.str.is_subclass_of(BUILTIN_TYPES.bool))
        self.assertTrue(BUILTIN_TYPES.str.is_subclass_of(BUILTIN_TYPES.str))

    def test_issubclass_with_awaitable_covariant(self):
        mod, comp = self.bind_module("class Num(int): pass", 0)
        num = comp.modules["foo"].children["Num"]
        awaitable_bool = comp.type_env.get_generic_type(
            BUILTIN_TYPES.awaitable,
            (BUILTIN_TYPES.bool,),
        )
        awaitable_int = comp.type_env.get_generic_type(
            BUILTIN_TYPES.awaitable,
            (BUILTIN_TYPES.int,),
        )
        awaitable_num = comp.type_env.get_generic_type(BUILTIN_TYPES.awaitable, (num,))

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
        int_or_str = TypeEnvironment().get_generic_type(
            BUILTIN_TYPES.union, (BUILTIN_TYPES.int, BUILTIN_TYPES.str)
        )
        self.assertFalse(int_or_str.is_subclass_of(BUILTIN_TYPES.int))
        self.assertFalse(int_or_str.is_subclass_of(BUILTIN_TYPES.str))
        self.assertTrue(BUILTIN_TYPES.int.is_subclass_of(int_or_str))
        self.assertTrue(BUILTIN_TYPES.str.is_subclass_of(int_or_str))

    def test_issubclass_with_union_self_and_source(self):
        type_env = TypeEnvironment()
        int_or_str = type_env.get_generic_type(
            BUILTIN_TYPES.union,
            (BUILTIN_TYPES.int, BUILTIN_TYPES.str),
        )
        str_or_tuple = type_env.get_generic_type(
            BUILTIN_TYPES.union,
            (BUILTIN_TYPES.str, BUILTIN_TYPES.tuple),
        )
        int_or_str_or_tuple = type_env.get_generic_type(
            BUILTIN_TYPES.union,
            (BUILTIN_TYPES.int, BUILTIN_TYPES.str, BUILTIN_TYPES.tuple),
        )
        self.assertFalse(int_or_str_or_tuple.is_subclass_of(int_or_str))
        self.assertFalse(int_or_str_or_tuple.is_subclass_of(str_or_tuple))
        self.assertFalse(int_or_str.is_subclass_of(str_or_tuple))
        self.assertTrue(int_or_str.is_subclass_of(int_or_str_or_tuple))
        self.assertFalse(str_or_tuple.is_subclass_of(int_or_str))
        self.assertTrue(str_or_tuple.is_subclass_of(int_or_str_or_tuple))

    def test_union_with_subclass_returns_superclass(self):
        bool_or_int = TypeEnvironment().get_generic_type(
            BUILTIN_TYPES.union,
            (BUILTIN_TYPES.bool, BUILTIN_TYPES.int),
        )
        self.assertIs(bool_or_int, BUILTIN_TYPES.int)

    def test_checkedlist_subclass(self):
        checked_list_str = TypeEnvironment().get_generic_type(
            BUILTIN_TYPES.checked_list,
            (BUILTIN_TYPES.str,),
        )
        self.assertTrue(checked_list_str.is_subclass_of(BUILTIN_TYPES.object))

    def test_cannot_subclass_static_classes_in_nonstatic_code(self):
        from __static__ import int8, Array, Vector

        with self.assertRaisesRegex(
            TypeError, "type 'int8' is not an acceptable base type"
        ):

            class D(int8):
                pass

        with self.assertRaisesRegex(
            TypeError, "type 'Vector' is not an acceptable base type"
        ):

            class D(Vector):
                pass

        with self.assertRaisesRegex(
            TypeError, "type 'Array' is not an acceptable base type"
        ):

            class D(Array):
                pass


if __name__ == "__main__":
    unittest.main()
