import inspect
from compiler.static import StaticCodeGenerator
from compiler.static.errors import TypedSyntaxError
from types import MemberDescriptorType

from .common import StaticTestBase


class StaticFieldTests(StaticTestBase):
    def test_slotification(self):
        codestr = """
            class C:
                x: "unknown_type"
        """
        code = self.compile(codestr, modname="foo")
        C = self.run_code(codestr, StaticCodeGenerator)["C"]
        self.assertEqual(type(C.x), MemberDescriptorType)

    def test_slotification_init(self):
        codestr = """
            class C:
                x: "unknown_type"
                def __init__(self):
                    self.x = 42
        """
        code = self.compile(codestr, modname="foo")
        C = self.run_code(codestr, StaticCodeGenerator)["C"]
        self.assertEqual(type(C.x), MemberDescriptorType)

    def test_slotification_ann_init(self):
        codestr = """
            class C:
                x: "unknown_type"
                def __init__(self):
                    self.x: "unknown_type" = 42
        """
        code = self.compile(codestr, modname="foo")
        C = self.run_code(codestr, StaticCodeGenerator)["C"]
        self.assertEqual(type(C.x), MemberDescriptorType)

    def test_slotification_typed(self):
        codestr = """
            class C:
                x: int
        """
        C = self.run_code(codestr, StaticCodeGenerator)["C"]
        self.assertNotEqual(type(C.x), MemberDescriptorType)

    def test_slotification_init_typed(self):
        codestr = """
            class C:
                x: int
                def __init__(self):
                    self.x = 42
        """
        with self.in_module(codestr) as mod:
            C = mod["C"]
            self.assertNotEqual(type(C.x), MemberDescriptorType)
            x = C()
            self.assertEqual(x.x, 42)
            with self.assertRaisesRegex(
                TypeError, "expected 'int', got 'str' for attribute 'x'"
            ) as e:
                x.x = "abc"

    def test_slotification_ann_init_typed(self):
        codestr = """
            class C:
                x: int
                def __init__(self):
                    self.x: int = 42
        """
        C = self.run_code(codestr, StaticCodeGenerator)["C"]
        self.assertNotEqual(type(C.x), MemberDescriptorType)

    def test_slotification_conflicting_types(self):
        codestr = """
            class C:
                x: object
                def __init__(self):
                    self.x: int = 42
        """
        with self.assertRaisesRegex(
            TypedSyntaxError,
            r"conflicting type definitions for slot x in Type\[foo.C\]",
        ):
            self.compile(codestr, modname="foo")

    def test_slotification_conflicting_types_imported(self):
        self.type_error(
            """
            from typing import Optional

            class C:
                x: Optional[int]
                def __init__(self):
                    self.x: Optional[str] = "foo"
            """,
            r"conflicting type definitions for slot x in Type\[<module>.C\]",
        )

    def test_slotification_conflicting_members(self):
        codestr = """
            class C:
                def x(self): pass
                x: object
        """
        with self.assertRaisesRegex(
            TypedSyntaxError, r"slot conflicts with other member x in Type\[foo.C\]"
        ):
            self.compile(codestr, modname="foo")

    def test_slotification_conflicting_function(self):
        codestr = """
            class C:
                x: object
                def x(self): pass
        """
        with self.assertRaisesRegex(
            TypedSyntaxError, r"function conflicts with other member x in Type\[foo.C\]"
        ):
            self.compile(codestr, modname="foo")

    def test_slot_inheritance(self):
        codestr = """
            class B:
                def __init__(self):
                    self.x = 42

                def f(self):
                    return self.x

            class D(B):
                def __init__(self):
                    self.x = 100
        """
        code = self.compile(codestr, modname="foo")
        with self.in_module(codestr) as mod:
            D = mod["D"]
            inst = D()
            self.assertEqual(inst.f(), 100)

    def test_del_slot(self):
        codestr = """
        class C:
            x: object

        def f(a: C):
            del a.x
        """
        code = self.compile(codestr, modname="foo")
        code = self.find_code(code, name="f")
        self.assertInBytecode(code, "DELETE_ATTR", "x")

    def test_uninit_slot(self):
        codestr = """
        class C:
            x: object

        def f(a: C):
            return a.x
        """
        code = self.compile(codestr, modname="foo")
        code = self.find_code(code, name="f")
        with self.in_module(codestr) as mod:
            with self.assertRaises(AttributeError) as e:
                f, C = mod["f"], mod["C"]
                f(C())

        self.assertEqual(e.exception.args[0], "x")

    def test_conditional_init(self):
        codestr = f"""
            from __static__ import box, int64

            class C:
                def __init__(self, init=True):
                    if init:
                        self.value: int64 = 1

                def f(self) -> int:
                    return box(self.value)
        """

        with self.in_module(codestr) as mod:
            C = mod["C"]
            x = C()
            self.assertEqual(x.f(), 1)
            x = C(False)
            self.assertEqual(x.f(), 0)
            self.assertInBytecode(C.f, "LOAD_FIELD", (mod["__name__"], "C", "value"))

    def test_error_incompat_assign_local(self):
        codestr = """
            class C:
                def __init__(self):
                    self.x = None

                def f(self):
                    x: "C" = self.x
        """
        with self.in_module(codestr) as mod:
            C = mod["C"]
            with self.assertRaisesRegex(TypeError, "expected 'C', got 'NoneType'"):
                C().f()

    def test_error_incompat_field_non_dynamic(self):
        codestr = """
            class C:
                def __init__(self):
                    self.x: int = 'abc'
        """
        with self.assertRaises(TypedSyntaxError):
            self.compile(codestr)

    def test_error_incompat_field(self):
        codestr = """
            class C:
                def __init__(self):
                    self.x: int = 100

                def f(self, x):
                    self.x = x
        """
        with self.in_module(codestr) as mod:
            C = mod["C"]
            C().f(42)
            with self.assertRaises(TypeError):
                C().f("abc")

    def test_error_incompat_assign_dynamic(self):
        with self.assertRaises(TypedSyntaxError):
            code = self.compile(
                """
            class C:
                x: "C"
                def __init__(self):
                    self.x = None
            """
            )

    def test_instance_var_annotated_on_class(self):
        codestr = """
            class C:
                x: int

                def __init__(self, x):
                    self.x = x

            def f(c: C) -> int:
                return c.x
        """
        with self.in_module(codestr) as mod:
            f, C = mod["f"], mod["C"]
            self.assertEqual(f(C(3)), 3)
            self.assertInBytecode(f, "LOAD_FIELD", ((mod["__name__"], "C", "x")))

    def test_annotated_instance_var(self):
        codestr = """
            class C:
                def __init__(self):
                    self.x: str = 'abc'
        """
        code = self.compile(codestr, modname="test_annotated_instance_var")
        # get C from module, and then get __init__ from C
        code = self.find_code(self.find_code(code))
        self.assertInBytecode(code, "STORE_FIELD")

    def test_load_store_attr_value(self):
        codestr = """
            class C:
                x: int

                def __init__(self, value: int):
                    self.x = value

                def f(self):
                    return self.x
        """
        code = self.compile(codestr, modname="foo")
        init = self.find_code(self.find_code(code), "__init__")
        self.assertInBytecode(init, "STORE_FIELD")
        f = self.find_code(self.find_code(code), "f")
        self.assertInBytecode(f, "LOAD_FIELD")
        with self.in_module(codestr) as mod:
            C = mod["C"]
            a = C(42)
            self.assertEqual(a.f(), 42)

    def test_load_store_attr(self):
        codestr = """
            class C:
                x: "C"

                def __init__(self):
                    self.x = self

                def g(self):
                    return 42

                def f(self):
                    return self.x.g()
        """
        with self.in_module(codestr) as mod:
            C = mod["C"]
            a = C()
            self.assertEqual(a.f(), 42)

    def test_load_store_attr_init(self):
        codestr = """
            class C:
                def __init__(self):
                    self.x: C = self

                def g(self):
                    return 42

                def f(self):
                    return self.x.g()
        """
        code = self.compile(codestr, modname="foo")

        with self.in_module(codestr) as mod:
            C = mod["C"]
            a = C()
            self.assertEqual(a.f(), 42)

    def test_load_store_attr_init_no_ann(self):
        codestr = """
            class C:
                def __init__(self):
                    self.x = self

                def g(self):
                    return 42

                def f(self):
                    return self.x.g()
        """
        code = self.compile(codestr, modname="foo")

        with self.in_module(codestr) as mod:
            C = mod["C"]
            a = C()
            self.assertEqual(a.f(), 42)

    def test_class_ann_assign_with_value(self):
        codestr = """
            class C:
                X: int = 42
        """
        code = self.compile(codestr, modname="foo")

        with self.in_module(codestr) as mod:
            C = mod["C"]
            self.assertEqual(C().X, 42)

    def test_class_ann_assign_with_value_conflict_init(self):
        self.type_error(
            """
            class C:
                X: int = 42
                def __init__(self):
                    self.X = 42
            """,
            r"Conflicting class vs instance variable",
        )

    def test_class_ann_assign_with_value_conflict(self):
        self.type_error(
            """
            class C:
                X: int = 42
                X: int
            """,
            r"Conflicting class vs instance variable",
        )

    def test_class_ann_assign_with_value_conflict_2(self):
        self.type_error(
            """
            class C:
                X: int
                X: int = 42
            """,
            r"Conflicting class vs instance variable",
        )
