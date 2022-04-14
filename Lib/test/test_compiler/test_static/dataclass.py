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
        self.type_error(codestr, r"reveal_type\(C\): 'Type\[dynamic\]'")

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
