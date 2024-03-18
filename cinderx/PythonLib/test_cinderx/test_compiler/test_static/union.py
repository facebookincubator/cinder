import re
import unittest
from unittest import skip, skipIf

from cinderx.compiler.static import StaticCodeGenerator
from cinderx.compiler.static.compiler import Compiler
from cinderx.compiler.static.types import TypeEnvironment

from .common import bad_ret_type, StaticTestBase


class UnionCompilationTests(StaticTestBase):
    type_env: TypeEnvironment = TypeEnvironment()

    def test_static_incompatible_union(self) -> None:
        codestr = """
        from typing import Optional
        def expects_bool(b: bool) -> None:
            return
        def foo(x: Optional[bool]) -> None:
            expects_bool(x)
        """
        self.type_error(
            codestr,
            re.escape(
                "type mismatch: Optional[bool] received for positional arg 'b', expected bool"
            ),
        )

    def test_static_compatible_union(self) -> None:
        codestr = """
        from typing import Optional
        def expects_object(b: object) -> None:
            return
        def foo(x: Optional[bool]) -> None:
            expects_object(x)
        """
        with self.in_module(codestr) as mod:
            foo = mod.foo
            self.assertEqual(foo(True), None)
            self.assertEqual(foo(None), None)

    def test_static_assigning_union_of_subclasses_to_base(self) -> None:
        codestr = """
        class C:
            pass
        class D(C):
            pass
        class E(C):
            pass
        def expects_object(c: C) -> None:
            return
        def foo(d_or_e: D | E) -> None:
            expects_object(d_or_e)
        """
        with self.in_module(codestr) as mod:
            foo = mod.foo

    def test_optional_union_syntax(self):
        self.revealed_type(
            """
            from typing import Optional, Union
            class B: pass
            class C(B): pass

            def f(x: Union[int, None]) -> int:
                # can assign None
                y: Optional[int] = None
                # can assign subclass
                z: Optional[B] = C()
                # can narrow
                if x is None:
                    return 1
                reveal_type(x)
                return x
            """,
            "int",
        )

    def test_optional_union_syntax_error(self):
        self.type_error(
            """
            from typing import Union

            def f(x: Union[int, None]) -> int:
                return x
            """,
            bad_ret_type("Optional[int]", "int"),
        )

    def test_union_can_assign_from(self):
        compiler = Compiler(StaticCodeGenerator)
        u1 = compiler.type_env.get_generic_type(
            self.type_env.union,
            (self.type_env.int, self.type_env.str),
        )
        u2 = compiler.type_env.get_generic_type(
            self.type_env.union,
            (self.type_env.int, self.type_env.str, self.type_env.none),
        )
        self.assertTrue(u2.can_assign_from(u1))
        self.assertFalse(u1.can_assign_from(u2))
        self.assertTrue(u1.can_assign_from(u1))
        self.assertTrue(u2.can_assign_from(u2))
        self.assertTrue(u1.can_assign_from(self.type_env.int))

    def test_union_simplify_to_single_type(self):
        self.revealed_type(
            """
            from typing import Union

            def f(x: int, y: int) -> int:
                reveal_type(x or y)
            """,
            "int",
        )

    def test_union_simplify_related(self):
        self.revealed_type(
            """
            from typing import Union
            class B: pass
            class C(B): pass

            def f(x: B, y: C) -> B:
                reveal_type(x or y)
            """,
            "<module>.B",
        )

    def test_union_flatten_nested(self):
        self.revealed_type(
            """
            from typing import Union
            class B: pass

            def f(x: int, y: str, z: B):
                reveal_type(x or (y or z))
            """,
            "Union[int, str, <module>.B]",
        )

    def test_union_deep_simplify(self):
        self.revealed_type(
            """
            from typing import Union

            def f(x: int, y: None):
                reveal_type((x or x) or (y or y) or (x or x))
            """,
            "Optional[int]",
        )

    def test_union_dynamic_element(self):
        self.revealed_type(
            """
            from somewhere import unknown

            def f(x: int, y: unknown):
                reveal_type(x or y)
            """,
            "dynamic",
        )

    def test_union_or_syntax(self):
        self.type_error(
            """
            def f(x) -> int:
                if isinstance(x, int|str):
                    return x
                return 1
            """,
            bad_ret_type("Union[int, str]", "int"),
        )

    def test_union_or_syntax_none(self):
        self.type_error(
            """
            def f(x) -> int:
                if isinstance(x, int|None):
                    return x
                return 1
            """,
            bad_ret_type("Optional[int]", "int"),
        )

    def test_union_or_syntax_builtin_type(self):
        self.compile(
            """
            from typing import Iterator
            def f(x) -> int:
                if isinstance(x, bytes | Iterator[bytes]):
                    return 1
                return 2
            """,
        )

    def test_optional_refine_boolop(self):
        self.compile(
            """
            from typing import Optional

            def a(x: bool) -> Optional[int]:
                if x:
                    return None
                else:
                    return 4

            def b() -> int:
                return 5

            def c() -> int:
                return a(True) or b()

            def d() -> int:
                return a(True) or a(False) or b()
            """,
        )

    def test_optional_refine_boolop_fail(self):
        self.type_error(
            """
            from typing import Optional

            def a(x: bool) -> Optional[int]:
                if x:
                    return None
                else:
                    return 4

            def b() -> int:
                return 5

            def c() -> int:
                return b() or a(True)
            """,
            bad_ret_type("Optional[int]", "int"),
        )

    def test_union_or_syntax_none_first(self):
        self.type_error(
            """
            def f(x) -> int:
                if isinstance(x, None|int):
                    return x
                return 1
            """,
            bad_ret_type("Optional[int]", "int"),
        )

    def test_union_or_syntax_annotation(self):
        self.type_error(
            """
            def f(y: int, z: str) -> int:
                x: int|str = y or z
                return x
            """,
            bad_ret_type("Union[int, str]", "int"),
        )

    def test_union_or_syntax_error(self):
        self.type_error(
            """
            def f():
                x = int | "foo"
            """,
            r"unsupported operand type(s) for |: Type\[int\] and str",
        )

    def test_union_or_syntax_annotation_bad_type(self):
        # TODO given that len is not unknown/dynamic, but is a known object
        # with type that is invalid in this position, this should really be an
        # error. But the current form of `resolve_annotations` doesn't let us
        # distinguish between unknown/dynamic and bad type. So for now we just
        # let this go as dynamic.
        self.revealed_type(
            """
            def f(x: len | int):
                reveal_type(x)
            """,
            "dynamic",
        )

    def test_union_attr(self):
        self.revealed_type(
            """
            class A:
                attr: int

            class B:
                attr: str

            def f(x: A, y: B):
                z = x or y
                reveal_type(z.attr)
            """,
            "Union[int, str]",
        )

    def test_union_attr_error(self):
        self.type_error(
            """
            class A:
                attr: int

            def f(x: A | None):
                return x.attr
            """,
            re.escape(
                "Optional[<module>.A]: 'NoneType' object has no attribute 'attr'"
            ),
        )

    # TODO add test_union_call when we have Type[] or Callable[] or
    # __call__ support. Right now we have no way to construct a Union of
    # callables that return different types.

    def test_union_call_error(self):
        self.type_error(
            """
            def f(x: int | None):
                return x()
            """,
            re.escape("Optional[int]: 'NoneType' object is not callable"),
        )

    def test_union_subscr(self):
        self.revealed_type(
            """
            from __static__ import CheckedDict

            def f(x: CheckedDict[int, int], y: CheckedDict[int, str]):
                reveal_type((x or y)[0])
            """,
            "Union[int, str]",
        )

    def test_union_unaryop(self):
        self.revealed_type(
            """
            def f(x: int, y: complex):
                reveal_type(-(x or y))
            """,
            "Union[int, complex]",
        )

    def test_union_isinstance_reverse_narrow(self):
        self.revealed_type(
            """
            def f(x: int, y: str):
                z = x or y
                if isinstance(z, str):
                    return 1
                reveal_type(z)
            """,
            "int",
        )

    def test_union_isinstance_reverse_narrow_supertype(self):
        self.revealed_type(
            """
            class A: pass
            class B(A): pass

            def f(x: int, y: B):
                o = x or y
                if isinstance(o, A):
                    return 1
                reveal_type(o)
            """,
            "int",
        )

    def test_union_isinstance_reverse_narrow_other_union(self):
        self.revealed_type(
            """
            class A: pass
            class B: pass
            class C: pass

            def f(x: A, y: B, z: C):
                o = x or y or z
                if isinstance(o, A | B):
                    return 1
                reveal_type(o)
            """,
            "<module>.C",
        )

    def test_union_not_isinstance_narrow(self):
        self.revealed_type(
            """
            def f(x: int, y: str):
                o = x or y
                if not isinstance(o, int):
                    return 1
                reveal_type(o)
            """,
            "int",
        )

    def test_union_no_arg_check(self):
        codestr = """
           def f(x: int | str) -> int:
               return x
        """
        with self.in_module(codestr) as mod:
            f = mod.f
            # no arg check for the union, it's just dynamic
            self.assertEqual(self.get_arg_check_types(f), ())
            # so we do have to check the return value
            self.assertInBytecode(f, "CAST", ("builtins", "int"))
            # runtime type error comes from return, not argument
            with self.assertRaisesRegex(TypeError, "expected 'int', got 'list'"):
                f([])

    def test_union_compare(self):
        codestr = """
            # float is actually int | float
            def f(x: float) -> bool:
                return x > 0
        """
        with self.in_strict_module(codestr) as mod:
            self.assertEqual(mod.f(3), True)
            self.assertEqual(mod.f(3.1), True)
            self.assertEqual(mod.f(-3), False)
            self.assertEqual(mod.f(-3.1), False)

    def test_int_subclass_of_float(self):
        """PEP 484 specifies that ints should be treated as subclasses of floats,
        even though they differ in the runtime."""
        codestr = """
            def takes_float(f: float) -> float:
                return f

            a: int = 1
            x: float = takes_float(a)
        """
        with self.in_module(codestr) as mod:
            self.assertEqual(mod.x, 1)

    def test_float_int_union_error_message(self):
        codestr = """
            class MyFloat(float):
                pass

            def f(x: int) -> int:
                y = 1.0
                if x:
                    y = MyFloat("1.5")
                z = x or y
                return z
        """
        self.type_error(codestr, bad_ret_type("float", "int"))

    def test_cast_int_to_float(self):
        codestr = """
            from __static__ import double

            def f(x: float) -> double:
                return double(x)
        """

        class MyInt(int):
            pass

        with self.in_module(codestr) as mod:
            self.assertEqual(mod.f(1.0), 1.0)
            for i in range(51):
                self.assertEqual(mod.f(1), 1.0)
                self.assertEqual(mod.f(MyInt(1)), 1.0)

    def test_isinstance_refine_dynamic(self) -> None:
        codestr = f"""
            from something import D

            class B:
                def __init__(self):
                    p: int = 1

            class C:
                def __init__(self, x: B | None) -> None:
                    self.x: B | None = x

                def f(self, y: B | None) -> int:
                    if isinstance(y, D):
                        reveal_type(y)
                    return -1
        """
        self.revealed_type(codestr, "dynamic")


if __name__ == "__main__":
    unittest.main()
