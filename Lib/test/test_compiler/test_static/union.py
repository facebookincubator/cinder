import re

from .common import StaticTestBase
from .tests import bad_ret_type, type_mismatch


class UnionCompilationTests(StaticTestBase):
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
                "type mismatch: Optional[Exact[bool]] received for positional arg 'b', expected Exact[bool]"
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
        self.assertReturns(
            """
            from typing import Union
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

    def test_union_can_assign_to_broader_union(self):
        self.assertReturns(
            """
            from typing import Union
            class B:
                pass

            def f(x: int, y: str) -> Union[int, str, B]:
                return x or y
            """,
            "Union[int, str]",
        )

    def test_union_can_assign_to_same_union(self):
        self.assertReturns(
            """
            from typing import Union

            def f(x: int, y: str) -> Union[int, str]:
                return x or y
            """,
            "Union[int, str]",
        )

    def test_union_can_assign_from_individual_element(self):
        self.assertReturns(
            """
            from typing import Union

            def f(x: int) -> Union[int, str]:
                return x
            """,
            "int",
        )

    def test_union_cannot_assign_from_broader_union(self):
        # TODO this should be a type error, but can't be safely
        # until we have runtime checking for unions
        self.assertReturns(
            """
            from typing import Union
            class B: pass

            def f(x: int, y: str, z: B) -> Union[int, str]:
                return x or y or z
            """,
            "Union[int, str, foo.B]",
        )

    def test_union_simplify_to_single_type(self):
        self.assertReturns(
            """
            from typing import Union

            def f(x: int, y: int) -> int:
                return x or y
            """,
            "int",
        )

    def test_union_simplify_related(self):
        self.assertReturns(
            """
            from typing import Union
            class B: pass
            class C(B): pass

            def f(x: B, y: C) -> B:
                return x or y
            """,
            "foo.B",
        )

    def test_union_flatten_nested(self):
        self.assertReturns(
            """
            from typing import Union
            class B: pass

            def f(x: int, y: str, z: B):
                return x or (y or z)
            """,
            "Union[int, str, foo.B]",
        )

    def test_union_deep_simplify(self):
        self.assertReturns(
            """
            from typing import Union

            def f(x: int, y: None) -> int:
                z = (x or x) or (y or y) or (x or x)
                if z is None:
                    return 1
                return z
            """,
            "int",
        )

    def test_union_dynamic_element(self):
        self.assertReturns(
            """
            from somewhere import unknown

            def f(x: int, y: unknown):
                return x or y
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
            modname="foo.py",
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
            modname="foo",
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
            r"unsupported operand type(s) for |: Type\[Exact\[int\]\] and Exact\[str\]",
        )

    def test_union_or_syntax_annotation_bad_type(self):
        # TODO given that len is not unknown/dynamic, but is a known object
        # with type that is invalid in this position, this should really be an
        # error. But the current form of `resolve_annotations` doesn't let us
        # distinguish between unknown/dynamic and bad type. So for now we just
        # let this go as dynamic.
        self.assertReturns(
            """
            def f(x: len | int) -> int:
                return x
            """,
            "dynamic",
        )

    def test_union_attr(self):
        self.assertReturns(
            """
            class A:
                attr: int

            class B:
                attr: str

            def f(x: A, y: B):
                z = x or y
                return z.attr
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
        self.assertReturns(
            """
            from __static__ import CheckedDict

            def f(x: CheckedDict[int, int], y: CheckedDict[int, str]):
                return (x or y)[0]
            """,
            "Union[int, str]",
        )

    def test_union_unaryop(self):
        self.assertReturns(
            """
            def f(x: int, y: complex):
                return -(x or y)
            """,
            "Union[int, complex]",
        )

    def test_union_isinstance_reverse_narrow(self):
        self.assertReturns(
            """
            def f(x: int, y: str):
                z = x or y
                if isinstance(z, str):
                    return 1
                return z
            """,
            "int",
        )

    def test_union_isinstance_reverse_narrow_supertype(self):
        self.assertReturns(
            """
            class A: pass
            class B(A): pass

            def f(x: int, y: B):
                o = x or y
                if isinstance(o, A):
                    return 1
                return o
            """,
            "int",
        )

    def test_union_isinstance_reverse_narrow_other_union(self):
        self.assertReturns(
            """
            class A: pass
            class B: pass
            class C: pass

            def f(x: A, y: B, z: C):
                o = x or y or z
                if isinstance(o, A | B):
                    return 1
                return o
            """,
            "foo.C",
        )

    def test_union_not_isinstance_narrow(self):
        self.assertReturns(
            """
            def f(x: int, y: str):
                o = x or y
                if not isinstance(o, int):
                    return 1
                return o
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
            self.assertInBytecode(f, "CHECK_ARGS", ())
            # so we do have to check the return value
            self.assertInBytecode(f, "CAST", ("builtins", "int"))
            # runtime type error comes from return, not argument
            with self.assertRaisesRegex(TypeError, "expected 'int', got 'list'"):
                f([])



if __name__ == "__main__":
    unittest.main()
