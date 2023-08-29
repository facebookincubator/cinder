from __future__ import annotations

from textwrap import dedent
from typing import final, List, Sequence, Tuple

from cinderx.strictmodule import StrictModuleLoader

from .common import StrictTestBase


@final
class OwnershipTests(StrictTestBase):
    def check_multiple_modules(
        self, modules: Sequence[Tuple[str, str]]
    ) -> Sequence[List[Tuple[str, str, int, int]]]:
        checker = StrictModuleLoader([], "", [], [], True)
        checker.set_force_strict(True)
        errors = []
        for code, name in modules:
            m = checker.check_source(dedent(code), f"{name}.py", name, [])
            errors.append(list(m.errors))
        return errors

    def assertError(
        self,
        modules: Sequence[Tuple[str, str]],
        expected: str,
        **kwargs: object,
    ) -> None:
        errors = self.check_multiple_modules(modules)
        for mod_errors in errors:
            for error in mod_errors:
                if expected in error[0]:  # match substring
                    return
        err_strings = [[e[0] for e in errs] for errs in errors]
        self.assertFalse(True, f"Expected: {expected}\nActual: {err_strings}")

    def test_list_modify(self) -> None:
        code1 = """
            l1 = [1, 2, 3]
        """
        code2 = """
            from m1 import l1
            l1[0] = 2
        """
        self.assertError(
            [(code1, "m1"), (code2, "m2")], "[1,2,3] from module m1 is modified by m2"
        )

    def test_list_append(self) -> None:
        code1 = """
            l1 = [1, 2, 3]
        """
        code2 = """
            from m1 import l1
            l1.append(4)
        """
        self.assertError(
            [(code1, "m1"), (code2, "m2")], "[1,2,3] from module m1 is modified by m2"
        )

    def test_dict_modify(self) -> None:
        code1 = """
            d1 = {1: 2, 3: 4}
        """
        code2 = """
            from m1 import d1
            d1[5] = 6
        """
        self.assertError(
            [(code1, "m1"), (code2, "m2")],
            "{1: 2, 3: 4} from module m1 is modified by m2",
        )

    def test_func_modify(self) -> None:
        code1 = """
            d1 = {1: 2, 3: 4}
        """
        code2 = """
            def f(value):
                value[5] = 1
        """
        code3 = """
            from m1 import d1
            from m2 import f
            f(d1)
        """
        self.assertError(
            [(code1, "m1"), (code2, "m2"), (code3, "m3")],
            "{1: 2, 3: 4} from module m1 is modified by m3",
        )

    def test_decorator_modify(self) -> None:
        code1 = """
            state = [0]
            def dec(func):
                state[0] = state[0] + 1
                return func
        """
        code2 = """
            from m1 import dec
            @dec
            def g():
                pass
        """
        self.assertError(
            [(code1, "m1"), (code2, "m2")], "[0] from module m1 is modified by m2"
        )

    def test_decorator_ok(self) -> None:
        code1 = """
            def dec(cls):
                cls.x = 1
                return cls
        """
        code2 = """
            from m1 import dec
            @dec
            class C:
                x: int = 0
        """
        self.check_multiple_modules([(code1, "m1"), (code2, "m2")])

    def test_dict_ok(self) -> None:
        code1 = """
            def f():
                return {1: 2, 3: 4}
        """
        code2 = """
            from m1 import f
            x = f()
            x[5] = 6
        """
        self.check_multiple_modules([(code1, "m1"), (code2, "m2")])

    def test_property_side_effect(self) -> None:
        code1 = """
            l = []
            class C:
                @property
                def l(self):
                    l.append(1)
                    return l
        """
        code2 = """
            from m1 import C
            c = C()
            c.l
        """
        self.assertError(
            [(code1, "m1"), (code2, "m2")], "[] from module m1 is modified by m2"
        )

    def test_func_dunder_dict_modification(self) -> None:
        code1 = """
            def f():
                pass
        """
        code2 = """
            from m1 import f

            f.__dict__["foo"] = 1
        """
        self.assertError(
            [(code1, "m1"), (code2, "m2")],
            "function.__dict__ from module m1 is modified by m2",
        )

    def test_func_dunder_dict_keys(self) -> None:
        code1 = """
            def f():
                pass
            f.foo = "bar"
        """
        code2 = """
            from m1 import f
            x = f.__dict__
            x["foo"] = "baz"

        """
        self.assertError(
            [(code1, "m1"), (code2, "m2")],
            "function.__dict__ from module m1 is modified by m2",
        )
