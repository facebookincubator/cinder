import re
from unittest import skip

from .common import StaticTestBase


class InferenceTests(StaticTestBase):
    def test_if_exp_union(self) -> None:
        """If expressions can be inferred as the union of the branches."""
        codestr = """
            def f(x: int) -> None:
                y = x if x else None
                reveal_type(y)
        """
        self.type_error(
            codestr, rf"reveal_type\(y\): 'Optional\[int\]'", at="reveal_type"
        )

    def test_if_exp_same_type(self) -> None:
        codestr = """
            class C: pass

            def foo(a):
                x = C() if a else C()
                reveal_type(x)
        """
        self.type_error(
            codestr, rf"reveal_type\(x\): 'Exact\[<module>.C\]'", at="reveal_type"
        )

    def test_type_widened_in_while_loop(self) -> None:
        for loc, typ in [("inside", "Literal[1]"), ("after", "Optional[Literal[1]]")]:
            with self.subTest(loc=loc, typ=typ):
                if loc == "inside":
                    inner = "reveal_type(x)"
                    after = ""
                else:
                    inner = ""
                    after = "reveal_type(x)"
                codestr = f"""
                    def f() -> int:
                        x: int | None = None
                        while True:
                            if x is not None:
                                {inner}
                                return x
                            x = 1
                        {after}
                """
                self.type_error(codestr, rf"reveal_type\(x\): '{re.escape(typ)}'")

    def test_type_widened_in_for_loop(self) -> None:
        for loc, typ in [("inside", "Literal[1]"), ("after", "Optional[Literal[1]]")]:
            with self.subTest(loc=loc, typ=typ):
                if loc == "inside":
                    inner = "reveal_type(x)"
                    after = ""
                else:
                    inner = ""
                    after = "reveal_type(x)"
                codestr = f"""
                    def f() -> int:
                        x: int | None = None
                        for i in [0, 1, 2]:
                            if x is not None:
                                {inner}
                                return x
                            x = 1
                        {after}
                """
                self.type_error(codestr, rf"reveal_type\(x\): '{re.escape(typ)}'")

    def test_while_guard_assigned_in_loop(self) -> None:
        for loc, typ in [("inside", "int"), ("after", "Optional[int]")]:
            with self.subTest(loc=loc, typ=typ):
                if loc == "inside":
                    inner = "reveal_type(x)"
                    after = ""
                else:
                    inner = ""
                    after = "reveal_type(x)"
                codestr = f"""
                    def f():
                        x: int | None = 3

                        while x is not None:
                            {inner}
                            x = x + 1
                        {after}
                """
                self.type_error(codestr, rf"reveal_type\(x\): '{re.escape(typ)}'")

    def test_while_guard_assigned_in_loop_need_narrowed_in_loop(self) -> None:
        codestr = """
            from __future__ import annotations

            class C:
                def meth(self) -> C | None:
                    return self

            def f(c: C):
                c2 = c.meth()
                while c2 is not None:
                    reveal_type(c2)
                    c2 = c2.meth()
        """
        self.type_error(codestr, r"reveal_type\(c2\): '<module>.C'")

    def test_while_guard_assigned_in_loop_need_narrowed_in_loop_no_initial_effect(
        self,
    ) -> None:
        codestr = """
            from __future__ import annotations

            class C:
                def meth(self) -> C | None:
                    return self

            def f(c: C):
                c2 = c
                while c2 is not None:
                    reveal_type(c2)
                    c2 = c2.meth()
        """
        self.type_error(codestr, r"reveal_type\(c2\): '<module>.C'")

    def test_inlined_call_in_loop(self) -> None:
        codestr = """
            from __static__ import inline

            @inline
            def incr(x: int) -> int:
                return x + 1

            def f(x: int) -> int:
                for i in [0, 1, 2]:
                    x = incr(x)
                return x
        """
        with self.in_module(codestr) as mod:
            self.assertEqual(mod.f(1), 4)

    def test_raise_terminal(self) -> None:
        codestr = """
            def f(x: int | None) -> int:
                if x is None:
                    raise ValueError("x is None")
                reveal_type(x)
        """
        self.type_error(codestr, r"reveal_type\(x\): 'int'")

    def test_infinite_loop(self) -> None:
        codestr = """
            def f(x: int | None, y: bool) -> int:
                if y:
                    while True:
                        if x is None:
                            continue
                        return x
                    ret = None
                else:
                    ret = 0
                reveal_type(ret)
        """
        self.type_error(codestr, r"reveal_type\(ret\): 'Literal\[0\]'")

    def test_not_actually_infinite_loop(self) -> None:
        codestr = """
            def f(x: int | None, y: bool) -> int:
                a = True
                if y:
                    while a:
                        if x is None:
                            a = False
                            continue
                        return x
                    ret = None
                else:
                    ret = 0
                reveal_type(ret)
        """
        self.type_error(codestr, r"reveal_type\(ret\): 'Optional\[Literal\[0\]\]'")

    def test_infinite_loop_break(self) -> None:
        codestr = """
            def f(x: int | None, y: bool, z: bool) -> int:
                if y:
                    while True:
                        if x is None:
                            if z:
                                break
                            else:
                                continue
                        return x
                    ret = None
                else:
                    ret = 0
                reveal_type(ret)
        """
        self.type_error(codestr, r"reveal_type\(ret\): 'Optional\[Literal\[0\]\]'")

    def test_loop_that_may_break_is_not_terminal(self) -> None:
        codestr = """
            def f(x: int | None, y: bool) -> int:
                while x is None:
                    if y:
                        break
                    return 0
                reveal_type(x)
        """
        self.type_error(codestr, r"reveal_type\(x\): 'Optional\[int\]'")

    def test_context_manager_returning_literal_false_does_not_suppress(self) -> None:
        codestr = """
            from typing import Literal

            class MyCM:
                def __enter__(self) -> None:
                    pass

                def __exit__(self, exc_type, exc_value, exc_tb) -> Literal[False]:
                    return False

            def g() -> int:
                return 1

            def f(x: int | None) -> int:
                if x is None:
                    with MyCM():
                        return g()
                reveal_type(x)
        """
        self.revealed_type(codestr, "int")

    def test_context_manager_returning_bool_may_suppress(self) -> None:
        codestr = """
            from typing import Literal

            class MyCM:
                def __enter__(self) -> None:
                    pass

                def __exit__(self, exc_type, exc_value, exc_tb) -> bool:
                    return False

            def g() -> int:
                return 1

            def f(x: int | None) -> int:
                if x is None:
                    with MyCM():
                        return g()
                reveal_type(x)
        """
        self.revealed_type(codestr, "Optional[int]")

    def test_unreachable_code(self) -> None:
        codestr = """
            def f():
                while True:
                    return 1
                    x = 3
        """
        with self.in_module(codestr) as mod:
            self.assertEqual(mod.f(), 1)

    def test_unreachable_code_error(self) -> None:
        codestr = """
            def f():
                while True:
                    x: str = "foo"
                    return 1
                    x = 3
        """
        self.type_error(codestr, r"Literal\[3\] cannot be assigned to str", at="x = 3")

    def test_does_not_narrow_if_body(self) -> None:
        codestr = """
            def f(flag: bool):
                x: int | None = None
                if flag:
                    x = 1
                    return
                reveal_type(x)
        """
        self.revealed_type(codestr, "Exact[None]")

    def test_try_except_both_terminal(self) -> None:
        codestr = """
            def f(x: int | None):
                if x is None:
                    try:
                        return x
                    except Exception:
                        raise
                reveal_type(x)
        """
        self.revealed_type(codestr, "int")

    def test_try_terminal_but_not_except(self) -> None:
        codestr = """
            def f(x: int | None):
                if x is None:
                    try:
                        return x
                    except Exception:
                        pass
                reveal_type(x)
        """
        self.revealed_type(codestr, "Optional[int]")

    def test_except_else_terminal_but_not_try(self) -> None:
        codestr = """
            def f(x: int | None):
                if x is None:
                    try:
                        pass
                    except Exception:
                        raise
                    else:
                        return 1
                reveal_type(x)
        """
        self.revealed_type(codestr, "int")

    def test_one_except_terminal_but_not_all(self) -> None:
        codestr = """
            def f(x: int | None):
                if x is None:
                    try:
                        return x
                    except Exception:
                        raise
                    except BaseException:
                        pass
                reveal_type(x)
        """
        self.revealed_type(codestr, "Optional[int]")

    def test_try_except_terminal_but_not_finally(self) -> None:
        codestr = """
            def f(x: int | None):
                if x is None:
                    try:
                        return x
                    except Exception:
                        raise
                    finally:
                        pass
                reveal_type(x)
        """
        self.revealed_type(codestr, "int")

    def test_try_finally_terminal_but_not_except(self) -> None:
        codestr = """
            def f(x: int | None):
                if x is None:
                    try:
                        return x
                    except Exception:
                        pass
                    finally:
                        return 0
                reveal_type(x)
        """
        self.revealed_type(codestr, "int")

    def test_only_finally_terminal(self) -> None:
        codestr = """
            def f(x: int | None):
                if x is None:
                    try:
                        pass
                    finally:
                        return 0
                reveal_type(x)
        """
        self.revealed_type(codestr, "int")

    def test_only_finally_terminal_with_except(self) -> None:
        codestr = """
            def f(x: int | None):
                if x is None:
                    try:
                        pass
                    except Exception:
                        pass
                    finally:
                        return 0
                reveal_type(x)
        """
        self.revealed_type(codestr, "int")

    def test_try_finally_both_terminal(self) -> None:
        codestr = """
            def f(x: int | None):
                if x is None:
                    try:
                        return x
                    finally:
                        return 0
                reveal_type(x)
        """
        self.revealed_type(codestr, "int")

    def test_try_finally_except_all_terminal(self) -> None:
        codestr = """
            def f(x: int | None):
                if x is None:
                    try:
                        return x
                    except Exception:
                        raise
                    finally:
                        return 0
                reveal_type(x)
        """
        self.revealed_type(codestr, "int")

    def test_try_except_lesser_terminal(self) -> None:
        codestr = """
            def f(x: int | None, y: bool):
                if y:
                    while x is None:
                        try:
                            return x
                        except Exception:
                            continue
                    ret = None
                else:
                    ret = 0

                reveal_type(ret)
        """
        self.revealed_type(codestr, "Optional[Literal[0]]")

    def test_try_multiple_except_lesser_terminal(self) -> None:
        codestr = """
            def f(x: int | None, y: bool):
                if y:
                    while x is None:
                        try:
                            return x
                        except Exception:
                            continue
                        except BaseException:
                            raise
                    ret = None
                else:
                    ret = 0

                reveal_type(ret)
        """
        self.revealed_type(codestr, "Optional[Literal[0]]")

    def test_try_finally_finally_wins(self) -> None:
        codestr = """
            def f(x: int | None):
                if x is None:
                    try:
                        continue
                    finally:
                        return 0

                reveal_type(x)
        """
        self.revealed_type(codestr, "int")
