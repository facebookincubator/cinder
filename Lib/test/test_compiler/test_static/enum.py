import itertools
from compiler.static.types import TypedSyntaxError

from _static import PRIM_OP_EQ_INT, TYPED_INT64

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

    def test_non_constant_disallowed(self):
        codestr = """
        from __static__ import Enum

        def foo() -> int:
            return 1

        class Foo(Enum):
            FOO = foo()
        """
        self.type_error(
            codestr,
            "Cannot resolve Enum value at compile time",
            at="FOO = foo()",
        )

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

    def test_int64enum_mixins_disallowed(self):
        codestr = """
        from __static__ import Int64Enum

        class Foo(float, Int64Enum):
            pass
        """
        self.type_error(codestr, "Int64Enum types cannot support multiple bases:")

    def test_int64enum_subclassing_disallowed(self):
        codestr = """
        from __static__ import Int64Enum

        class Foo(Int64Enum):
            pass

        class Bar(Foo):
            pass
        """
        self.type_error(codestr, "Int64Enum types do not allow subclassing")

    def test_non_constant_int64enum_disallowed(self):
        codestr = """
        from __static__ import Int64Enum

        def foo() -> int:
            return 1

        class Foo(Int64Enum):
            FOO = foo()
        """
        self.type_error(
            codestr,
            "Cannot resolve Int64Enum value at compile time",
            at="FOO = foo()",
        )

    def test_int64enum_non_int_disallowed(self):
        self.type_error(
            """
            from __static__ import Int64Enum

            class Foo(Int64Enum):
                BAR = "not an int"
            """,
            "Int64Enum values must be int, not str",
            at='BAR = "not an int"',
        )

    def test_int64enum_overflow(self):
        self.type_error(
            """
            from __static__ import Int64Enum

            class Foo(Int64Enum):
                BAR = 2**63
            """,
            "Value 9223372036854775808 for <module>.Foo.BAR is out of bounds",
            at="BAR = 2**63",
        )

    def test_int64enum_underflow(self):
        self.type_error(
            """
            from __static__ import Int64Enum

            class Foo(Int64Enum):
                BAR = -2**63 - 1
            """,
            "Value -9223372036854775809 for <module>.Foo.BAR is out of bounds",
            at="BAR = -2**63 - 1",
        )

    def test_int64enum_compare_with_int_disallowed(self):
        codestr = """
        from __static__ import Int64Enum

        class Bit(Int64Enum):
            ZERO = 0
            ONE = 1

        def is_set(bit: Bit) -> bool:
            return bit == 1
        """
        with self.assertRaisesRegex(
            TypedSyntaxError, r"can't compare foo\.Bit to Literal\[1\]"
        ):
            self.compile(codestr, modname="foo")

    def test_int64enum_compare_different_enums_disallowed(self):
        codestr = """
        from __static__ import Int64Enum

        class Bit(Int64Enum):
            ZERO = 0
            ONE = 1

        class Color(Int64Enum):
            RED = 0
            GREEN = 1
            BLUE = 2

        def foo() -> bool:
            return Bit.ZERO == Color.RED
        """
        with self.assertRaisesRegex(
            TypedSyntaxError,
            r"can't compare <foo\.Bit\.ZERO: 0> to <foo\.Color\.RED: 0>",
        ):
            self.compile(codestr, modname="foo")

    def test_int64enum_reverse_compare_with_int_disallowed(self):
        codestr = """
        from __static__ import Int64Enum

        class Bit(Int64Enum):
            ZERO = 0
            ONE = 1

        def is_set(bit: Bit) -> bool:
            return 1 == bit
        """
        with self.assertRaisesRegex(
            TypedSyntaxError, r"can't compare Literal\[1\] to foo\.Bit"
        ):
            self.compile(codestr, modname="foo")

    def test_int64enum_delattr_disallowed(self):
        codestr = """
        from __static__ import Int64Enum

        class Direction(Int64Enum):
            NORTH = 0
            SOUTH = 1
            EAST = 2
            WEST = 3

        def north_pole() -> None:
            # You can't go north at the North Pole.
            del Direction.NORTH
        """
        with self.assertRaisesRegex(
            TypedSyntaxError, "Enum values cannot be modified or deleted"
        ):
            self.compile(codestr)

    def test_int64enum_setattr_disallowed(self):
        codestr = """
        from __static__ import Int64Enum

        class Color(Int64Enum):
            RED = 0
            GREEN = 1
            BLUE = 2

        def redshift() -> None:
            Color.RED = -1
        """
        with self.assertRaisesRegex(
            TypedSyntaxError, "Enum values cannot be modified or deleted"
        ):
            self.compile(codestr)

    def test_int64enum_function_arg_and_return_type(self):
        codestr = """
        from __static__ import Int64Enum

        class Coin(Int64Enum):
            HEADS = 0
            TAILS = 1

        def flip(coin: Coin) -> Coin:
            if coin == Coin.HEADS:
                return Coin.TAILS
            return Coin.HEADS
        """
        with self.in_strict_module(codestr) as mod:
            self.assertInBytecode(mod.flip, "PRIMITIVE_COMPARE_OP", PRIM_OP_EQ_INT)

            self.assertEqual(mod.flip(mod.Coin.HEADS), mod.Coin.TAILS)
            self.assertEqual(mod.flip(mod.Coin.TAILS), mod.Coin.HEADS)

    def test_function_returns_int64enum(self):
        codestr = """
        from __static__ import Int64Enum

        class Sign(Int64Enum):
            NEGATIVE = -1
            ZERO = 0
            POSITIVE = 1

        def sign(val: float) -> Sign:
            if val < 0:
                return Sign.NEGATIVE
            if val == 0:
                return Sign.ZERO
            return Sign.POSITIVE
        """
        with self.in_strict_module(codestr) as mod:
            self.assertInBytecode(mod.sign, "PRIMITIVE_LOAD_CONST", (-1, TYPED_INT64))
            self.assertInBytecode(mod.sign, "PRIMITIVE_LOAD_CONST", (0, TYPED_INT64))
            self.assertInBytecode(mod.sign, "PRIMITIVE_LOAD_CONST", (1, TYPED_INT64))

            self.assertEqual(mod.sign(0), mod.Sign.ZERO)
            self.assertEqual(mod.sign(-42.0), mod.Sign.NEGATIVE)
            self.assertEqual(mod.sign(42.0), mod.Sign.POSITIVE)

    def test_pass_int64enum_between_static_functions(self):
        codestr = """
        from __static__ import Int64Enum

        class Bit(Int64Enum):
            ZERO = 0
            ONE = 1

        def bitwise_not(bit: Bit) -> Bit:
            if bit == Bit.ZERO:
                return Bit.ONE
            return Bit.ZERO

        def bitwise_or(bit1: Bit, bit2: Bit) -> Bit:
            if bit1 == Bit.ONE or bit2 == Bit.ONE:
                return Bit.ONE
            return Bit.ZERO

        def bitwise_nor(bit1: Bit, bit2: Bit) -> Bit:
            return bitwise_not(bitwise_or(bit1, bit2))
        """
        with self.in_strict_module(codestr) as mod:
            self.assertEqual(mod.bitwise_nor(mod.Bit.ZERO, mod.Bit.ZERO), mod.Bit.ONE)
            self.assertEqual(mod.bitwise_nor(mod.Bit.ZERO, mod.Bit.ONE), mod.Bit.ZERO)
            self.assertEqual(mod.bitwise_nor(mod.Bit.ONE, mod.Bit.ZERO), mod.Bit.ZERO)
            self.assertEqual(mod.bitwise_nor(mod.Bit.ONE, mod.Bit.ONE), mod.Bit.ZERO)

    def test_call_converts_int_to_int64enum(self):
        codestr = """
        from __static__ import Int64Enum

        class Bit(Int64Enum):
            ZERO = 0
            ONE = 1

        def convert_to_bit(num: int) -> Bit:
            return Bit(num)
        """
        with self.in_strict_module(codestr) as mod:
            self.assertEqual(mod.convert_to_bit(0), mod.Bit.ZERO)
            self.assertEqual(mod.convert_to_bit(1), mod.Bit.ONE)

    def test_int64enum_primitive_unbox_shadowcode(self):
        codestr = """
        from __static__ import Int64Enum

        class Bit(Int64Enum):
            ZERO = 0
            ONE = 1

        def convert_to_bit(num: int) -> Bit:
            return Bit(num)
        """
        with self.in_strict_module(codestr) as mod:
            for _ in range(100):
                self.assertEqual(mod.convert_to_bit(0), mod.Bit.ZERO)
                self.assertEqual(mod.convert_to_bit(1), mod.Bit.ONE)

    def test_int64enum_primitive_return(self):
        vals = [("Foo.BAR", 1), ("Foo.BAZ", 2)]
        tf = [True, False]
        for val, box, strict, error, unjitable in itertools.product(
            vals, tf, tf, tf, tf
        ):
            unjitable_code = "class c: pass" if unjitable else ""
            codestr = f"""
                from __static__ import box, Int64Enum

                class Foo(Int64Enum):
                    BAR = 1
                    BAZ = 2

                def f(error: bool) -> Foo:
                    {unjitable_code}
                    if error:
                        raise RuntimeError("boom")
                    return {val[0]}

            """
            if box:
                codestr += f"""
                def g() -> bool:
                    x = f({error})
                    y = Foo({val[1]})
                    return box(x) == box(y)
                """
            else:
                codestr += f"""
                def g() -> bool:
                    x = f({error})
                    y = Foo({val[1]})
                    return box(x == y)
                """
            ctx = self.in_strict_module if strict else self.in_module
            with self.subTest(
                val=val,
                box=box,
                strict=strict,
                error=error,
                unjitable=unjitable,
            ):
                with ctx(codestr) as mod:
                    f = mod.f
                    g = mod.g

                    self.assertInBytecode(f, "RETURN_PRIMITIVE", TYPED_INT64)

                    if error:
                        with self.assertRaisesRegex(RuntimeError, "boom"):
                            g()
                    else:
                        self.assertTrue(g())

                    self.assert_jitted(g)
                    if unjitable:
                        self.assert_not_jitted(f)
                    else:
                        self.assert_jitted(f)

    def test_boxed_int64enum_cannot_be_returned_primitive(self):
        self.type_error(
            """
            from __static__ import box, Int64Enum

            class Foo(Int64Enum):
                BAR = 1
                BAZ = 2

            def f(foo: Foo) -> Foo:
                return box(foo)
            """,
            r"return type must be .*\.Foo, not Boxed\[.*\.Foo\]",
            at="return box(foo)",
        )

    def test_boxed_int64enum_can_be_returned_as_object(self):
        codestr = """
        from __static__ import box, Int64Enum

        class Foo(Int64Enum):
            BAR = 1
            BAZ = 2

        def f(foo: Foo) -> object:
            return box(foo)
        """
        with self.in_strict_module(codestr) as mod:
            self.assertEqual(mod.f(mod.Foo.BAR), mod.Foo.BAR)
            self.assertEqual(mod.f(mod.Foo.BAZ), mod.Foo.BAZ)

    def test_cannot_unbox_int64enum(self):
        self.type_error(
            """
            from __static__ import Int64Enum

            class Foo(Int64Enum):
                BAR = 1
                BAZ = 2

            def f(foo: Foo) -> Foo:
                return Foo(foo)
            """,
            "Call argument cannot be a primitive",
            at="foo",
        )

    def test_cannot_box_boxed_int64enum(self):
        self.type_error(
            """
            from __static__ import box, Int64Enum

            class Foo(Int64Enum):
                BAR = 1
                BAZ = 2

            def f(foo: Foo) -> object:
                x = box(foo)
                return box(x)
            """,
            r"can't box non-primitive: Boxed\[.*\.Foo\]",
            at="box(x)",
        )

    def test_int64enum_store_attr(self):
        self.type_error(
            """
            from __static__ import box, Int64Enum

            class Foo(Int64Enum):
                BAR = 1
                BAZ = 2

            def modify(foo: Foo) -> None:
                foo.name = "NEW"
            """,
            "Enum values cannot be modified or deleted",
            at="foo.name",
        )

    def test_int64enum_delete_attr(self):
        self.type_error(
            """
            from __static__ import box, Int64Enum

            class Foo(Int64Enum):
                BAR = 1
                BAZ = 2

            def modify(foo: Foo) -> None:
                del foo.name
            """,
            "Enum values cannot be modified or deleted",
            at="foo.name",
        )

    def test_int64enum_method(self):
        codestr = """
        from __static__ import box, Int64Enum

        class Foo(Int64Enum):
            BAR = 1
            BAZ = 2

            def even(self) -> bool:
                if self == Foo.BAR:
                    return False
                return True

        def odd(foo: Foo) -> bool:
            return not foo.even()
        """
        with self.in_strict_module(codestr) as mod:
            self.assertInBytecode(
                mod.odd, "INVOKE_FUNCTION", ((mod.__name__, "Foo", "!", "even"), 1)
            )
            self.assertNotInBytecode(mod.odd, "PRIMITIVE_BOX")

            self.assertTrue(mod.odd(mod.Foo.BAR))
            self.assertFalse(mod.odd(mod.Foo.BAZ))

            self.assertFalse(mod.Foo.BAR.even())
            self.assertTrue(mod.Foo.BAZ.even())

    def test_int64enum_name(self):
        codestr = """
        from __static__ import box, Int64Enum

        class Foo(Int64Enum):
            BAR = 1
            BAZ = 2

        def name(foo: Foo) -> str:
            return foo.name
        """
        with self.in_strict_module(codestr) as mod:
            self.assertEqual(mod.name(mod.Foo.BAR), "BAR")
            self.assertEqual(mod.name(mod.Foo.BAZ), "BAZ")

    def test_int64enum_value(self):
        codestr = """
        from __static__ import box, Int64Enum, int64

        class Foo(Int64Enum):
            BAR = 1
            BAZ = 2

        def value(foo: Foo) -> int64:
            return foo.value
        """
        with self.in_strict_module(codestr) as mod:
            self.assertEqual(mod.value(mod.Foo.BAR), 1)
            self.assertEqual(mod.value(mod.Foo.BAZ), 2)

    def test_pass_int64enum_from_module_level(self):
        codestr = """
        from __static__ import box, Int64Enum

        class Foo(Int64Enum):
            BAR = 1
            BAZ = 2

        def value(foo: Foo) -> int:
            return box(foo.value)

        result1 = value(Foo.BAR)
        result2 = value(Foo.BAZ)
        """
        with self.in_strict_module(codestr) as mod:
            self.assertEqual(mod.result1, 1)
            self.assertEqual(mod.result2, 2)

    def test_return_int64enum_to_module_level(self):
        codestr = """
        from __static__ import box, Int64Enum

        class Foo(Int64Enum):
            BAR = 1
            BAZ = 2

        def get_foo(val: int) -> Foo:
            return Foo(val)


        result1 = box(get_foo(1))
        result2 = box(get_foo(2))
        """
        with self.in_strict_module(codestr) as mod:
            self.assertEqual(mod.result1, mod.Foo.BAR)
            self.assertEqual(mod.result2, mod.Foo.BAZ)

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

    def test_non_constant_int_enum_disallowed(self):
        codestr = """
        from __static__ import IntEnum

        def foo() -> int:
            return 1

        class Foo(IntEnum):
            FOO = foo()
        """
        self.type_error(
            codestr,
            "Cannot resolve IntEnum value at compile time",
            at="FOO = foo()",
        )

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
            "StringEnum values must be str, not int",
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

    def test_non_constant_string_enum_disallowed(self):
        codestr = """
        from __static__ import StringEnum

        def foo() -> str:
            return "foo"

        class Foo(StringEnum):
            FOO = foo()
        """
        self.type_error(
            codestr,
            "Cannot resolve StringEnum value at compile time",
            at="FOO = foo()",
        )

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
