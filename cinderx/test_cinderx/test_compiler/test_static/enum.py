import itertools
from unittest import skip

from cinderx.compiler.static.types import TypedSyntaxError

from cinderx.static import PRIM_OP_EQ_INT, TYPED_INT64

from .common import StaticTestBase


class StaticEnumTests(StaticTestBase):
    def test_mixins_unsupported(self):
        codestr = """
        from __static__ import Enum

        class Foo(int, Enum):
            pass
        """
        with self.assertRaisesRegex(
            TypedSyntaxError,
            "Static Enum types cannot support multiple bases:",
        ):
            self.compile(codestr)

    def test_subclassing_unsupported(self):
        codestr = """
        from __static__ import Enum

        class Foo(Enum):
            pass

        class Bar(Foo):
            pass
        """
        with self.assertRaisesRegex(
            TypedSyntaxError,
            "Static Enum types do not allow subclassing",
        ):
            self.compile(codestr)

    def test_non_constant_allowed(self):
        codestr = """
        from __static__ import Enum

        def foo() -> int:
            return 1

        class Foo(Enum):
            FOO = foo()
        """
        with self.in_strict_module(codestr) as mod:
            self.assertEqual(mod.Foo.FOO.value, 1)

    def test_multiple_types_allowed(self):
        codestr = """
        from __static__ import Enum

        class Foo(Enum):
            FOO = "FOO"
            BAR = 23
        """
        with self.in_module(codestr) as mod:
            self.assertEqual(mod.Foo.FOO.value, "FOO")
            self.assertEqual(mod.Foo.BAR.value, 23)

    def test_delattr_disallowed(self):
        codestr = """
        from __static__ import Enum

        class Foo(Enum):
            FOO = "FOO"
            BAR = 23

        def delete() -> None:
            del Foo.FOO
        """
        self.type_error(
            codestr,
            "Static Enum values cannot be modified or deleted",
        )

    def test_setattr_disallowed(self):
        codestr = """
        from __static__ import Enum

        class Foo(Enum):
            FOO = "FOO"
            BAR = 23

        def bar():
            Foo.FOO = "BAR"
        """
        self.type_error(
            codestr,
            "Static Enum values cannot be modified or deleted",
        )

    def test_compare_enum_to_values(self):
        codestr = """
        from __static__ import Enum

        class Foo(Enum):
            FOO = "FOO"
            BAR = 23

        def f():
            return Foo.FOO == "FOO", Foo.BAR == 23
        """
        with self.in_module(codestr) as mod:
            self.assertEqual(mod.f(), (False, False))

    def test_compare_enum(self):
        codestr = """
        from __static__ import Enum

        class Foo(Enum):
            FOO = "FOO"
            BAR = "FOO"
            BAZ = "BAZ"

        def f():
            return (
                Foo.FOO == Foo.FOO,
                Foo.FOO == Foo.BAR,
                Foo.FOO == Foo.BAZ,
            )
        """
        with self.in_module(codestr) as mod:
            self.assertEqual(mod.f(), (True, True, False))

    def test_compare_enum_nonstatic(self):
        codestr = """
        from __static__ import Enum

        class Foo(Enum):
             FOO = 1
             BAR = 1
             BAZ = 2
        """
        with self.in_module(codestr) as mod:
            self.assertTrue(mod.Foo.FOO == mod.Foo.FOO)
            self.assertTrue(mod.Foo.FOO == mod.Foo.BAR)
            self.assertFalse(mod.Foo.FOO == mod.Foo.BAZ)

    def test_compare_different_enums(self):
        codestr = """
        from __static__ import Enum

        class Foo(Enum):
             FOO = 1

        class Bar(Enum):
             FOO = 1

        def f():
            return Foo.FOO == Bar.FOO
        """
        with self.in_module(codestr) as mod:
            self.assertEqual(mod.f(), False)

    def test_compare_different_enums_nonstatic(self):
        codestr = """
        from __static__ import Enum

        class Foo(Enum):
             FOO = 1

        class Bar(Enum):
             FOO = 1
        """
        with self.in_module(codestr) as mod:
            self.assertFalse(mod.Foo.FOO == mod.Bar.FOO)

    def test_enum_with_annotations(self):
        codestr = """
        from __static__ import Enum

        class C(Enum):
            FOO: int = 1
            BAR: str = "bar"
        """
        with self.in_strict_module(codestr) as mod:
            self.assertEqual(mod.C.FOO.value, 1)
            self.assertEqual(mod.C.BAR.value, "bar")

    def test_enum_defined_in_terms_of_other_enum(self):
        codestr = """
        from __static__ import Enum

        class C(Enum):
            FOO = "FOO"

        class D(Enum):
            FOO = C.FOO

        reveal_type(D.FOO.value)
        """
        # Make sure we're binding to the enum type and not the value
        self.revealed_type(codestr, "Exact[<module>.C]")

    def test_int_enum_mixins_unsupported(self):
        codestr = """
        from __static__ import IntEnum

        class Foo(str, IntEnum):
            pass
        """
        self.type_error(codestr, "Static IntEnum types cannot support multiple bases")

    def test_subclassing_int_enum_unsupported(self):
        codestr = """
        from __static__ import IntEnum

        class Foo(IntEnum):
            pass

        class Bar(Foo):
            pass
        """
        with self.assertRaisesRegex(
            TypedSyntaxError,
            "Static IntEnum types do not allow subclassing",
        ):
            self.compile(codestr)

    def test_non_int_int_enum_unsupported(self):
        codestr = """
        from __static__ import IntEnum

        class Foo(IntEnum):
            FOO = 1
            BAR = "BAR"
        """
        self.type_error(
            codestr,
            "IntEnum values must be int, not str",
            at='BAR = "BAR"',
        )

    def test_delattr_int_enum_disallowed(self):
        codestr = """
        from __static__ import IntEnum

        class Foo(IntEnum):
            FOO = 1

        def delete() -> None:
            del Foo.FOO
        """
        self.type_error(
            codestr,
            "Static Enum values cannot be modified or deleted",
        )

    def test_setattr_int_enum_disallowed(self):
        codestr = """
        from __static__ import IntEnum

        class Foo(IntEnum):
            FOO = 1

        def bar():
            Foo.FOO = 2
        """
        self.type_error(
            codestr,
            "Static Enum values cannot be modified or deleted",
        )

    def test_non_constant_int_enum_allowed(self):
        codestr = """
        from __static__ import IntEnum

        def foo() -> int:
            return 1

        class Foo(IntEnum):
            FOO = foo()
        """
        with self.in_strict_module(codestr) as mod:
            self.assertEqual(mod.Foo.FOO, 1)

    def test_compare_int_enum_to_int(self):
        codestr = """
        from __static__ import IntEnum

        class Foo(IntEnum):
             FOO = 1

        def f():
            return (Foo.FOO == 1, Foo.FOO == 2)
        """
        with self.in_module(codestr) as mod:
            self.assertEqual(mod.f(), (True, False))

    def test_compare_int_enum_to_int_enum(self):
        codestr = """
        from __static__ import IntEnum

        class Foo(IntEnum):
             FOO = 1
             BAR = 2

        def f():
            return (Foo.FOO == Foo.FOO, Foo.FOO == Foo.BAR)
        """
        with self.in_module(codestr) as mod:
            self.assertEqual(mod.f(), (True, False))

    def test_compare_int_enum_to_int_nonstatic(self):
        codestr = """
        from __static__ import IntEnum

        class Foo(IntEnum):
             FOO = 1
        """
        with self.in_module(codestr) as mod:
            self.assertTrue(mod.Foo.FOO == 1)
            self.assertFalse(mod.Foo.FOO == 2)

    def test_compare_int_enum_to_int_enum_nonstatic(self):
        codestr = """
        from __static__ import IntEnum

        class Foo(IntEnum):
             FOO = 1
             BAR = 2
        """
        with self.in_module(codestr) as mod:
            self.assertTrue(mod.Foo.FOO == mod.Foo.FOO)
            self.assertFalse(mod.Foo.FOO == mod.Foo.BAR)

    def test_compare_different_int_enums(self):
        codestr = """
        from __static__ import IntEnum

        class Foo(IntEnum):
             FOO = 1
             BAR = 2

        class Bar(IntEnum):
             FOO = 1

        def f():
            return Foo.FOO == Bar.FOO, Foo.BAR == Bar.FOO
        """
        with self.in_module(codestr) as mod:
            # To match existing behavior, values from different enums with the same constant
            # compare to be equal.
            self.assertEqual(mod.f(), (True, False))

    def test_compare_different_int_enums_nonstatic(self):
        codestr = """
        from __static__ import IntEnum

        class Foo(IntEnum):
             FOO = 1
             BAR = 2

        class Bar(IntEnum):
             FOO = 1
        """
        with self.in_module(codestr) as mod:
            self.assertTrue(mod.Foo.FOO == mod.Bar.FOO)
            self.assertFalse(mod.Foo.BAR == mod.Bar.FOO)

    def test_int_enum_instances_are_ints(self):
        codestr = """
        from __static__ import IntEnum

        class Foo(IntEnum):
             FOO = 1
             BAR = 2

        def foo(s: int) -> str:
            return f"{s} is an int"

        def bar() -> str:
            return foo(Foo.FOO)
        """
        with self.in_module(codestr) as mod:
            self.assertEqual(mod.bar(), "Foo.FOO is an int")
            self.assertEqual(mod.foo(mod.Foo.BAR), "Foo.BAR is an int")

    def test_int_enum_with_annotations(self):
        codestr = """
        from __static__ import IntEnum

        class C(IntEnum):
            FOO: int = 1
            BAR: int = 2
        """
        with self.in_module(codestr) as mod:
            self.assertEqual(mod.C.FOO, 1)
            self.assertEqual(mod.C.BAR, 2)

    def test_int_enum_defined_in_terms_of_other_enum(self):
        codestr = """
        from __static__ import IntEnum

        class C(IntEnum):
            FOO = 1
            BAR = 2

        class D(IntEnum):
            FOO = C.FOO
            BAR = C.BAR
        """
        with self.in_module(codestr) as mod:
            self.assertEqual(mod.D.FOO, 1)
            self.assertEqual(mod.D.BAR, 2)

    def test_string_enum_mixins_unsupported(self):
        codestr = """
        from __static__ import StringEnum

        class Foo(int, StringEnum):
            pass
        """
        self.type_error(
            codestr, "Static StringEnum types cannot support multiple bases"
        )

    def test_subclassing_string_enums_unsupported(self):
        codestr = """
        from __static__ import StringEnum

        class Foo(StringEnum):
            pass

        class Bar(Foo):
            pass
        """
        with self.assertRaisesRegex(
            TypedSyntaxError,
            "Static StringEnum types do not allow subclassing",
        ):
            self.compile(codestr)

    def test_non_str_string_enum_unsupported(self):
        codestr = """
        from __static__ import StringEnum

        class Foo(StringEnum):
            FOO = "FOO"
            BAR = 23
        """
        self.type_error(
            codestr,
            r"StringEnum values must be str, not Literal\[23\]",
            at="BAR = 23",
        )

    def test_delattr_string_enum_disallowed(self):
        codestr = """
        from __static__ import StringEnum

        class Foo(StringEnum):
            FOO = "FOO"

        def delete() -> None:
            del Foo.FOO
        """
        self.type_error(
            codestr,
            "Static Enum values cannot be modified or deleted",
        )

    def test_setattr_string_enum_disallowed(self):
        codestr = """
        from __static__ import StringEnum

        class Foo(StringEnum):
            FOO = "FOO"

        def bar():
            Foo.FOO = "BAR"
        """
        self.type_error(
            codestr,
            "Static Enum values cannot be modified or deleted",
        )

    def test_non_constant_string_enum_allowed(self):
        codestr = """
        from __static__ import StringEnum

        def foo() -> str:
            return "foo"

        class Foo(StringEnum):
            FOO = foo()
        """
        with self.in_strict_module(codestr) as mod:
            self.assertEqual(mod.Foo.FOO, "foo")

    def test_compare_string_enum_to_string(self):
        codestr = """
        from __static__ import StringEnum

        class Foo(StringEnum):
             FOO = "FOO"

        def f():
            return (Foo.FOO == "FOO", Foo.FOO == "BAR")
        """
        with self.in_module(codestr) as mod:
            self.assertEqual(mod.f(), (True, False))

    def test_compare_string_enum_to_string_enum(self):
        codestr = """
        from __static__ import StringEnum

        class Foo(StringEnum):
             FOO = "FOO"
             BAR = "BAR"

        def f():
            return (Foo.FOO == Foo.FOO, Foo.FOO == Foo.BAR)
        """
        with self.in_module(codestr) as mod:
            self.assertEqual(mod.f(), (True, False))

    def test_compare_string_enum_to_string_nonstatic(self):
        codestr = """
        from __static__ import StringEnum

        class Foo(StringEnum):
             FOO = "FOO"
        """
        with self.in_module(codestr) as mod:
            self.assertTrue(mod.Foo.FOO == "FOO")
            self.assertFalse(mod.Foo.FOO == "BAR")

    def test_compare_string_enum_to_string_enum_nonstatic(self):
        codestr = """
        from __static__ import StringEnum

        class Foo(StringEnum):
             FOO = "FOO"
             BAR = "BAR"
        """
        with self.in_module(codestr) as mod:
            self.assertTrue(mod.Foo.FOO == mod.Foo.FOO)
            self.assertFalse(mod.Foo.FOO == mod.Foo.BAR)

    def test_compare_different_string_enums(self):
        codestr = """
        from __static__ import StringEnum

        class Foo(StringEnum):
             FOO = "FOO"
             BAR = "BAR"

        class Bar(StringEnum):
             FOO = "FOO"

        def f():
            return Foo.FOO == Bar.FOO, Foo.BAR == Bar.FOO
        """
        with self.in_module(codestr) as mod:
            # To match existing behavior, values from different enums with the same constant
            # compare to be equal.
            self.assertEqual(mod.f(), (True, False))

    def test_compare_different_string_enums_nonstatic(self):
        codestr = """
        from __static__ import StringEnum

        class Foo(StringEnum):
             FOO = "FOO"
             BAR = "BAR"

        class Bar(StringEnum):
             FOO = "FOO"
        """
        with self.in_module(codestr) as mod:
            self.assertTrue(mod.Foo.FOO == mod.Bar.FOO)
            self.assertFalse(mod.Foo.BAR == mod.Bar.FOO)

    def test_string_enum_instances_are_strings(self):
        codestr = """
        from __static__ import StringEnum

        class Foo(StringEnum):
             FOO = "FOO"
             BAR = "BAR"

        def foo(s: str) -> str:
            return f"{s} is a string"

        def bar() -> str:
            return foo(Foo.FOO)
        """
        with self.in_module(codestr) as mod:
            self.assertEqual(mod.bar(), "FOO is a string")
            self.assertEqual(mod.foo(mod.Foo.BAR), "BAR is a string")

    def test_string_enum_with_annotations(self):
        codestr = """
        from __static__ import StringEnum

        class C(StringEnum):
            FOO: str = "FOO"
            BAR: str = "BAR"
        """
        with self.in_module(codestr) as mod:
            self.assertEqual(mod.C.FOO, "FOO")
            self.assertEqual(mod.C.BAR, "BAR")

    def test_string_enum_defined_in_terms_of_other_enum(self):
        codestr = """
        from __static__ import StringEnum

        class C(StringEnum):
            FOO = "FOO"
            BAR = "BAR"

        class D(StringEnum):
            FOO = C.FOO
            BAR = C.BAR
        """
        with self.in_module(codestr) as mod:
            self.assertEqual(mod.D.FOO, "FOO")
            self.assertEqual(mod.D.BAR, "BAR")
