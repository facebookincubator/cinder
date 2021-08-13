from __future__ import annotations

from textwrap import dedent
from typing import Sequence, Tuple, Type, final

from pyre_extensions import none_throws
from strict_modules.abstract import AbstractModule
from strict_modules.common import ErrorSink
from strict_modules.compiler import Compiler
from strict_modules.exceptions import (
    StrictModuleException,
    StrictModuleModifyImportedValueException,
    UnsafeCallException,
)
from strict_modules.tests.base import ExceptionMatch, StrictModuleTest


@final
class OwnershipTests(StrictModuleTest):
    ONCALL_SHORTNAME = "strictmod"

    def check_multiple_modules(
        self, modules: Sequence[Tuple[str, str]]
    ) -> Sequence[AbstractModule]:
        comp = Compiler([], ErrorSink, lambda name: True, [], support_cache=False)
        mods = []
        for code, name in modules:
            mods.append(
                none_throws(comp.load_from_source(dedent(code), "<test>", name).value)
            )
        return mods

    def assertError(
        self,
        modules: Sequence[Tuple[str, str]],
        expected: ExceptionMatch | Type[StrictModuleException],
        **kwargs: object,
    ) -> None:
        # pyre-fixme[6]: Expected `Optional[ExceptionMatch]` for 2nd param but got
        #  `object`.
        with self.assertExceptionMatch(expected, **kwargs):
            self.check_multiple_modules(modules)

    def test_list_modify(self) -> None:
        code1 = """
            l1 = [1, 2, 3]
        """
        code2 = """
            from m1 import l1
            l1[0] = 2
        """
        self.assertError(
            [(code1, "m1"), (code2, "m2")],
            StrictModuleModifyImportedValueException,
            modified_obj="[1, 2, 3]",
            owner_module="m1",
            caller_module="m2",
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
            [(code1, "m1"), (code2, "m2")],
            StrictModuleModifyImportedValueException,
            modified_obj="[1, 2, 3]",
            owner_module="m1",
            caller_module="m2",
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
            StrictModuleModifyImportedValueException,
            modified_obj="<dict>",
            owner_module="m1",
            caller_module="m2",
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
            UnsafeCallException,
            callable_name="f",
            cause=self.Match(
                StrictModuleModifyImportedValueException,
                modified_obj="<dict>",
                owner_module="m1",
                caller_module="m3",
            ),
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
            [(code1, "m1"), (code2, "m2")],
            UnsafeCallException,
            callable_name="dec",
            cause=self.Match(
                StrictModuleModifyImportedValueException,
                modified_obj="[0]",
                owner_module="m1",
                caller_module="m2",
            ),
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
            [(code1, "m1"), (code2, "m2")],
            UnsafeCallException,
            callable_name="C.l",
            cause=self.Match(
                StrictModuleModifyImportedValueException,
                modified_obj="[]",
                owner_module="m1",
                caller_module="m2",
            ),
        )

    def test_bound_method_ownership(self) -> None:
        code1 = """
            class C:
                def f(cls) -> None:
                    pass
        """
        code2 = """
            from m1 import C
            c = C()
            x = c.f
        """
        m1, m2 = self.check_multiple_modules([(code1, "m1"), (code2, "m2")])
        self.assertIs(m2.dict["x"].creator, m2)

    def test_bound_classmethod_ownership(self) -> None:
        code1 = """
            class C:
                @classmethod
                def f(cls) -> None:
                    pass
        """
        code2 = """
            from m1 import C
            x = C.f
        """
        m1, m2 = self.check_multiple_modules([(code1, "m1"), (code2, "m2")])
        self.assertIs(m2.dict["x"].creator, m2)

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
            StrictModuleModifyImportedValueException,
            modified_obj="__dict__",
            owner_module="m1",
            caller_module="m2",
        )

    def test_func_dunder_dict_keys(self) -> None:
        code1 = """
            def f():
                pass
            f.foo = "bar"
        """
        code2 = """
            from m1 import f
            x, = f.__dict__
        """
        m1, m2 = self.check_multiple_modules([(code1, "m1"), (code2, "m2")])
        self.assertIs(m2.dict["x"].creator, m1)
