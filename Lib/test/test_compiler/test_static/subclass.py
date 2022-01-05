import unittest
from compiler.static.types import (
    BOOL_TYPE,
    CHECKED_LIST_TYPE,
    INT_TYPE,
    OBJECT_TYPE,
    STR_TYPE,
    UNION_TYPE,
    TUPLE_TYPE,
    AWAITABLE_TYPE,
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
        self.assertTrue(INT_TYPE.is_subclass_of(INT_TYPE))
        self.assertFalse(INT_TYPE.is_subclass_of(BOOL_TYPE))
        self.assertFalse(INT_TYPE.is_subclass_of(STR_TYPE))

        self.assertTrue(BOOL_TYPE.is_subclass_of(INT_TYPE))
        self.assertTrue(BOOL_TYPE.is_subclass_of(BOOL_TYPE))
        self.assertFalse(BOOL_TYPE.is_subclass_of(STR_TYPE))

        self.assertFalse(STR_TYPE.is_subclass_of(INT_TYPE))
        self.assertFalse(STR_TYPE.is_subclass_of(BOOL_TYPE))
        self.assertTrue(STR_TYPE.is_subclass_of(STR_TYPE))

    def test_issubclass_with_awaitable_covariant(self):
        mod, comp = self.bind_module("class Num(int): pass", 0)
        num = comp.modules["foo"].children["Num"]
        awaitable_bool = AWAITABLE_TYPE.make_generic_type(
            (BOOL_TYPE,), comp.generic_types
        )
        awaitable_int = AWAITABLE_TYPE.make_generic_type(
            (INT_TYPE,), comp.generic_types
        )
        awaitable_num = AWAITABLE_TYPE.make_generic_type((num,), comp.generic_types)

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
        int_or_str = UNION_TYPE.make_generic_type((INT_TYPE, STR_TYPE), {})
        self.assertFalse(int_or_str.is_subclass_of(INT_TYPE))
        self.assertFalse(int_or_str.is_subclass_of(STR_TYPE))
        self.assertTrue(INT_TYPE.is_subclass_of(int_or_str))
        self.assertTrue(STR_TYPE.is_subclass_of(int_or_str))

    def test_issubclass_with_union_self_and_source(self):
        generic_types = {}
        int_or_str = UNION_TYPE.make_generic_type((INT_TYPE, STR_TYPE), generic_types)
        str_or_tuple = UNION_TYPE.make_generic_type(
            (STR_TYPE, TUPLE_TYPE), generic_types
        )
        int_or_str_or_tuple = UNION_TYPE.make_generic_type(
            (INT_TYPE, STR_TYPE, TUPLE_TYPE), generic_types
        )
        self.assertFalse(int_or_str_or_tuple.is_subclass_of(int_or_str))
        self.assertFalse(int_or_str_or_tuple.is_subclass_of(str_or_tuple))
        self.assertFalse(int_or_str.is_subclass_of(str_or_tuple))
        self.assertTrue(int_or_str.is_subclass_of(int_or_str_or_tuple))
        self.assertFalse(str_or_tuple.is_subclass_of(int_or_str))
        self.assertTrue(str_or_tuple.is_subclass_of(int_or_str_or_tuple))

    def test_union_with_subclass_returns_superclass(self):
        bool_or_int = UNION_TYPE.make_generic_type((BOOL_TYPE, INT_TYPE), {})
        self.assertIs(bool_or_int, INT_TYPE)

    def test_checkedlist_subclass(self):
        checked_list_str = CHECKED_LIST_TYPE.make_generic_type((STR_TYPE,), {})
        self.assertTrue(checked_list_str.is_subclass_of(OBJECT_TYPE))

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
