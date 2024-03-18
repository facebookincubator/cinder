from dataclasses import (
    _DataclassParams,
    _FIELD,
    _FIELD_CLASSVAR,
    _FIELD_INITVAR,
    Field,
    FrozenInstanceError,
    is_dataclass,
    MISSING,
)
from typing import Mapping

from cinderx.compiler.consts import CO_STATICALLY_COMPILED
from cinderx.compiler.pycodegen import PythonCodeGenerator

from .common import StaticTestBase


class DataclassTests(StaticTestBase):
    def test_static_dataclass_is_not_dynamic(self) -> None:
        for call in [True, False]:
            with self.subTest(call=call):
                insert = "(init=True)" if call else ""
                codestr = f"""
                from dataclasses import dataclass

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

    def test_dataclass_basic(self) -> None:
        codestr = """
        from dataclasses import dataclass

        @dataclass
        class C:
            x: str
            y: int

        c = C("foo", 42)
        """
        with self.in_strict_module(codestr) as mod:
            self.assertEqual(mod.c.x, "foo")
            self.assertEqual(mod.c.y, 42)

    def test_dataclass_init_by_name(self) -> None:
        codestr = """
        from dataclasses import dataclass

        @dataclass
        class C:
            x: str
            y: int

        c = C(y=42, x="foo")
        """
        with self.in_strict_module(codestr) as mod:
            self.assertEqual(mod.c.x, "foo")
            self.assertEqual(mod.c.y, 42)

    def test_dataclass_no_fields(self) -> None:
        codestr = """
        from dataclasses import dataclass

        @dataclass
        class C:
            pass

        c = C()
        """
        with self.in_strict_module(codestr):
            pass

    def test_dataclass_too_few_args(self) -> None:
        codestr = """
        from dataclasses import dataclass

        @dataclass
        class C:
            x: str
            y: int

        C("foo")
        """
        self.type_error(codestr, "expects a value for argument y", at='C("foo")')

    def test_dataclass_too_many_args(self) -> None:
        codestr = """
        from dataclasses import dataclass

        @dataclass
        class C:
            x: str
            y: int

        C("foo", 42, "bar")
        """
        self.type_error(codestr, "Mismatched number of args", at='C("foo", 42, "bar")')

    def test_dataclass_incorrect_arg_type(self) -> None:
        codestr = """
        from dataclasses import dataclass

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
        from dataclasses import dataclass

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
        from dataclasses import dataclass

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
        from dataclasses import dataclass

        @dataclass
        class C:
            x: int = 0
            y: str
        """
        self.type_error(
            codestr, "non-default argument 'y' follows default argument", at="class C:"
        )

    def test_dataclass_with_positional_arg(self) -> None:
        codestr = """
        from dataclasses import dataclass

        @dataclass(1)
        class C:
            x: int
        """
        self.type_error(
            codestr, r"dataclass\(\) takes no positional arguments", at="dataclass"
        )

    def test_dataclass_with_non_constant(self) -> None:
        codestr = """
        from dataclasses import dataclass

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
        from dataclasses import dataclass

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
        from dataclasses import dataclass

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
        from dataclasses import dataclass
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
        from dataclasses import dataclass

        @dataclass
        class C:
            self: int

        c = C(1)
        """
        with self.in_strict_module(codestr) as mod:
            self.assertEqual(mod.c.self, 1)

    def test_post_init_checks_args(self) -> None:
        codestr = """
        from dataclasses import dataclass

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
        from dataclasses import dataclass

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
        from dataclasses import dataclass

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
        from dataclasses import dataclass

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
        from dataclasses import dataclass

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
        from dataclasses import dataclass

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
        from dataclasses import dataclass

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
        from dataclasses import dataclass

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
                from dataclasses import dataclass

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
        from dataclasses import dataclass

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
        from dataclasses import dataclass

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
        from dataclasses import dataclass

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
                from dataclasses import dataclass

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
        from dataclasses import dataclass

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

    def test_cannot_assign_incorrect_type_toplevel(self) -> None:
        codestr = """
        from dataclasses import dataclass

        @dataclass
        class C:
            x: str

        c = C("foo")
        c.x = 1
        """
        with self.assertRaisesRegex(
            TypeError, "expected 'str', got 'int' for attribute 'x'"
        ):
            with self.in_module(codestr):
                pass

    def test_cannot_assign_incorrect_type_static_error(self) -> None:
        codestr = """
        from dataclasses import dataclass

        @dataclass
        class C:
            x: str

        def func():
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
        from dataclasses import dataclass

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

    def test_cannot_assign_to_frozen_dataclass_field_toplevel(self) -> None:
        codestr = """
        from dataclasses import dataclass

        @dataclass(frozen=True)
        class C:
            x: str

        c = C("foo")
        c.x = "bar"
        """
        with self.assertRaisesRegex(FrozenInstanceError, "cannot assign to field 'x'"):
            with self.in_module(codestr):
                pass

    def test_cannot_assign_to_frozen_dataclass_field_static(self) -> None:
        codestr = """
        from dataclasses import dataclass

        @dataclass(frozen=True)
        class C:
            x: str

        def f():
            c = C("foo")
            c.x = "bar"
        """
        self.type_error(
            codestr,
            "cannot assign to field 'x' of frozen dataclass '<module>.C'",
            at='c.x = "bar"',
        )

    def test_cannot_delete_frozen_dataclass_field_static_error(self) -> None:
        codestr = """
        from dataclasses import dataclass

        @dataclass(frozen=True)
        class C:
            x: str

        def func():
            c = C("foo")
            del c.x
        """
        self.type_error(
            codestr,
            "cannot delete field 'x' of frozen dataclass '<module>.C'",
            at="c.x",
        )

    def test_cannot_delete_frozen_dataclass_field_toplevel_dynamic_error(self) -> None:
        codestr = """
        from dataclasses import dataclass

        @dataclass(frozen=True)
        class C:
            x: str

        c = C("foo")
        del c.x
        """
        with self.assertRaisesRegex(FrozenInstanceError, "cannot delete field 'x'"):
            with self.in_module(codestr):
                pass

    def test_frozen_field_subclass(self) -> None:
        codestr = """
        from dataclasses import dataclass

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
        from dataclasses import dataclass

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
        from dataclasses import dataclass

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
        from dataclasses import dataclass

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
        from dataclasses import dataclass

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
        from dataclasses import dataclass

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
        from dataclasses import dataclass

        @dataclass
        class C:
            pass

        c = C()
        """
        with self.in_strict_module(codestr) as mod:
            self.assertEqual(repr(mod.c), "C()")

    def test_repr_one_field(self) -> None:
        codestr = """
        from dataclasses import dataclass

        @dataclass
        class C:
            x: int

        c = C(1)
        """
        with self.in_strict_module(codestr) as mod:
            self.assertEqual(repr(mod.c), "C(x=1)")

    def test_repr_multiple_fields(self) -> None:
        codestr = """
        from dataclasses import dataclass

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
        from dataclasses import dataclass
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
        from dataclasses import dataclass

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
        from dataclasses import dataclass

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
        from dataclasses import dataclass

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
        from dataclasses import dataclass

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
        from dataclasses import dataclass

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
        from dataclasses import dataclass

        @dataclass(eq=False, frozen=True)
        class C:
            x: str
            y: int
        """
        with self.in_strict_module(codestr) as mod:
            self.assertNotIn("__hash__", mod.C.__dict__)

    def test_unsafe_hash_cannot_overwrite_explicit_hash(self) -> None:
        codestr = """
        from dataclasses import dataclass

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

    def test_unsafe_hash_uses_tuple_hash(self) -> None:
        codestr = """
        from dataclasses import dataclass

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
        from dataclasses import dataclass

        @dataclass(eq=False, order=True)
        class C:
            x: str
            y: int
        """
        self.type_error(codestr, "eq must be true if order is true")

    def test_dataclass_has_params_attribute(self) -> None:
        codestr = """
        from dataclasses import dataclass

        @dataclass
        class C:
            x: str
            y: int
        """
        with self.in_strict_module(codestr) as mod:
            params = mod.C.__dataclass_params__
            self.assertIsInstance(params, _DataclassParams)
            self.assertTrue(params.init)
            self.assertTrue(params.repr)
            self.assertTrue(params.eq)
            self.assertFalse(params.order)
            self.assertFalse(params.unsafe_hash)
            self.assertFalse(params.frozen)

    def test_dataclass_subclass_dynamic_is_dynamic(self) -> None:
        codestr = """
        from dataclasses import dataclass

        @dataclass
        class C:
            x: str
        """
        with self.in_module(codestr, code_gen=PythonCodeGenerator) as nonstatic_mod:
            codestr = f"""
            from dataclasses import dataclass
            from {nonstatic_mod.__name__} import C

            @dataclass
            class D(C):
                y: int
            """
            self.revealed_type(codestr + "reveal_type(D)", "Type[dynamic]")
            with self.in_module(codestr) as mod:
                d = mod.D("foo", 2)
                self.assertEqual(d.x, "foo")
                self.assertEqual(d.y, 2)

    def test_dataclass_field_in_non_dataclass_fails_type_check(self) -> None:
        codestr = """
        from dataclasses import field

        class C:
            x: str = field(default="foo")
        """
        self.type_error(
            codestr,
            "type mismatch: dataclasses.Field cannot be assigned to str",
            at="field",
        )

    def test_dataclass_field_in_dynamic_dataclass_fails_type_check(self) -> None:
        codestr = """
        class C:
            pass
        """
        with self.in_module(codestr, code_gen=PythonCodeGenerator) as nonstatic_mod:
            codestr = f"""
            from dataclasses import dataclass, field
            from {nonstatic_mod.__name__} import C

            @dataclass
            class D(C):
                x: int = field(hash=False)
            """
            self.type_error(
                codestr,
                "type mismatch: dataclasses.Field cannot be assigned to int",
                at="field(hash=False)",
            )

    def test_dataclass_field_takes_no_positional_arguments(self) -> None:
        codestr = """
        from dataclasses import dataclass, field

        @dataclass
        class C:
            x: str = field("foo")
        """
        self.type_error(
            codestr,
            r"dataclasses.field\(\) takes no positional arguments",
            at="class C:",
        )

    def test_dataclass_field_checks_flags_for_bool_constant(self) -> None:
        codestr = """
        from dataclasses import dataclass, field

        @dataclass
        class C:
            x: str = field(init=1)
        """
        self.type_error(
            codestr,
            r"dataclasses.field\(\) argument 'init' must be a boolean constant",
            at="class C:",
        )

    def test_dataclass_field_checks_hash_for_bool_constant_or_none(self) -> None:
        codestr = """
        from dataclasses import dataclass, field

        @dataclass
        class C:
            x: str = field(hash=1)
        """
        self.type_error(
            codestr,
            r"dataclasses.field\(\) argument 'hash' must be None or a boolean constant",
            at="class C:",
        )

    def test_dataclass_field_unexpected_keyword(self) -> None:
        codestr = """
        from dataclasses import dataclass, field

        @dataclass
        class C:
            x: str = field(foo=1)
        """
        self.type_error(
            codestr,
            r"dataclasses.field\(\) got an unexpected keyword argument 'foo'",
            at="class C:",
        )

    def test_dataclass_field_without_default(self) -> None:
        codestr = """
        from dataclasses import dataclass, field

        @dataclass
        class C:
            x: str = field(metadata={"foo": "bar"})

        c = C("hello")
        """
        with self.in_module(codestr) as mod:
            self.assertEqual(mod.c.x, "hello")
            self.assertRaisesRegex(
                TypeError,
                r"__init__\(\) missing 1 required positional argument: 'x'",
                mod.C,
            )

    def test_dataclass_field_with_default(self) -> None:
        codestr = """
        from dataclasses import dataclass, field

        @dataclass
        class C:
            x: str = field(default="foo")

        c1 = C()
        c2 = C("bar")
        """
        with self.in_module(codestr) as mod:
            self.assertEqual(mod.C.x, "foo")
            self.assertEqual(mod.c1.x, "foo")
            self.assertEqual(mod.c2.x, "bar")

    def test_dataclass_field_with_incorrect_default_type(self) -> None:
        codestr = """
        from dataclasses import dataclass, field

        @dataclass
        class C:
            x: str = field(default=1)
        """
        self.type_error(
            codestr,
            r"type mismatch: Literal\[1\] cannot be assigned to str",
            at="1",
        )

    def test_dataclass_field_with_default_factory(self) -> None:
        codestr = """
        from dataclasses import dataclass, field

        @dataclass
        class C:
            x: str = field(default_factory=lambda: "foo")

        c1 = C()
        c2 = C("bar")
        """
        with self.in_module(codestr) as mod:
            self.assertEqual(mod.c1.x, "foo")
            self.assertEqual(mod.c2.x, "bar")

    def test_dataclass_field_with_incorrect_default_factory_type(self) -> None:
        codestr = """
        from dataclasses import dataclass, field

        @dataclass
        class C:
            x: str = field(default_factory=list)
        """
        with self.in_module(codestr) as mod:
            self.assertRaisesRegex(TypeError, "expected 'str', got 'list'", mod.C)

    def test_dataclass_field_cannot_set_both_default_and_default_factory(self) -> None:
        codestr = """
        from dataclasses import dataclass, field

        @dataclass
        class C:
            x: List[str] = field(default=[], default_factory=list)
        """
        self.type_error(
            codestr, "cannot specify both default and default_factory", at="class C:"
        )

    def test_dataclass_has_fields_attribute(self) -> None:
        codestr = """
        from dataclasses import dataclass, InitVar
        from typing import ClassVar

        class SomeField:
            pass

        @dataclass
        class C:
            x: str
            y: ClassVar[int]
            z: InitVar[SomeField]
        """
        with self.in_strict_module(codestr) as mod:
            fields = mod.C.__dataclass_fields__
            self.assertIsInstance(fields, dict)
            self.assertEqual(len(fields), 3)

            for name, type, kind in (
                ("x", "str", _FIELD),
                ("y", "ClassVar[int]", _FIELD_CLASSVAR),
                ("z", "InitVar[SomeField]", _FIELD_INITVAR),
            ):
                with self.subTest(name=name, type=type, kind=kind):
                    self.assertIn(name, fields)
                    field = fields[name]
                    self.assertIsInstance(field, Field)
                    self.assertEqual(field.name, name)
                    self.assertEqual(field.type, type)
                    self.assertIs(field.default, MISSING)
                    self.assertIs(field.default_factory, MISSING)
                    self.assertTrue(field.init)
                    self.assertTrue(field.repr)
                    self.assertIs(field.hash, None)
                    self.assertTrue(field.compare)
                    self.assertIsInstance(field.metadata, Mapping)
                    self.assertIs(field._field_type, kind)

    def test_nonstatic_dataclass_picks_up_static_fields(self) -> None:
        codestr = """
        from dataclasses import dataclass

        @dataclass
        class C:
            x: str
        """
        with self.in_module(codestr) as static_mod:
            codestr = f"""
            from dataclasses import dataclass
            from {static_mod.__name__} import C

            @dataclass
            class D(C):
                y: int
            """
            with self.in_module(codestr, code_gen=PythonCodeGenerator) as mod:
                d = mod.D("foo", 2)
                self.assertEqual(d.x, "foo")
                self.assertEqual(d.y, 2)

    def test_classvar_cannot_have_default_factory(self) -> None:
        codestr = """
        from dataclasses import dataclass, field
        from typing import ClassVar

        @dataclass
        class C:
            x: ClassVar[list] = field(default_factory=list)
        """
        self.type_error(
            codestr,
            "field x cannot have a default factory",
            at="x:",
        )

    def test_initvar_cannot_have_default_factory(self) -> None:
        codestr = """
        from dataclasses import dataclass, field, InitVar

        @dataclass
        class C:
            x: InitVar[list] = field(default_factory=list)
        """
        self.type_error(
            codestr,
            "field x cannot have a default factory",
            at="x:",
        )

    def test_field_cannot_have_mutable_default(self) -> None:
        for type, default in (("list", "[]"), ("dict", "{}"), ("set", "set()")):
            with self.subTest(type=type, default=default):
                codestr = f"""
                from dataclasses import dataclass

                @dataclass
                class C:
                    x: {type} = {default}
                """
                self.type_error(
                    codestr,
                    f"mutable default {type} for field x is not allowed: use default_factory",
                    at="x:",
                )

    def test_initvar_cannot_have_init_false(self) -> None:
        codestr = """
        from dataclasses import dataclass, field, InitVar

        @dataclass
        class C:
            x: InitVar[str] = field(init=False)
        """
        self.type_error(
            codestr,
            "InitVar fields must have init=True",
            at="x:",
        )

    def test_classvar_and_init_false_not_dunder_init_args(self) -> None:
        codestr = """
        from dataclasses import dataclass, field, InitVar
        from typing import ClassVar

        @dataclass
        class C:
            x: int
            y: ClassVar[int] = 4
            z: int = field(init=False)

            def __post_init__(self) -> None:
                self.z = 42

        c = C(1)
        """
        with self.in_module(codestr) as mod:
            self.assertEqual(mod.c.x, 1)
            self.assertEqual(mod.c.y, 4)
            self.assertEqual(mod.c.z, 42)

    def test_initvar_passed_to_post_init(self) -> None:
        codestr = """
        from dataclasses import dataclass, field, InitVar

        @dataclass
        class C:
            x: int
            y: InitVar[int]
            z: int

            def __post_init__(self, y: int) -> None:
                self.z += y

        c = C(1, 2, 3)
        """
        with self.in_module(codestr) as mod:
            self.assertEqual(mod.c.x, 1)
            self.assertFalse(hasattr(mod.c, "y"))
            self.assertEqual(mod.c.z, 5)

    def test_init_false_still_calls_default_factory(self) -> None:
        codestr = """
        from dataclasses import dataclass, field, InitVar

        def foo() -> int:
            return 42

        @dataclass
        class C:
            x: int = field(init=False, default_factory=foo)

        c = C()
        """
        with self.in_module(codestr) as mod:
            self.assertEqual(mod.c.x, 42)

    def test_comparisons_and_hash_with_field_compare_false(self) -> None:
        codestr = """
        from dataclasses import dataclass, field

        @dataclass(frozen=True, order=True)
        class C:
            x: int
            y: int = field(compare=False)
            z: int

        c1 = C(1, 2, 3)
        c2 = C(1, 4, 3)
        """
        with self.in_module(codestr) as mod:
            self.assertTrue(mod.c1 == mod.c2)
            self.assertFalse(mod.c1 < mod.c2)
            self.assertTrue(mod.c1 <= mod.c2)
            self.assertFalse(mod.c1 > mod.c2)
            self.assertTrue(mod.c1 >= mod.c2)

            self.assertEqual(hash(mod.c1), hash((1, 3)))
            self.assertEqual(hash(mod.c2), hash((1, 3)))

    def test_hash_with_field_hash_false(self) -> None:
        codestr = """
        from dataclasses import dataclass, field

        @dataclass(frozen=True)
        class C:
            x: int
            y: int = field(hash=False)
            z: int

        c = C(1, 2, 3)
        """
        with self.in_module(codestr) as mod:
            self.assertEqual(hash(mod.c), hash((1, 3)))

    def test_repr_with_field_repr_false(self) -> None:
        codestr = """
        from dataclasses import dataclass, field

        @dataclass
        class C:
            x: int
            y: int = field(repr=False)
            z: int

        c = C(1, 2, 3)
        """
        with self.in_module(codestr) as mod:
            self.assertRegex(repr(mod.c), r"C\(x=1, z=3\)")

    def test_dataclass_subclass_non_default_after_default_disallowed(self) -> None:
        codestr = """
        from dataclasses import dataclass, field

        @dataclass
        class C:
            x: int
            y: int = 1

        @dataclass
        class D(C):
            z: int
        """
        self.type_error(
            codestr,
            "non-default argument 'z' follows default argument",
            at="class D(C):",
        )

    def test_dataclass_subclass_default_after_default_allowed(self) -> None:
        codestr = """
        from dataclasses import dataclass, field

        @dataclass
        class C:
            x: int
            y: int = 1

        @dataclass
        class D(C):
            z: int = 2

        d1 = D(0)
        d2 = D(3, 4, 5)
        """
        with self.in_module(codestr) as mod:
            self.assertEqual(mod.d1.x, 0)
            self.assertEqual(mod.d1.y, 1)
            self.assertEqual(mod.d1.z, 2)

            self.assertEqual(mod.d2.x, 3)
            self.assertEqual(mod.d2.y, 4)
            self.assertEqual(mod.d2.z, 5)

    def test_dataclass_with_dataclass_subclass(self) -> None:
        codestr = """
        from dataclasses import dataclass

        @dataclass
        class C:
            x: int
            y: int

        @dataclass
        class D(C):
            z: int

        d = D(1, 2, 3)
        """
        with self.in_module(codestr) as mod:
            self.assertEqual(mod.d.x, 1)
            self.assertEqual(mod.d.y, 2)
            self.assertEqual(mod.d.z, 3)

    def test_dataclass_inheritance_chain(self) -> None:
        codestr = """
        from dataclasses import dataclass

        @dataclass
        class C:
            x: int
            y: int

        # dataclasses.is_dataclass(D) is still True,
        # but we do not need to process it as a dataclass
        class D(C):
            pass

        @dataclass
        class E(D):
            z: int

        e = E(1, 2, 3)
        """
        with self.in_module(codestr) as mod:
            self.assertEqual(mod.e.x, 1)
            self.assertEqual(mod.e.y, 2)
            self.assertEqual(mod.e.z, 3)

    def test_non_frozen_dataclass_cannot_subclass_frozen(self) -> None:
        codestr = """
        from dataclasses import dataclass

        @dataclass(frozen=True)
        class C:
            x: int
            y: int

        @dataclass
        class D(C):
            z: int
        """
        self.type_error(
            codestr,
            "cannot inherit non-frozen dataclass from a frozen one",
            at="class D(C):",
        )

    def test_frozen_dataclass_cannot_subclass_non_frozen(self) -> None:
        codestr = """
        from dataclasses import dataclass, field

        @dataclass
        class C:
            x: int
            y: int

        @dataclass(frozen=True)
        class D(C):
            z: int
        """
        self.type_error(
            codestr,
            "cannot inherit frozen dataclass from a non-frozen one",
            at="class D(C):",
        )

    def test_dataclass_with_no_assignment_inherits_default(self) -> None:
        codestr = """
        from dataclasses import dataclass

        @dataclass
        class C:
            x: int
            y: int = 1

        @dataclass
        class D(C):
            y: int

        d = D(0)
        """
        with self.in_module(codestr) as mod:
            self.assertEqual(mod.d.x, 0)
            self.assertEqual(mod.d.y, 1)

    def test_dataclass_can_load_inherited_defaults(self) -> None:
        codestr = """
        from dataclasses import dataclass, field

        @dataclass
        class C:
            x: int = 1
            y: int = field(default_factory=lambda: 2)

        @dataclass
        class D(C):
            z: int = 3

        d = D()
        """
        with self.in_module(codestr) as mod:
            self.assertEqual(mod.d.x, 1)
            self.assertEqual(mod.d.y, 2)
            self.assertEqual(mod.d.z, 3)

    def test_dataclass_with_field_overrides_default(self) -> None:
        codestr = """
        from dataclasses import dataclass, field

        @dataclass
        class C:
            x: int = 1

        @dataclass
        class D(C):
            x: int = field(hash=False)

        d = D()
        """
        self.type_error(codestr, "D.__init__ expects a value for argument x", at="D()")

    def test_dataclass_cannot_change_field_type(self) -> None:
        codestr = """
        from dataclasses import dataclass, field

        @dataclass
        class C:
            x: int

        @dataclass
        class D(C):
            x: str
        """
        self.type_error(
            codestr,
            r"Type of field 'x' on class '.*D' conflicts with base type\. "
            r"Base field has annotation int, but overridden field has annotation str",
            at="class D",
        )

    def test_dataclass_cannot_narrow_field_type(self) -> None:
        codestr = """
        from dataclasses import dataclass, field

        @dataclass
        class C:
            x: int

        @dataclass
        class D(C):
            x: bool
        """
        self.type_error(
            codestr,
            r"Type of field 'x' on class '.*D' conflicts with base type\. "
            r"Base field has annotation int, but overridden field has annotation bool",
            at="class D",
        )

    def test_dataclass_cannot_override_true_field_with_pseudo_field(self) -> None:
        annotations = ("ClassVar", "InitVar")
        for psuedo_field in annotations:
            with self.subTest(override=psuedo_field):
                codestr = f"""
                from dataclasses import dataclass, InitVar
                from typing import ClassVar

                @dataclass
                class C:
                    x: int

                @dataclass
                class D(C):
                    x: {psuedo_field}[int]
                """
                self.type_error(
                    codestr,
                    f"Override of field 'x' cannot be a {psuedo_field}",
                    at="class D",
                )

    def test_dataclass_override_pseudo_field_with_other(self) -> None:
        annotations = {
            "ClassVar": ("int", "InitVar[int]"),
            "InitVar": ("int", "ClassVar[int]"),
        }
        for base, overrides in annotations.items():
            for override in overrides:
                with self.subTest(base=base, override=override):
                    codestr = f"""
                    from dataclasses import dataclass, InitVar
                    from typing import ClassVar

                    @dataclass
                    class C:
                        x: {base}[int]

                    @dataclass
                    class D(C):
                        x: {override}
                    """
                    self.type_error(
                        codestr,
                        f"Override of field 'x' must be a {base}",
                        at="class D",
                    )

    def test_dataclass_override_must_be_consistent(self) -> None:
        codestr = f"""
        from dataclasses import dataclass

        @dataclass
        class C:
            x: int

        @dataclass
        class D:
            x: str

        @dataclass
        class Join(D, C):
            pass
        """
        self.type_error(
            codestr,
            r"Type of field 'x' on class '.*Join' conflicts with base type\. "
            r"Base field has annotation int, but overridden field has annotation str",
            at="class Join",
        )

    def test_dataclass_does_not_allow_multiple_inheritance(self) -> None:
        codestr = f"""
        from dataclasses import dataclass

        @dataclass
        class C:
            x: int

        @dataclass
        class D:
            x: int

        @dataclass
        class Join(D, C):
            pass
        """
        self.assertRaisesRegex(
            TypeError,
            "multiple bases have instance lay-out conflict",
            self.run_code,
            codestr,
        )

    def test_dataclass_can_call_init_on_subclass(self) -> None:
        codestr = f"""
        from dataclasses import dataclass

        @dataclass
        class C:
            x: int

        class D(C):
            pass

        d = D(1)
        d.__init__(2)
        """
        with self.in_module(codestr) as mod:
            self.assertEqual(mod.d.x, 2)

    def test_dataclass_method_call(self) -> None:
        codestr = f"""
        from dataclasses import dataclass

        @dataclass
        class C:
            x: int

            def squared(self) -> int:
                return self.x * self.x

        c = C(10)
        res = c.squared()
        """
        with self.in_module(codestr) as mod:
            self.assertEqual(mod.res, 100)

    def test_dynamic_dataclass_type_bind_visits_arguments(self) -> None:
        codestr = """
        from dataclasses import dataclass

        def foo():
            @dataclass(frozen=True)
            class C:
                pass

            return C

        C = foo()
        """
        with self.in_module(codestr) as mod:
            self.assertTrue(is_dataclass(mod.C))
            self.assertTrue(mod.C.__dataclass_params__.frozen)

    def test_dataclass_methods_statically_compiled(self) -> None:
        codestr = """
        from dataclasses import dataclass
        @dataclass
        class C:
            x: int
            y: str
        """
        with self.in_module(codestr) as mod:
            self.assertTrue(mod.C.__init__.__code__.co_flags & CO_STATICALLY_COMPILED)
