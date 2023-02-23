from compiler.pycodegen import PythonCodeGenerator
from types import MemberDescriptorType

from unittest import skip

from .common import StaticTestBase, type_mismatch

SHADOWCODE_REPETITIONS = 100


class StaticFieldTests(StaticTestBase):
    def test_slotification(self):
        codestr = """
            class C:
                x: "unknown_type"
        """
        with self.in_module(codestr) as mod:
            C = mod.C
            self.assertEqual(type(C.x), MemberDescriptorType)

    def test_slotification_init(self):
        codestr = """
            class C:
                x: "unknown_type"
                def __init__(self):
                    self.x = 42
        """
        with self.in_module(codestr) as mod:
            C = mod.C
            self.assertEqual(type(C.x), MemberDescriptorType)

    def test_slotification_init_redeclared(self):
        self.type_error(
            """
            class C:
                x: "unknown_type"
                def __init__(self):
                    self.x: "unknown_type" = 42
            """,
            r"Cannot re-declare member 'x' in '<module>.C'",
            at="self.x:",
        )

    def test_slotification_typed(self):
        codestr = """
            class C:
                x: int
        """
        with self.in_module(codestr) as mod:
            C = mod.C
            self.assertNotEqual(type(C.x), MemberDescriptorType)

    def test_slotification_init_typed(self):
        codestr = """
            class C:
                x: int
                def __init__(self):
                    self.x = 42
        """
        with self.in_module(codestr) as mod:
            C = mod.C
            self.assertNotEqual(type(C.x), MemberDescriptorType)
            x = C()
            self.assertEqual(x.x, 42)
            with self.assertRaisesRegex(
                TypeError, "expected 'int', got 'str' for attribute 'x'"
            ) as e:
                x.x = "abc"

    def test_slotification_init_typed_redeclared(self):
        self.type_error(
            """
            class C:
                x: int
                def __init__(self):
                    self.x: int = 42
            """,
            r"Cannot re-declare member 'x' in '<module>\.C'",
            at="self.x",
        )

    def test_slotification_conflicting_types(self):
        self.type_error(
            """
            class C:
                x: object
                def __init__(self):
                    self.x: int = 42
            """,
            r"Cannot re-declare member 'x' in '<module>\.C'",
            at="self.x",
        )

    def test_slotification_conflicting_types_imported(self):
        self.type_error(
            """
            from typing import Optional

            class C:
                x: Optional[int]
                def __init__(self):
                    self.x: Optional[str] = "foo"
            """,
            r"Cannot re-declare member 'x' in '<module>\.C'",
            at="self.x",
        )

    def test_slotification_conflicting_members(self):
        self.type_error(
            """
            class C:
                def x(self): pass
                x: object
            """,
            r"slot conflicts with other member x in Type\[<module>.C\]",
            at="x: object",
        )

    def test_slotification_conflicting_function(self):
        self.type_error(
            """
            class C:
                x: object
                def x(self): pass
            """,
            r"function conflicts with other member x in Type\[<module>.C\]",
            at="def x(self):",
        )

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
        with self.in_module(codestr) as mod:
            D = mod.D
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
        with self.in_module(codestr) as mod:
            with self.assertRaises(AttributeError) as e:
                f, C = mod.f, mod.C
                f(C())

        self.assertEqual(e.exception.args[0], "'C' object has no attribute 'x'")

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
            C = mod.C
            x = C()
            self.assertEqual(x.f(), 1)
            x = C(False)
            self.assertEqual(x.f(), 0)
            self.assertInBytecode(C.f, "LOAD_FIELD", (mod.__name__, "C", "value"))

    def test_aligned_subclass_field(self):
        codestr = """
            from __static__ import cbool

            class Parent:
                def __init__(self):
                    self.running: cbool = False

            class Child(Parent):
                def __init__(self):
                    Parent.__init__(self)
                    self.end = "bloop"
        """

        with self.in_module(codestr) as mod:
            Child = mod.Child
            self.assertInBytecode(
                Child.__init__, "STORE_FIELD", (mod.__name__, "Child", "end")
            )
            for i in range(SHADOWCODE_REPETITIONS):
                c = Child()
                self.assertEqual(c.end, "bloop")

    def test_error_incompat_assign_local(self):
        codestr = """
            class C:
                def __init__(self):
                    self.x = None

                def f(self):
                    x: "C" = self.x
        """
        with self.in_module(codestr) as mod:
            C = mod.C
            with self.assertRaisesRegex(TypeError, "expected 'C', got 'NoneType'"):
                C().f()

    def test_primitive_field_leaked_type(self):
        codestr = """
            from __static__ import cbool

            insts = []
            class B:
                def __init_subclass__(cls: object):
                    insts.append(cls())

            class D(B):
                x: cbool
        """
        with self.assertRaisesRegex(RuntimeError, "type has leaked"):
            with self.in_module(codestr) as mod:
                pass

    def test_assign_implicit_primitive_field(self):
        codestr = """
            from __static__ import cbool

            class B:
                def __init__(self):
                    self.x: cbool = False

            class C(B):
                def __init__(self):
                    self.x = self.f()

                def f(self) -> cbool:
                    return True
        """
        with self.in_module(codestr) as mod:
            self.assertEqual(mod.C().x, True)

    def test_error_incompat_field_non_dynamic(self):
        self.type_error(
            """
            class C:
                def __init__(self):
                    self.x: int = 'abc'
            """,
            type_mismatch("str", "int"),
            at="'abc'",
        )

    def test_error_incompat_field(self):
        codestr = """
            class C:
                def __init__(self):
                    self.x: int = 100

                def f(self, x):
                    self.x = x
        """
        with self.in_module(codestr) as mod:
            C = mod.C
            C().f(42)
            with self.assertRaises(TypeError):
                C().f("abc")

    def test_error_incompat_assign_dynamic(self):
        self.type_error(
            """
            class C:
                x: "C"
                def __init__(self):
                    self.x = None
            """,
            type_mismatch("None", "<module>.C"),
            at="self.x",
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
            f, C = mod.f, mod.C
            self.assertEqual(f(C(3)), 3)
            self.assertInBytecode(f, "LOAD_FIELD", ((mod.__name__, "C", "x")))

    def test_annotated_instance_var(self):
        codestr = """
            class C:
                def __init__(self):
                    self.x: str = 'abc'
        """
        code = self.compile(codestr, modname="test_annotated_instance_var")
        # get C from module, and then get __init__ from C
        code = self.find_code(self.find_code(code), "__init__")
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
            C = mod.C
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
            C = mod.C
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
        with self.in_module(codestr) as mod:
            C = mod.C
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
        with self.in_module(codestr) as mod:
            C = mod.C
            a = C()
            self.assertEqual(a.f(), 42)

    def test_class_ann_assign_with_value_conflict_init(self):
        self.type_error(
            """
            from typing import ClassVar

            class C:
                X: ClassVar[int] = 42
                def __init__(self):
                    self.X = 42
            """,
            r"Cannot assign to classvar 'X' on '<module>\.C' instance",
            at="self.X =",
        )

    def test_class_ann_assign_with_value_conflict(self):
        self.type_error(
            """
            from typing import ClassVar

            class C:
                X: ClassVar[int] = 42
                X: int
            """,
            r"Cannot re-declare member 'X' in '<module>\.C'",
            at="X: int",
        )

    def test_class_ann_assign_with_value_conflict_2(self):
        self.type_error(
            """
            from typing import ClassVar

            class C:
                X: int
                X: ClassVar[int] = 42
            """,
            r"Cannot re-declare member 'X' in '<module>\.C'",
            at="X: ClassVar[int]",
        )

    def test_annotated_classvar(self):
        codestr = """
            from typing import ClassVar

            class C:
                x: ClassVar[int] = 3

            def f() -> int:
                return C.x

            def g(c: C) -> int:
                return c.x
        """
        with self.in_module(codestr) as mod:
            f, g, C = mod.f, mod.g, mod.C
            self.assertEqual(f(), 3)
            self.assertEqual(g(C()), 3)
            self.assertNotInBytecode(f, "CAST")
            self.assertNotInBytecode(g, "CAST")

    def test_classvar_no_assign_from_instance(self):
        self.type_error(
            """
            from typing import ClassVar

            class C:
                x: ClassVar[int] = 3

            def f(c: C):
                c.x = 4
            """,
            r"Cannot assign to classvar 'x' on '<module>.C' instance",
            at="c.x = 4",
        )

    def test_bad_classvar_arg(self):
        self.type_error(
            """
            from typing import ClassVar

            def f(x: ClassVar[int]):
                pass
            """,
            r"ClassVar is allowed only in class attribute annotations.",
            at="ClassVar[int]",
        )

    def test_bad_classvar_local(self):
        self.type_error(
            """
            from typing import ClassVar

            def f():
                x: ClassVar[int] = 3
            """,
            r"ClassVar is allowed only in class attribute annotations.",
            at="x: ClassVar[int]",
        )

    def test_bad_initvar_arg(self):
        self.type_error(
            """
            from dataclasses import InitVar

            def f(x: InitVar[int]):
                pass
            """,
            r"InitVar is allowed only in class attribute annotations.",
            at="InitVar[int]",
        )

    def test_bad_initvar_local(self):
        self.type_error(
            """
            from dataclasses import InitVar

            def f():
                x: InitVar[int] = 3
            """,
            r"InitVar is allowed only in class attribute annotations.",
            at="x: InitVar[int]",
        )

    def test_final_attr(self):
        codestr = """
        from typing import Final

        class C:
            x: Final[int]

            def __init__(self):
                self.x = 3
        """
        with self.in_module(codestr) as mod:
            self.assertEqual(mod.C().x, 3)

    def test_final_attr_decl_uninitialized(self):
        self.type_error(
            """
            from typing import Final

            class C:
                x: Final
            """,
            r"Final attribute not initialized: <module>\.C:x",
            at="x: Final",
        )

    def test_final_classvar_reinitialized(self):
        self.type_error(
            """
            from typing import Final

            class C:
                x: Final[int] = 3
                x = 4
            """,
            r"Cannot assign to a Final variable",
            at="x = 4",
        )

    def test_final_classvar_reinitialized_externally(self):
        self.type_error(
            """
            from typing import Final

            class C:
                x: Final[int] = 3

            C.x = 4
            """,
            r"Cannot assign to a Final attribute of <module>\.C:x",
            at="C.x = 4",
        )

    def test_final_attr_reinitialized_externally_on_class(self):
        self.type_error(
            """
            from typing import Final

            class C:
                x: Final[int]

                def __init__(self):
                    self.x = 3

            C.x = 4
            """,
            type_mismatch("Literal[4]", "types.MemberDescriptorType"),
            at="C.x = 4",
        )

    def test_final_attr_reinitialized_externally_on_instance(self):
        self.type_error(
            """
            from typing import Final

            class C:
                x: Final[int]

                def __init__(self):
                    self.x = 3

            c: C = C()
            c.x = 4
            """,
            r"Cannot assign to a Final attribute of <module>\.C:x",
            at="c.x = 4",
        )

    def test_final_classvar_reinitialized_in_instance(self):
        self.type_error(
            """
            from typing import Final

            class C:
                x: Final[int] = 3

            C().x = 4
            """,
            r"Cannot assign to classvar 'x' on '<module>\.C' instance",
            at="C().x = 4",
        )

    def test_final_classvar_reinitialized_in_method(self):
        self.type_error(
            """
            from typing import Final

            class C:
                x: Final[int] = 3

                def something(self) -> None:
                    self.x = 4
            """,
            r"Cannot assign to classvar 'x' on '<module>\.C' instance",
            at="self.x = 4",
        )

    def test_final_classvar_reinitialized_in_subclass_without_annotation(self):
        self.type_error(
            """
            from typing import Final

            class C:
                x: Final[int] = 3

            class D(C):
                x = 4
            """,
            r"Cannot assign to a Final attribute of <module>\.D:x",
            at="x = 4",
        )

    def test_final_classvar_reinitialized_in_subclass_with_annotation(self):
        self.type_error(
            """
            from typing import Final

            class C:
                x: Final[int] = 3

            class D(C):
                x: Final[int] = 4
            """,
            r"Cannot assign to a Final attribute of <module>\.D:x",
            at="x: Final[int] = 4",
        )

    def test_final_classvar_reinitialized_in_subclass_init(self):
        self.type_error(
            """
            from typing import Final

            class C:
                x: Final[int] = 3

            class D(C):
                def __init__(self):
                    self.x = 4
            """,
            r"Cannot assign to classvar 'x' on '<module>\.D' instance",
            at="self.x = 4",
        )

    def test_final_classvar_reinitialized_in_subclass_init_with_annotation(self):
        self.type_error(
            """
            from typing import Final

            class C:
                x: Final[int] = 3

            class D(C):
                def __init__(self):
                    self.x: Final[int] = 4
            """,
            r"Cannot assign to classvar 'x' on '<module>\.D' instance",
            at="self.x: Final[int] = 4",
        )

    def test_final_attr_reinitialized_in_subclass_init(self):
        self.type_error(
            """
            from typing import Final

            class C:
                x: Final[int]

                def __init__(self) -> None:
                    self.x = 3

            class D(C):
                def __init__(self) -> None:
                    self.x = 4
            """,
            r"Cannot assign to a Final attribute of <module>\.D:x",
            at="self.x = 4",
        )

    def test_nested_classvar_and_final(self):
        """Per PEP 591, class-level final assignments are always ClassVar."""
        self.type_error(
            f"""
            from typing import ClassVar, Final

            class C:
                x: Final[ClassVar[int]] = 3
            """,
            r"Class Finals are inferred ClassVar; do not nest with Final",
            at="ClassVar[int]",
        )

    def test_incompatible_attr_override(self):
        self.type_error(
            """
            class A:
                x: int

            class B(A):
                x: str
            """,
            r"Cannot change type of inherited attribute \(inherited type 'int'\)",
            at="x: str",
        )

    def test_mutable_attr_invariant(self):
        self.type_error(
            """
            class A:
                x: object

            class B(A):
                x: int
            """,
            r"Cannot change type of inherited attribute \(inherited type 'object'\)",
            at="x: int",
        )

    def test_explicit_dict(self):
        codestr = """
            from typing import Any
            class A:
                __dict__: Any

            def get_dict(a: A):
                return a.__dict__
        """
        with self.in_module(codestr) as mod:
            a = mod.A()
            a.foo = 42
            self.assertEqual(mod.get_dict(a), {"foo": 42})

    def test_explicit_weakref(self):
        codestr = """
            from typing import Any
            from weakref import ref

            class A:
                __weakref__: Any

            def get_ref(a: A):
                return ref(a)
        """
        with self.in_module(codestr) as mod:
            a = mod.A()
            self.assertEqual(mod.get_ref(a)(), a)

    def test_multiple_fields_with_nonstatic_base(self):
        non_static = """
        class SomeType:
            pass
        """
        with self.in_module(non_static, code_gen=PythonCodeGenerator) as nonstatic_mod:
            codestr = f"""
            from __strict__ import mutable
            from {nonstatic_mod.__name__} import SomeType

            @mutable
            class C(SomeType):
                def __init__(self):
                    self.x: str = 'abc'
                    self.y: str = 'foo'

            """
            with self.in_strict_module(codestr) as mod:
                pass

    def test_single_field_with_nonstatic_base(self):
        non_static = """
        class SomeType:
            pass
        """
        with self.in_module(non_static, code_gen=PythonCodeGenerator) as nonstatic_mod:
            codestr = f"""
            from __strict__ import mutable
            from {nonstatic_mod.__name__} import SomeType

            @mutable
            class C(SomeType):
                def __init__(self):
                    self.x: str = 'abc'

            """
            with self.in_strict_module(codestr) as mod:
                pass
