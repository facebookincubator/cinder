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

    def test_compare_with_int_disallowed(self):
        codestr = """
        from __static__ import Enum

        class Bit(Enum):
            ZERO = 0
            ONE = 1

        def is_set(bit: Bit) -> bool:
            return bit == 1
        """
        with self.assertRaisesRegex(
            TypedSyntaxError, r"can't compare foo\.Bit to Literal\[1\]"
        ):
            self.compile(codestr, modname="foo")

    def test_compare_different_enums_disallowed(self):
        codestr = """
        from __static__ import Enum

        class Bit(Enum):
            ZERO = 0
            ONE = 1

        class Color(Enum):
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

    def test_reverse_compare_with_int_disallowed(self):
        codestr = """
        from __static__ import Enum

        class Bit(Enum):
            ZERO = 0
            ONE = 1

        def is_set(bit: Bit) -> bool:
            return 1 == bit
        """
        with self.assertRaisesRegex(
            TypedSyntaxError, r"can't compare Literal\[1\] to foo\.Bit"
        ):
            self.compile(codestr, modname="foo")

    def test_delattr_disallowed(self):
        codestr = """
        from __static__ import Enum

        class Direction(Enum):
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

    def test_setattr_disallowed(self):
        codestr = """
        from __static__ import Enum

        class Color(Enum):
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

    def test_enum_function_arg_and_return_type(self):
        codestr = """
        from __static__ import Enum

        class Coin(Enum):
            HEADS = 0
            TAILS = 1

        def flip(coin: Coin) -> Coin:
            if coin == Coin.HEADS:
                return Coin.TAILS
            return Coin.HEADS
        """
        with self.in_strict_module(codestr) as mod:
            self.assertInBytecode(mod.flip, "PRIMITIVE_COMPARE_OP", PRIM_OP_EQ_INT)

            self.assertEqual(mod.flip(0), 1)
            self.assertEqual(mod.flip(1), 0)

    def test_function_returns_enum(self):
        codestr = """
        from __static__ import Enum

        class Sign(Enum):
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

            self.assertEqual(mod.sign(0), 0)
            self.assertEqual(mod.sign(-42.0), -1)
            self.assertEqual(mod.sign(42.0), 1)

    def test_pass_enum_between_static_functions(self):
        codestr = """
        from __static__ import Enum

        class Bit(Enum):
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
            self.assertEqual(mod.bitwise_nor(0, 0), 1)
            self.assertEqual(mod.bitwise_nor(0, 1), 0)
            self.assertEqual(mod.bitwise_nor(1, 0), 0)
            self.assertEqual(mod.bitwise_nor(1, 1), 0)
