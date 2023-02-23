import builtins
from cinder import cached_property
from compiler.pycodegen import PythonCodeGenerator
from unittest import skip
from unittest.mock import Mock, patch

from .common import StaticTestBase


class SlotsWithDefaultTests(StaticTestBase):
    def test_access_from_instance_and_class(self) -> None:
        codestr = """
        class C:
            x: int = 42

        def f():
            c = C()
            return (C.x, c.x)
        """
        with self.in_module(codestr) as mod:
            self.assertNotInBytecode(mod.f, "LOAD_FIELD")
            self.assertEqual(mod.f(), (42, 42))

    def test_nonstatic_access_from_instance_and_class(self) -> None:
        codestr = """
        class C:
            x: int = 42
        """
        with self.in_module(codestr) as mod:
            C = mod.C
            self.assertEqual(C.x, 42)
            self.assertEqual(C().x, 42)

    def test_write_from_instance(self) -> None:
        codestr = """
        class C:
            x: int = 42

        def f():
            c = C()
            c.x = 21
            return (C.x, c.x)
        """
        with self.in_module(codestr) as mod:
            self.assertNotInBytecode(mod.f, "LOAD_FIELD")
            self.assertEqual(mod.f(), (42, 21))

    def test_nonstatic_write_from_instance(self) -> None:
        codestr = """
        class C:
            x: int = 42
        """
        with self.in_module(codestr) as mod:
            C = mod.C
            c = C()
            c.x = 21
            self.assertEqual(C.x, 42)
            self.assertEqual(c.x, 21)

    def test_write_from_class(self) -> None:
        codestr = """
        class C:
            x: int = 42

        def f():
            c = C()
            C.x = 21
            return (C.x, c.x)
        """
        with self.in_module(codestr) as mod:
            self.assertNotInBytecode(mod.f, "LOAD_FIELD")
            self.assertEqual(mod.f(), (21, 21))

    def test_nonstatic_write_from_class(self) -> None:
        codestr = """
        class C:
            x: int = 42
        """
        with self.in_module(codestr) as mod:
            C = mod.C
            c = C()
            C.x = 21
            self.assertEqual(C.x, 21)
            self.assertEqual(c.x, 21)

    def test_write_to_class_after_instance(self) -> None:
        codestr = """
        class C:
            x: int = 42

        def f():
            c = C()
            c.x = 36 # This write will get clobbered when the class gets patched below.
            C.x = 21
            return (C.x, c.x)
        """
        with self.in_module(codestr) as mod:
            self.assertNotInBytecode(mod.f, "LOAD_FIELD")
            # TODO: Ideally, we can recover the behavior of the result being (21, 36).
            self.assertEqual(mod.f(), (21, 21))

    def test_inheritance(self) -> None:
        codestr = """
        class C:
            x: int = 42

        class D(C):
            pass

        def f():
            d = D()
            return (D.x, d.x)
        """
        with self.in_module(codestr) as mod:
            self.assertEqual(mod.f(), (42, 42))

    def test_inheritance_with_override(self) -> None:
        codestr = """
        class C:
            x: int = 1

        class D(C):
            x: int = 3

        def f():
            c = C()
            c.x = 2
            d = D()
            d.x = 4
            return (C.x, c.x, D.x, d.x)
        """
        with self.in_module(codestr) as mod:
            self.assertEqual(mod.f(), (1, 2, 3, 4))

    def test_custom_descriptor_override_preserved(self) -> None:
        codestr = """
        class C:
            x: int = 42

        def f(c: C):
            return c.x
        """
        with self.in_module(codestr) as mod:

            class descr:
                def __get__(self, inst, ctx) -> int:
                    return 21

            class D(mod.C):
                x: int = descr()

            self.assertEqual(mod.f(D()), 21)

    def test_call(self) -> None:
        codestr = """
        class C:
            x: int = 1

        class D(C):
            pass

        def f(c: C):
            return c.x
        """
        with self.in_module(codestr) as mod:
            d = mod.D()
            self.assertEqual(mod.f(d), 1)
            d.x = 2
            self.assertEqual(mod.f(d), 2)

    def test_typed_descriptor_default_value_type_error(self) -> None:
        codestr = """
        class C:
            x: int = 1
        """
        with self.in_module(codestr) as mod:
            c = mod.C()
            with self.assertRaisesRegex(
                TypeError, "expected 'int', got 'str' for attribute 'x'"
            ):
                c.x = "A"

    def test_typed_descriptor_default_value_patching_type_error(self) -> None:
        codestr = """
        class C:
            x: int = 1
        """
        with self.in_module(codestr) as mod:
            with self.assertRaisesRegex(
                TypeError, "Cannot assign a str, because C.x is expected to be a int"
            ):
                mod.C.x = "A"

    def test_nonstatic_inheritance_reads_allowed(self) -> None:
        codestr = """
        class C:
            x: int = 1

        def f(c: C):
           return (type(c).x, c.x)
        """
        with self.in_module(codestr) as mod:

            class D(mod.C):
                x: int = 2

            self.assertEqual(mod.f(D()), (2, 2))

    def test_nonstatic_inheritance_writes_allowed(self) -> None:
        codestr = """
        class C:
            x: int = 1

        def f(c: C):
            initial_x = c.x
            c.x = 2
            return (initial_x, c.x, c.__class__.x)

        """
        with self.in_module(codestr) as mod:

            class D(mod.C):
                x: int = 3

            self.assertEqual(mod.f(D()), (3, 2, 3))

    def test_nonstatic_inheritance_writes_allowed_init_subclass_override(self) -> None:
        codestr = """
        class C:
            x: int = 1
            def __init_subclass__(cls):
                cls.foo = 42

        def f(c: C):
            initial_x = c.x
            c.x = 2
            return (initial_x, c.x, c.__class__.x)

        """
        m = self.compile(codestr)
        with self.in_module(codestr) as mod:

            class D(mod.C):
                x: int = 3

            self.assertEqual(mod.f(D()), (3, 2, 3))
            self.assertEqual(D.foo, 42)

    def test_static_property_override(
        self,
    ) -> None:
        codestr = """
        class C:
            x: int = 1
            def get_x(self):
                return self.x

        class D(C):
            @property
            def x(self) -> int:
                return 2
        """
        with self.in_module(codestr) as mod:
            self.assertEqual(mod.C().get_x(), 1)
            self.assertEqual(mod.D().get_x(), 2)

    def test_static_property_override_bad_type(
        self,
    ) -> None:
        codestr = """
        class C:
            x: int = 1
            def get_x(self):
                return self.x.val

        class D(C):
            @property
            def x(self) -> str:
                return 'abc'
        """

        self.type_error(codestr, "Cannot change type of inherited attribute")

    def test_static_property_override_no_type(
        self,
    ) -> None:
        codestr = """
        class X:
            def __init__(self, val: int):
                self.val = val

            def f(self):
                pass

        class C:
            x: X = X(1)
            def get_x(self):
                return self.x.val

        class D(C):
            @property
            def x(self):
                return 'abc'
        """
        self.type_error(codestr, "Cannot change type of inherited attribute")

    def test_override_property_with_slot(
        self,
    ) -> None:
        codestr = """
        class C:
            @property
            def x(self) -> int:
                return 2
            def get_x(self):
                return self.x

        class D(C):
            x: int = 1
        """
        with self.in_module(codestr) as mod:
            self.assertEqual(mod.C().get_x(), 2)
            self.assertEqual(mod.D().get_x(), 1)

    def test_override_property_with_slot_non_static(
        self,
    ) -> None:
        codestr = """
        class C:
            @property
            def x(self) -> int:
                return 2
            def get_x(self):
                return self.x
        """
        with self.in_module(codestr) as mod:

            class D(mod.C):
                x: int = 1

            self.assertEqual(mod.C().get_x(), 2)
            self.assertEqual(D().get_x(), 1)

    def test_override_property_with_slot_no_value(
        self,
    ) -> None:
        codestr = """
        class C:
            @property
            def x(self) -> int:
                return 2
            def get_x(self):
                return self.x

        class D(C):
            x: int
        """
        with self.in_module(codestr) as mod:
            self.assertEqual(mod.C().get_x(), 2)
            # this differs from the non-static case because we create a slot
            # which never gets initialized.
            with self.assertRaises(AttributeError):
                mod.D().get_x()

    def test_override_property_with_slot_no_value_non_static(
        self,
    ) -> None:
        codestr = """
        class C:
            @property
            def x(self) -> int:
                return 2
            def get_x(self):
                return self.x

        """
        with self.in_module(codestr) as mod:

            class D(mod.C):

                x: int

            self.assertEqual(mod.C().get_x(), 2)
            self.assertEqual(D().get_x(), 2)

    def test_override_property_with_slot_non_static_slots(
        self,
    ) -> None:
        codestr = """
        class C:
            @property
            def x(self) -> int:
                return 2
            def get_x(self):
                return self.x

        """
        with self.in_module(codestr) as mod:

            class D(mod.C):

                __slots__ = "x"

            self.assertEqual(mod.C().get_x(), 2)
            with self.assertRaises(AttributeError):
                D().get_x()

    def test_override_property_with_slot_bad_type(
        self,
    ) -> None:
        codestr = """
        class C:
            @property
            def x(self) -> int:
                return 2
            def get_x(self):
                return self.x.val

        class D(C):
            x: str = 'abc'
        """
        self.type_error(codestr, "Cannot change type of inherited attribute")

    def test_nonstatic_property_override(
        self,
    ) -> None:
        codestr = """
        class C:
            x: int = 1

        def f(c: C):
            return c.x, c.__class__.x
        """
        with self.in_module(codestr) as mod:

            class D(mod.C):
                @property
                def x(self) -> int:
                    return 2

            self.assertEqual(mod.f(mod.C()), (1, 1))
            self.assertEqual(mod.f(D()), (2, D.x))

    def test_nonstatic_property_override_setter(
        self,
    ) -> None:
        codestr = """
        class C:
            x: int = 1

        def f(c: C):
            c.x = 123
            return c.x, c.__class__.x
        """
        with self.in_module(codestr) as mod:

            class D(mod.C):
                @property
                def x(self) -> int:
                    return 2

            self.assertEqual(mod.f(mod.C()), (123, 1))
            with self.assertRaisesRegex(AttributeError, "can't set attribute"):
                mod.f(D())

    def test_nonstatic_cached_property_override(
        self,
    ) -> None:
        codestr = """
        class C:
            x: int = 1

        def f(c: C):
            return c.x, c.__class__.x
        """
        with self.in_module(codestr) as mod:

            class D(mod.C):
                def __init__(self):
                    self.hit_count = 0

                @cached_property
                def x(self) -> int:
                    self.hit_count += 1
                    return 2

            self.assertEqual(mod.f(mod.C()), (1, 1))
            d = D()
            self.assertEqual(d.hit_count, 0)

            self.assertEqual(mod.f(d), (2, D.x))
            self.assertEqual(d.hit_count, 1)

            self.assertEqual(mod.f(d), (2, D.x))
            self.assertEqual(d.hit_count, 1)

    def test_nonstatic_cached_property_override_type_error(
        self,
    ) -> None:
        codestr = """
        class C:
            x: int = 1

        def f(c: C):
            return c.x, c.__class__.x
        """
        with self.in_module(codestr) as mod:

            class D(mod.C):
                @cached_property
                def x(self) -> str:
                    return "A"

            self.assertEqual(mod.f(mod.C()), (1, 1))
            d = D()

            with self.assertRaisesRegex(
                TypeError, "unexpected return type from D.x, expected int, got str"
            ):
                mod.f(d)

    def test_nonstatic_cached_property_override_setter(
        self,
    ) -> None:
        codestr = """
        class C:
            x: int = 1

        def f(c: C):
            c.x = 123
            return c.x, c.__class__.x
        """
        with self.in_module(codestr) as mod:

            class D(mod.C):
                @cached_property
                def x(self) -> int:
                    return 2

            self.assertEqual(mod.f(mod.C()), (123, 1))
            with self.assertRaisesRegex(
                TypeError, "'cached_property' doesn't support __set__"
            ):
                self.assertEqual(mod.f(D()), (2, D.x))

    def test_override_with_slot_without_default(self) -> None:
        codestr = """
        class C:
            x: int = 1

        class D(C):
            def __init__(self):
                self.x = 3

        def f(c: C):
            r1 = c.x
            r2 = c.__class__.x
            c.x = 42
            return r1, r2, c.x, c.__class__.x
        """
        with self.in_module(codestr) as mod:
            self.assertEqual(mod.f(mod.C()), (1, 1, 42, 1))
            self.assertEqual(mod.f(mod.D()), (3, 1, 42, 1))

    def test_override_with_nonstatic_slot(self) -> None:
        codestr = """
        class C:
            x: int = 1

        def f(c: C):
            r1 = c.x
            r2 = c.__class__.x
            c.x = 42
            return r1, r2, c.x, c.__class__.x
        """

        with self.in_module(codestr) as mod:

            class D(mod.C):
                def __init__(self):
                    self.x = 3

            self.assertEqual(mod.f(mod.C()), (1, 1, 42, 1))
            self.assertEqual(mod.f(D()), (3, 1, 42, 1))

    def test_class_patching_allowed(self) -> None:
        codestr = """
        class C:
            x: int = 1

        def f(c: C) -> int:
            return c.x
        """
        with self.in_module(codestr, enable_patching=True) as mod:
            mod.C.x = 42
            c = mod.C()
            self.assertEqual(mod.f(c), 42)

    def test_class_patching_wrong_type(self) -> None:
        codestr = """
        class C:
            x: int = 1

        def f(c: C) -> int:
            return c.x
        """
        with self.in_module(codestr, enable_patching=True) as mod:
            with self.assertRaisesRegex(
                TypeError,
                "Cannot assign a MagicMock, because C.x is expected to be a int",
            ), patch(f"{mod.__name__}.C.x", return_value=1) as mock:
                c = mod.C()

    def test_instance_patching_allowed(self) -> None:
        codestr = """
        class C:
            x: int = 1

        def f(c: C) -> int:
            return c.x

        def g(c: C):
            c.x = 3
        """
        with self.in_module(codestr, enable_patching=True) as mod:
            c = mod.C()
            with patch.object(c, "x", 2):
                self.assertEqual(c.x, 2)
                self.assertEqual(mod.C.x, 1)
                self.assertEqual(mod.f(c), 2)
                mod.g(c)
                self.assertEqual(mod.f(c), 3)
                self.assertEqual(c.x, 3)

    def test_instance_patching_wrong_type(self) -> None:
        codestr = """
        class C:
            x: int = 1

        def f(c: C) -> int:
            return c.x

        def g(c: C):
            c.x = 3
        """
        with self.in_module(codestr, enable_patching=True) as mod:
            c = mod.C()

            with self.assertRaisesRegex(TypeError, "expected 'int', got 'str'"):
                c.x = ""

    def test_type_descriptor_of_dynamic_type(self) -> None:
        non_static = """
        class SomeType:
            pass
        """
        with self.in_module(non_static, code_gen=PythonCodeGenerator) as nonstatic_mod:
            static = f"""
                from dataclasses import dataclass
                from {nonstatic_mod.__name__} import SomeType

                class C:
                    dynamic_field: SomeType = SomeType()
            """
            with self.in_strict_module(static) as static_mod:
                c = static_mod.C()
                ST = nonstatic_mod.SomeType()
                self.assertNotEqual(c.dynamic_field, ST)
                c.dynamic_field = ST
                self.assertEqual(c.dynamic_field, ST)
                self.assertNotEqual(static_mod.C.dynamic_field, ST)

    def test_slot_assigned_conditionally(self):
        codestr = """
        class Parent:
            x: bool = False

        class Child(Parent):

            def __init__(self, flag: bool):
                if flag:
                    self.x = True
        """
        with self.in_module(codestr) as mod:
            c1 = mod.Child(True)
            self.assertTrue(c1.x)

            c1 = mod.Child(False)
            self.assertFalse(c1.x)
