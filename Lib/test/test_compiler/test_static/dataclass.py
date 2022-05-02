from dataclasses import FrozenInstanceError

from .common import StaticTestBase


class DataclassTests(StaticTestBase):
    def test_dataclasses_dataclass_is_dynamic(self) -> None:
        codestr = """
        from dataclasses import dataclass

        @dataclass
        class C:
            x: str

        reveal_type(C)
        """
        self.revealed_type(codestr, "Type[dynamic]")

    def test_static_dataclass_is_not_dynamic(self) -> None:
        for call in [True, False]:
            with self.subTest(call=call):
                insert = "(init=True)" if call else ""
                codestr = f"""
                from __static__ import dataclass

                @dataclass{insert}
                class C:
                    x: str
                """
                self.revealed_type(
                    codestr + "reveal_type(C)", "Type[Exact[<module>.C]]"
                )
                # check we don't call the decorator at runtime
                code = self.compile(codestr)
                self.assertNotInBytecode(code, "LOAD_NAME", "dataclass")

    def test_dataclass_bans_subclassing(self) -> None:
        codestr = """
        from __static__ import dataclass

        @dataclass
        class C:
            pass

        class D(C):
            pass
        """
        self.type_error(codestr, "Cannot subclass static dataclasses", at="class D")

    def test_dataclass_basic(self) -> None:
        codestr = """
        from __static__ import dataclass

        @dataclass
        class C:
            x: str
            y: int

        c = C("foo", 42)
        """
        with self.in_strict_module(codestr) as mod:
            self.assertEqual(mod.c.x, "foo")
            self.assertEqual(mod.c.y, 42)

    def test_dataclass_no_fields(self) -> None:
        codestr = """
        from __static__ import dataclass

        @dataclass
        class C:
            pass

        c = C()
        """
        with self.in_strict_module(codestr):
            pass

    def test_dataclass_too_few_args(self) -> None:
        codestr = """
        from __static__ import dataclass

        @dataclass
        class C:
            x: str
            y: int

        C("foo")
        """
        self.type_error(codestr, "expects a value for argument y", at='C("foo")')

    def test_dataclass_too_many_args(self) -> None:
        codestr = """
        from __static__ import dataclass

        @dataclass
        class C:
            x: str
            y: int

        C("foo", 42, "bar")
        """
        self.type_error(codestr, "Mismatched number of args", at='C("foo", 42, "bar")')

    def test_dataclass_incorrect_arg_type(self) -> None:
        codestr = """
        from __static__ import dataclass

        @dataclass
        class C:
            x: str
            y: int

        C("foo", "bar")
        """
        self.type_error(
            codestr, "str received for positional arg 'y', expected int", at='"bar"'
        )

    def test_default_value(self) -> None:
        codestr = """
        from __static__ import dataclass

        def generate_str() -> str:
            return "foo"

        @dataclass
        class C:
            x: str = generate_str()

        c = C()
        """
        with self.in_strict_module(codestr) as mod:
            self.assertEqual(mod.c.x, "foo")

    def test_default_replaced_by_value(self) -> None:
        codestr = """
        from __static__ import dataclass

        def generate_str() -> str:
            return "foo"

        @dataclass
        class C:
            x: str = generate_str()

        c = C("bar")
        """
        with self.in_strict_module(codestr) as mod:
            self.assertEqual(mod.C.x, "foo")
            self.assertEqual(mod.c.x, "bar")

    def test_nondefault_after_default(self) -> None:
        codestr = """
        from __static__ import dataclass

        @dataclass
        class C:
            x: int = 0
            y: str
        """
        self.type_error(
            codestr, "non-default argument y follows default argument", at="dataclass"
        )

    def test_dataclass_with_positional_arg(self) -> None:
        codestr = """
        from __static__ import dataclass

        @dataclass(1)
        class C:
            x: int
        """
        self.type_error(
            codestr, r"dataclass\(\) takes no positional arguments", at="dataclass"
        )

    def test_dataclass_with_non_constant(self) -> None:
        codestr = """
        from __static__ import dataclass

        def thunk() -> bool:
            return True

        @dataclass(init=thunk())
        class C:
            x: int
        """
        self.type_error(
            codestr,
            r"dataclass\(\) arguments must be boolean constants",
            at="dataclass",
        )

    def test_dataclass_with_non_bool(self) -> None:
        codestr = """
        from __static__ import dataclass

        @dataclass(init=1)
        class C:
            x: int
        """
        self.type_error(
            codestr,
            r"dataclass\(\) arguments must be boolean constants",
            at="dataclass",
        )

    def test_dataclass_with_unexpected_kwarg(self) -> None:
        codestr = """
        from __static__ import dataclass

        @dataclass(foo=True)
        class C:
            x: int
        """
        self.type_error(
            codestr,
            r"dataclass\(\) got an unexpected keyword argument 'foo'",
            at="dataclass",
        )

    def test_nondefault_after_default_with_init_false(self) -> None:
        codestr = """
        from __static__ import dataclass
        from typing import Optional

        @dataclass(init=False)
        class C:
            x: int = 0
            y: str

            def __init__(self, y: str) -> None:
                self.y = y

        c1 = C("foo")
        c2 = C("bar")
        """
        with self.in_strict_module(codestr) as mod:
            self.assertEqual(mod.c1.x, 0)
            self.assertEqual(mod.c1.y, "foo")

            self.assertEqual(mod.c2.x, 0)
            self.assertEqual(mod.c2.y, "bar")

    def test_field_named_self(self) -> None:
        codestr = """
        from __static__ import dataclass

        @dataclass
        class C:
            self: int

        c = C(1)
        """
        with self.in_strict_module(codestr) as mod:
            self.assertEqual(mod.c.self, 1)

    def test_post_init_checks_args(self) -> None:
        codestr = """
        from __static__ import dataclass

        @dataclass
        class C:
            x: int

            def __post_init__(self, y: int) -> None:
                self.x = 2
        """
        with self.in_strict_module(codestr) as mod:
            self.assertRaisesRegex(
                TypeError,
                r"__post_init__\(\) missing 1 required positional argument: 'y'",
                mod.C,
                1,
            )

    def test_post_init_called(self) -> None:
        codestr = """
        from __static__ import dataclass

        @dataclass
        class C:
            x: int

            def __post_init__(self) -> None:
                self.x = 2

        c = C(1)
        """
        with self.in_strict_module(codestr) as mod:
            self.assertEqual(mod.c.x, 2)

    def test_unannotated_not_a_field(self) -> None:
        codestr = """
        from __static__ import dataclass

        @dataclass
        class C:
            x = 0

            def foo(self) -> int:
                return self.x
        """
        with self.in_strict_module(codestr) as mod:
            self.assertNotInBytecode(mod.C.foo, "LOAD_FIELD")

    def test_dataclass_eq_static(self) -> None:
        codestr = """
        from __static__ import dataclass

        @dataclass
        class C:
            x: str
            y: int

        class D:
            def __init__(self, x: str, y: int) -> None:
                self.x = x
                self.y = y

        c1 = C("foo", 1)
        c2 = C("foo", 1)
        c3 = C("bar", 1)
        c4 = C("foo", 2)
        d = D("foo", 1)

        res = (
            c1 == c1,
            c1 == c2,
            c1 == c3,
            c1 == c4,
            c1 == d,
        )
        """
        with self.in_strict_module(codestr) as mod:
            self.assertTupleEqual(mod.res, (True, True, False, False, False))

    def test_dataclass_eq_nonstatic(self) -> None:
        codestr = """
        from __static__ import dataclass

        @dataclass
        class C:
            x: str
            y: int

        class D:
            def __init__(self, x: str, y: int) -> None:
                self.x = x
                self.y = y

        c1 = C("foo", 1)
        c2 = C("foo", 1)
        c3 = C("bar", 1)
        c4 = C("foo", 2)
        d = D("foo", 1)
        """
        with self.in_strict_module(codestr) as mod:
            self.assertEqual(mod.c1, mod.c1)
            self.assertEqual(mod.c1, mod.c2)
            self.assertNotEqual(mod.c1, mod.c3)
            self.assertNotEqual(mod.c1, mod.c4)
            self.assertNotEqual(mod.c1, mod.d)

    def test_dataclass_eq_does_not_overwrite(self) -> None:
        codestr = """
        from __static__ import dataclass

        @dataclass
        class C:
            x: int

            def __eq__(self, other):
                return True

        c1 = C(1)
        c2 = C(2)
        """
        with self.in_strict_module(codestr) as mod:
            self.assertEqual(mod.c1, mod.c2)

    def test_dataclass_eq_false_does_not_generate_dunder_eq(self) -> None:
        codestr = """
        from __static__ import dataclass

        @dataclass(eq=False)
        class C:
            x: int

        c1 = C(1)
        c2 = C(1)
        """
        with self.in_strict_module(codestr) as mod:
            self.assertEqual(mod.c1, mod.c1)
            self.assertNotEqual(mod.c1, mod.c2)
            self.assertEqual(mod.c2, mod.c2)

    def test_dataclass_eq_with_different_type_delegates_to_other(self) -> None:
        codestr = """
        from __static__ import dataclass

        @dataclass
        class C:
            x: int

        class EqualsEverything:
            def __eq__(self, other) -> bool:
                return True
        """
        with self.in_strict_module(codestr) as mod:
            self.assertEqual(mod.C(1), mod.EqualsEverything())

    def test_order_with_dunder_defined_raises_syntax_error(self) -> None:
        methods = ("__lt__", "__le__", "__gt__", "__ge__")
        for method in methods:
            with self.subTest(method=method):
                codestr = f"""
                from __static__ import dataclass

                @dataclass(order=True)
                class C:
                    x: int
                    y: str

                    def {method}(self, other) -> bool:
                        return False
                """
                self.type_error(
                    codestr,
                    f"Cannot overwrite attribute {method} in class C. "
                    "Consider using functools.total_ordering",
                    at="dataclass",
                )

    def test_comparison_subclass_returns_not_implemented(self) -> None:
        codestr = """
        from __static__ import dataclass

        @dataclass(order=True)
        class C:
            x: int
            y: str
        """
        with self.in_strict_module(codestr) as mod:

            class D(mod.C):
                pass

            c = mod.C(1, "foo")
            d = D(2, "bar")

            self.assertEqual(c.__eq__(d), NotImplemented)
            self.assertEqual(c.__ne__(d), NotImplemented)
            self.assertEqual(c.__lt__(d), NotImplemented)
            self.assertEqual(c.__le__(d), NotImplemented)
            self.assertEqual(c.__gt__(d), NotImplemented)
            self.assertEqual(c.__ge__(d), NotImplemented)

    def test_order_uses_tuple_order(self) -> None:
        codestr = """
        from __static__ import dataclass

        @dataclass(order=True)
        class C:
            x: str
            y: int

        c1 = C("foo", 1)
        c2 = C("foo", 1)
        c3 = C("bar", 1)
        c4 = C("foo", 2)
        """
        with self.in_strict_module(codestr) as mod:
            self.assertFalse(mod.c1 < mod.c2)
            self.assertTrue(mod.c1 <= mod.c2)
            self.assertFalse(mod.c1 > mod.c2)
            self.assertTrue(mod.c1 >= mod.c2)

            self.assertFalse(mod.c1 < mod.c3)
            self.assertFalse(mod.c1 <= mod.c3)
            self.assertTrue(mod.c1 > mod.c3)
            self.assertTrue(mod.c1 >= mod.c3)

            self.assertTrue(mod.c1 < mod.c4)
            self.assertTrue(mod.c1 <= mod.c4)
            self.assertFalse(mod.c1 > mod.c4)
            self.assertFalse(mod.c1 >= mod.c4)

    def test_assign_to_non_frozen_field(self) -> None:
        codestr = """
        from __static__ import dataclass

        @dataclass
        class C:
            x: str
            y: int

        c = C("foo", 1)
        """
        with self.in_strict_module(codestr) as mod:
            self.assertEqual(mod.c.x, "foo")
            self.assertEqual(mod.c.y, 1)

            mod.c.x = "bar"
            mod.c.y = 2
            self.assertEqual(mod.c.x, "bar")
            self.assertEqual(mod.c.y, 2)

            del mod.c.x
            del mod.c.y
            self.assertFalse(hasattr(mod.c, "x"))
            self.assertFalse(hasattr(mod.c, "y"))

    def test_frozen_with_dunder_defined_raises_syntax_error(self) -> None:
        for delete in (False, True):
            method = "__delattr__" if delete else "__setattr__"
            args = "self, name" if delete else "self, name, args"
            with self.subTest(method=method):
                codestr = f"""
                from __static__ import dataclass

                @dataclass(frozen=True)
                class C:
                    x: int
                    y: str

                    def {method}({args}) -> None:
                        return
                """
                self.type_error(
                    codestr,
                    f"Cannot overwrite attribute {method} in class C",
                    at="dataclass",
                )

    def test_assign_to_non_field_on_frozen_dataclass(self) -> None:
        codestr = """
        from __static__ import dataclass

        @dataclass(frozen=True)
        class C:
            x: str
            y: int

        c = C("foo", 1)
        """
        with self.in_strict_module(codestr) as mod:
            self.assertRaisesRegex(
                FrozenInstanceError,
                "cannot assign to field 'z'",
                mod.c.__setattr__,
                "z",
                "bar",
            )

    def test_cannot_assign_incorrect_type(self) -> None:
        codestr = """
        from __static__ import dataclass

        @dataclass
        class C:
            x: str

        c = C("foo")
        c.x = 1
        """
        self.type_error(
            codestr,
            r"type mismatch: Literal\[1\] cannot be assigned to str",
            at="c.x = 1",
        )

    def test_cannot_assign_incorrect_type_setattr(self) -> None:
        codestr = """
        from __static__ import dataclass

        @dataclass
        class C:
            x: str

        c = C("foo")
        """
        with self.in_strict_module(codestr) as mod:
            self.assertRaisesRegex(
                TypeError,
                "expected 'str', got 'int' for attribute 'x'",
                mod.c.__setattr__,
                "x",
                1,
            )

    def test_cannot_assign_to_frozen_dataclass_field(self) -> None:
        codestr = """
        from __static__ import dataclass

        @dataclass(frozen=True)
        class C:
            x: str

        c = C("foo")
        c.x = "bar"
        """
        self.type_error(
            codestr,
            "cannot assign to field 'x' of frozen dataclass '<module>.C'",
            at='c.x = "bar"',
        )

    def test_cannot_delete_frozen_dataclass_field(self) -> None:
        codestr = """
        from __static__ import dataclass

        @dataclass(frozen=True)
        class C:
            x: str

        c = C("foo")
        del c.x
        """
        self.type_error(
            codestr,
            "cannot delete field 'x' of frozen dataclass '<module>.C'",
            at="c.x",
        )

    def test_frozen_field_subclass(self) -> None:
        codestr = """
        from __static__ import dataclass

        @dataclass(frozen=True)
        class C:
            x: str
        """
        with self.in_strict_module(codestr) as mod:

            class D(mod.C):
                pass

            d = D("foo")
            self.assertRaisesRegex(
                FrozenInstanceError,
                "cannot assign to field 'x'",
                d.__setattr__,
                "x",
                "bar",
            )

            d.y = "bar"
            self.assertEqual(d.y, "bar")
            del d.y
            self.assertFalse(hasattr(d, "y"))

    def test_frozen_no_fields(self) -> None:
        codestr = """
        from __static__ import dataclass

        @dataclass(frozen=True)
        class C:
            pass
        """
        with self.in_strict_module(codestr) as mod:
            c = mod.C()
            self.assertRaisesRegex(
                FrozenInstanceError,
                "cannot assign to field 'x'",
                c.__setattr__,
                "x",
                "foo",
            )

            class D(mod.C):
                pass

            d = D()
            d.x = "foo"
            self.assertEqual(d.x, "foo")
            del d.x
            self.assertFalse(hasattr(d, "x"))

    def test_dunder_setattr_raises_frozen_instance_error(self) -> None:
        codestr = """
        from __static__ import dataclass

        @dataclass(frozen=True)
        class C:
            x: str

        c = C("foo")
        """
        with self.in_strict_module(codestr) as mod:
            self.assertRaisesRegex(
                FrozenInstanceError,
                "cannot assign to field 'x'",
                mod.c.__setattr__,
                "x",
                "bar",
            )

    def test_dunder_delattr_raises_frozen_instance_error(self) -> None:
        codestr = """
        from __static__ import dataclass

        @dataclass(frozen=True)
        class C:
            x: str

        c = C("foo")
        """
        with self.in_strict_module(codestr) as mod:
            self.assertRaisesRegex(
                FrozenInstanceError,
                "cannot delete field 'x'",
                mod.c.__delattr__,
                "x",
            )

    def test_set_frozen_fields_with_object_setattr(self) -> None:
        codestr = """
        from __static__ import dataclass

        @dataclass(frozen=True)
        class C:
            x: str

        c = C("foo")
        object.__setattr__(c, "x", "bar")
        """
        with self.in_strict_module(codestr) as mod:
            self.assertEqual(mod.c.x, "bar")

    def test_delete_frozen_fields_with_object_setattr(self) -> None:
        codestr = """
        from __static__ import dataclass

        @dataclass(frozen=True)
        class C:
            x: str

        c = C("foo")
        object.__delattr__(c, "x")
        """
        with self.in_strict_module(codestr) as mod:
            self.assertFalse(hasattr(mod.c, "x"))

    def test_repr_empty(self) -> None:
        codestr = """
        from __static__ import dataclass

        @dataclass
        class C:
            pass

        c = C()
        """
        with self.in_strict_module(codestr) as mod:
            self.assertEqual(repr(mod.c), "C()")

    def test_repr_one_field(self) -> None:
        codestr = """
        from __static__ import dataclass

        @dataclass
        class C:
            x: int

        c = C(1)
        """
        with self.in_strict_module(codestr) as mod:
            self.assertEqual(repr(mod.c), "C(x=1)")

    def test_repr_multiple_fields(self) -> None:
        codestr = """
        from __static__ import dataclass

        @dataclass
        class C:
            x: int
            y: str

        c = C(1, "foo")
        """
        with self.in_strict_module(codestr) as mod:
            self.assertEqual(repr(mod.c), "C(x=1, y='foo')")

    def test_repr_recursive(self) -> None:
        codestr = """
        from __static__ import dataclass
        from typing import Optional

        @dataclass
        class Node:
            val: int
            next: Optional["Node"] = None

        node = Node(1)
        node.next = node
        """
        with self.in_strict_module(codestr) as mod:
            self.assertEqual(repr(mod.node), "Node(val=1, next=...)")

    def test_repr_subclass(self) -> None:
        codestr = """
        from __static__ import dataclass

        @dataclass
        class C:
            x: int
            y: str
        """
        with self.in_strict_module(codestr) as mod:

            class D(mod.C):
                def __init__(self, x: int, y: str, z: int) -> None:
                    super().__init__(x, y)
                    self.z: int = z

            d = D(1, "foo", 2)
            self.assertRegex(repr(d), r"D\(x=1, y='foo'\)")

    def test_default_dataclass_has_none_hash(self) -> None:
        codestr = """
        from __static__ import dataclass

        @dataclass
        class C:
            x: str
            y: int
        """
        with self.in_strict_module(codestr) as mod:
            self.assertIn("__hash__", mod.C.__dict__)
            self.assertEqual(mod.C.__hash__, None)

    def test_explicit_hash(self) -> None:
        codestr = """
        from __static__ import dataclass

        @dataclass(frozen=True)
        class C:
            x: str
            y: int

            def __hash__(self) -> int:
                return 42

        c = C("foo", 2)
        """
        with self.in_strict_module(codestr) as mod:
            self.assertEqual(hash(mod.c), 42)

    def test_frozen_dataclass_generates_dunder_hash(self) -> None:
        codestr = """
        from __static__ import dataclass

        @dataclass(frozen=True)
        class C:
            x: str
            y: int

        c = C("foo", 2)
        """
        with self.in_strict_module(codestr) as mod:
            self.assertIn("__hash__", mod.C.__dict__)
            self.assertEqual(hash(mod.c), hash(("foo", 2)))

    def test_dataclass_hash_no_fields(self) -> None:
        codestr = """
        from __static__ import dataclass

        @dataclass(frozen=True)
        class C:
            pass

        c = C()
        """
        with self.in_strict_module(codestr) as mod:
            self.assertIn("__hash__", mod.C.__dict__)
            self.assertEqual(hash(mod.c), hash(()))

    def test_frozen_dataclass_without_eq_does_not_generate_hash(self) -> None:
        codestr = """
        from __static__ import dataclass

        @dataclass(eq=False, frozen=True)
        class C:
            x: str
            y: int
        """
        with self.in_strict_module(codestr) as mod:
            self.assertNotIn("__hash__", mod.C.__dict__)

    def test_unsafe_hash_cannot_overwrite_explicit_hash(self) -> None:
        codestr = """
        from __static__ import dataclass

        @dataclass(unsafe_hash=True)
        class C:
            x: str
            y: int

            def __hash__(self) -> int:
                return 42
        """
        self.type_error(
            codestr, "Cannot overwrite attribute __hash__ in class C", at="dataclass"
        )

    def test_unsafe_hash_cannot_overwrite_explicit_hash(self) -> None:
        codestr = """
        from __static__ import dataclass

        @dataclass(unsafe_hash=True)
        class C:
            x: str
            y: int

        c = C("foo", 2)
        """
        with self.in_strict_module(codestr) as mod:
            self.assertIn("__hash__", mod.C.__dict__)
            self.assertEqual(hash(mod.c), hash(("foo", 2)))

            mod.c.x = "bar"
            self.assertEqual(hash(mod.c), hash(("bar", 2)))

    def test_dataclass_order_requires_eq(self) -> None:
        codestr = """
        from __static__ import dataclass

        @dataclass(eq=False, order=True)
        class C:
            x: str
            y: int
        """
        self.type_error(codestr, "eq must be true if order is true")
