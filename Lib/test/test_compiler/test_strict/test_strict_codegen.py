from __future__ import annotations

import sys

import unittest
from typing import Any

from .common import StrictTestBase, StrictTestWithCheckerBase

try:
    import cinderjit
except ImportError:
    cinderjit = None


class StrictCompilationTests(StrictTestBase):
    def test_strictmod_freeze_type(self):
        codestr = """
        class C:
            x = 1
        """
        code = self.compile(codestr)
        self.assertInBytecode(
            code,
            "LOAD_GLOBAL",
            "<freeze-type>",
        )
        self.assertInBytecode(
            code,
            "STORE_GLOBAL",
            "<classes>",
        )
        with self.with_freeze_type_setting(True), self.in_module(codestr) as mod:
            C = mod.C
            self.assertEqual(C.x, 1)
            with self.assertRaises(TypeError):
                C.x = 2
            self.assertEqual(C.x, 1)

    def test_strictmod_freeze_set_false(self):
        codestr = """
        class C:
            x = 1
        """
        code = self.compile(codestr)
        with self.with_freeze_type_setting(False), self.in_module(codestr) as mod:
            C = mod.C
            self.assertEqual(C.x, 1)
            C.x = 2
            self.assertEqual(C.x, 2)

    def test_strictmod_class_in_function(self):
        codestr = """
        def f():
            class C:
                x = 1
            return C
        """
        with self.with_freeze_type_setting(True), self.in_module(codestr) as mod:
            f = mod.f
            C = f()
            self.assertEqual(C.x, 1)

            code = f.__code__
            self.assertInBytecode(
                code,
                "SETUP_FINALLY",
            )
            self.assertInBytecode(
                code,
                "STORE_FAST",
                "<classes>",
            )

    def test_strictmod_freeze_class_in_function(self):
        codestr = """
        def f():
            class C:
                x = 1
            return C
        """
        with self.with_freeze_type_setting(True), self.in_module(codestr) as mod:
            f = mod.f
            C = f()
            self.assertEqual(C.x, 1)
            with self.assertRaises(TypeError):
                C.x = 2
            self.assertEqual(C.x, 1)

    def test_strictmod_class_not_in_function(self):
        codestr = """
        class C:
            pass
        def f():
            return C
        """
        code = self.compile(codestr)
        self.assertNotInBytecode(
            code,
            "SETUP_FINALLY",
        )
        self.assertInBytecode(
            code,
            "STORE_GLOBAL",
            "<classes>",
        )

    def test_strictmod_fixed_modules_typing(self):
        codestr = """
        from typing import final

        @final
        class C:
            x = 1
        """
        code = self.compile(codestr)
        self.assertInBytecode(
            code,
            "STORE_GLOBAL",
            "final",
        )
        with self.with_freeze_type_setting(True), self.in_module(codestr) as mod:
            C = mod.C
            self.assertEqual(C.x, 1)
            with self.assertRaises(TypeError):
                C.x = 2
            self.assertEqual(C.x, 1)


class StrictBuiltinCompilationTests(StrictTestWithCheckerBase):
    def test_deps_run(self) -> None:
        """other things which interact with dependencies need to run"""

        called = False

        def side_effect(x: List[object]) -> None:
            nonlocal called
            called = True
            self.assertEqual(x, [42])
            x.append(23)

        code = """
            x = []
            y = list(x)
            x.append(42)
            side_effect(x)
        """
        mod = self.compile_and_run(code, builtins={"side_effect": side_effect})
        self.assertEqual(mod.y, [])
        self.assertTrue(called)

    def test_deps_run_2(self) -> None:
        """other things which interact with dependencies need to run"""

        called = False

        def side_effect(x: List[object]) -> None:
            nonlocal called
            called = True
            self.assertEqual(x, [42])
            x.append(23)

        code = """
            x = []
            y = list(x)
            x.append(42)
            side_effect(x)
            y = list(x)
        """

        mod = self.compile_and_run(code, builtins={"side_effect": side_effect})
        self.assertEqual(mod.y, [42, 23])
        self.assertTrue(called)

    def test_deps_not_run(self) -> None:
        """independent pieces of code don't cause others to run"""

        called = False

        def side_effect(x: object) -> None:
            nonlocal called
            called = True

        code = """
            x = []
            y = 2
            side_effect(x)
        """

        mod = self.compile_and_run(code, builtins={"side_effect": side_effect})
        self.assertEqual(mod.y, 2)
        self.assertEqual(called, True)

    def test_builtins(self) -> None:
        code = """
            x = 1
            def f():
                return min(x, 0)
        """
        mod = self.compile_and_run(code, builtins={"min": min})
        self.assertEqual(mod.f(), 0)

        code = """
            x = 1
            min = 3
            del min
            def f():
                return min(x, 0)
        """
        mod = self.compile_and_run(code, builtins={"min": max})
        self.assertEqual(mod.f(), 1)

        # import pdb; pdb.set_trace()

        code = """
            x = 1
            def f():
                return min(x, 0)

            x = globals()
        """
        mod = self.compile_and_run(code, builtins={"min": min, globals: None})
        self.assertNotIn("min", mod.x)

    def test_del_shadowed_builtin(self) -> None:
        code = """
            min = None
            x = 1
            del min
            def f():
                return min(x, 0)
        """
        mod = self.compile_and_run(code, builtins={"min": min, "NameError": NameError})
        self.assertEqual(mod.f(), 0)

        code = """
            min = None
            del min
            x = 1
            def f():
                return min(x, 0)
        """
        mod = self.compile_and_run(code, builtins={"min": max})
        self.assertEqual(mod.f(), 1)

    def test_del_shadowed_and_call_globals(self) -> None:
        code = """
            min = 2
            del min
            x = globals()
        """
        mod = self.compile_and_run(code)
        self.assertNotIn("min", mod.x)
        self.assertNotIn("<assigned:min>", mod.x)

    def test_cant_assign(self) -> None:
        code = """
            x = 1
            def f():
                return x
        """
        mod = self.compile_and_run(code)
        with self.assertRaises(AttributeError):
            # pyre-ignore[16]: no attribute x
            mod.x = 42

    def test_deleted(self) -> None:
        code = """
            x = 1
            del x
        """
        mod = self.compile_and_run(code)
        with self.assertRaises(AttributeError):
            mod.x

    def test_deleted_mixed_global_non_name(self) -> None:
        # Mixed (global, non-name) multi-delete statement
        code = """
            x = 1
            y = {2:3, 4:2}
            del x, y[2]
        """

        mod = self.compile_and_run(code)
        with self.assertRaises(AttributeError):
            mod.x
        self.assertEqual(mod.y, {4: 2})

    def test_deleted_mixed_global_non_global(self) -> None:
        # Mixed (global, non-global) multi-delete statement
        code = """
            x = 1
            def f():
                global x
                y = 2
                del x, y
                return y
        """

        mod = self.compile_and_run(code)
        self.assertEqual(mod.x, 1)
        with self.assertRaises(UnboundLocalError):
            mod.f()

    def test_deleted_non_global(self) -> None:
        # Non-mixed non-global case
        code = """
            y = {2:3, 4:2}
            del y[2]
        """

        mod = self.compile_and_run(code)
        self.assertEqual(mod.y, {4: 2})

    def test_deleted_accessed_on_call(self) -> None:
        # This doesn't match the non-strict module case because the error message
        # is incorrect.
        code = """
            x = 1
            del x
            def f ():
                a = x
        """
        with self.assertRaisesRegex(NameError, "name 'x' is not defined"):
            mod = self.compile_and_run(code)
            mod.f()

    def test_closure(self) -> None:
        code = """
            abc = 42
            def x():
                abc = 100
                def inner():
                    return abc
                return inner

            a = x()() # should be 100
        """
        mod = self.compile_and_run(code)
        self.assertEqual(mod.a, 100)

    def test_nonlocal_alias(self) -> None:
        code = """
            abc = 42
            def x():
                abc = 100
                def inner():
                    global abc
                    return abc
                return inner

            a = x()() # should be 42
        """
        mod = self.compile_and_run(code)
        self.assertEqual(mod.a, 42)

    def test_nonlocal_alias_called_from_mod(self) -> None:
        code = """
            abc = 42
            def x():
                abc = 100
                def inner():
                    global abc
                    del abc
                return inner

            x()()
        """
        mod = self.compile_and_run(code)
        self.assertFalse(hasattr(mod, "abc"))

    def test_nonlocal_alias_multi_func(self) -> None:
        code = """
            def abc():
                return 100

            def x():
                def abc():
                    return 42
                def inner():
                    global abc
                    return abc
                return inner

            a = x()()() # should be 100
        """
        mod = self.compile_and_run(code)
        self.assertEqual("abc", mod.x()().__name__)
        self.assertEqual("abc", mod.x()().__qualname__)
        self.assertEqual(mod.a, 100)

    def test_nonlocal_alias_prop(self) -> None:
        code = """
            from __strict__ import strict_slots
            @strict_slots
            class C:
                x = 1

            def x():
                @strict_slots
                class C:
                    x = 2
                def inner():
                    global C
                    return C
                return inner

            a = x()().x
        """

        mod = self.compile_and_run(code)
        self.assertEqual("C", mod.x()().__name__)
        self.assertEqual("C", mod.x()().__qualname__)
        self.assertEqual(mod.a, 1)

    def test_global_assign(self) -> None:
        code = """
            abc = 42
            def modify(new_value):
                global abc
                abc = new_value
        """
        mod = self.compile_and_run(code)
        self.assertEqual(mod.abc, 42)
        mod.modify(100)
        self.assertEqual(mod.abc, 100)

    def test_global_delete(self) -> None:
        code = """
            abc = 42
            def f():
                global abc
                del abc

            f()
        """
        mod = self.compile_and_run(code)
        self.assertFalse(hasattr(mod, "abc"))

    def test_call_globals(self) -> None:
        code = """
            abc = 42
            x = globals()
        """
        mod = self.compile_and_run(code)
        self.assertEqual(mod.x["abc"], 42)
        self.assertEqual(mod.x["__name__"], "<module>")

    def test_shadow_del_globals(self) -> None:
        """re-assigning to a deleted globals should restore our globals helper"""
        code = """
            globals = 2
            abc = 42
            del globals
            x = globals()
        """
        mod = self.compile_and_run(code)
        self.assertEqual(mod.x["abc"], 42)
        self.assertEqual(mod.x["__name__"], "<module>")

    def test_vars(self) -> None:
        code = """
            abc = 42
        """
        mod = self.compile_and_run(code)
        self.assertEqual(vars(mod)["abc"], 42)

    def test_double_def(self) -> None:
        # TODO: Need better bodies that we can differentiate here
        code = """
            x = 1
            def f():
                return x

            def f():
                return 42
        """
        mod = self.compile_and_run(code)
        self.assertEqual(mod.f(), 42)

    def test_exec(self) -> None:
        code = """
            y = []
            def f():
                x = []
                exec('x.append(42); y.append(100)')
                return x, y
        """
        mod = self.compile_and_run(code)
        self.assertEqual(mod.f(), ([42], [100]))

        code = """
            y = []
            def f():
                x = []
                exec('x.append(42); y.append(100)', {'x': [], 'y': []})
                return x, y
        """
        mod = self.compile_and_run(code)
        self.assertEqual(mod.f(), ([], []))

        # exec can't modify globals
        code = """
            x = 1
            def f():
                exec('global x; x = 2')
            f()
        """
        mod = self.compile_and_run(code)
        self.assertEqual(mod.x, 1)

    def test_eval(self) -> None:
        code = """
            y = 42
            def f():
                x = 100
                return eval('x, y')
        """
        mod = self.compile_and_run(code)
        self.assertEqual(mod.f(), (100, 42))

        code = """
            y = 42
            def f():
                x = 100
                return eval('x, y', {'x':23, 'y':5})
        """
        mod = self.compile_and_run(code)
        self.assertEqual(mod.f(), (23, 5))

    def test_define_dunder_globals(self) -> None:
        code = """
            __globals__ = 42
        """
        mod = self.compile_and_run(code)
        self.assertEqual(mod.__globals__, 42)

    def test_shadow_via_for(self) -> None:
        code = """
            for min in [1,2,3]:
                pass
            x = 1
            del min
            def f():
                return min(x, 0)
        """
        mod = self.compile_and_run(code, builtins={"min": min, "NameError": NameError})
        self.assertEqual(mod.f(), 0)

    def test_del_shadowed_via_tuple(self) -> None:
        code = """
            (min, max) = None, None
            x = 1
            del min
            def f():
                return min(x, 0)
        """
        mod = self.compile_and_run(code, builtins={"max": max, "min": min})
        self.assertEqual(mod.f(), 0)

    def test_del_shadowed_via_list(self) -> None:
        code = """
            (min, max) = None, None
            x = 1
            del min
            def f():
                return min(x, 0)
        """
        mod = self.compile_and_run(code, builtins={"max": max, "min": min})
        self.assertEqual(mod.f(), 0)

    def test_list_comp_aliased_builtin(self) -> None:
        code = """
            min = 1
            del min
            y = [min for x in [1,2,3]]
            x = 1
            def f():
                return y[0](x, 0)
        """
        mod = self.compile_and_run(code, builtins={"min": min})
        self.assertEqual(mod.f(), 0)

    def test_set_comp_aliased_builtin(self) -> None:
        code = """
            min = 1
            del min
            y = {min for x in [1,2,3]}
            x = 1
            def f():
                return next(iter(y))(x, 0)
        """
        mod = self.compile_and_run(
            code, builtins={"min": min, "iter": iter, "next": next}
        )
        self.assertEqual(mod.f(), 0)

    def test_gen_comp_aliased_builtin(self) -> None:
        code = """
            min = 1
            del min
            y = (min for x in [1,2,3])
            x = 1
            def f():
                return next(iter(y))(x, 0)
        """
        mod = self.compile_and_run(
            code, builtins={"min": min, "iter": iter, "next": next}
        )
        self.assertEqual(mod.f(), 0)

    def test_dict_comp_aliased_builtin(self) -> None:
        code = """
            min = 1
            del min
            y = {min:x for x in [1,2,3]}
            x = 1
            def f():
                return next(iter(y))(x, 0)
        """
        mod = self.compile_and_run(
            code, builtins={"min": min, "iter": iter, "next": next}
        )
        self.assertEqual(mod.f(), 0)

    def test_try_except_alias_builtin(self) -> None:
        code = """
            try:
                raise Exception()
            except Exception as min:
                pass
            x = 1
            def f():
                return min(x, 0)
        """
        mod = self.compile_and_run(code, builtins={"min": min, "Exception": Exception})
        self.assertEqual(mod.f(), 0)

    def test_try_except_alias_builtin_2(self) -> None:
        code = """
            try:
                raise Exception()
            except Exception as min:
                pass
            except TypeError as min:
                pass
            x = 1
            def f():
                return min(x, 0)
        """
        mod = self.compile_and_run(
            code, builtins={"min": min, "Exception": Exception, "TypeError": TypeError}
        )
        self.assertEqual(mod.f(), 0)

    def test_try_except_alias_builtin_check_exc(self) -> None:
        code = """
            try:
                raise Exception()
            except Exception as min:
                if type(min) is not Exception:
                    raise Exception('wrong exception type!')
            x = 1
            def f():
                return min(x, 0)
        """
        mod = self.compile_and_run(
            code, builtins={"min": min, "Exception": Exception, "type": type}
        )
        self.assertEqual(mod.f(), 0)


class StrictCheckedCompilationTests(StrictTestWithCheckerBase):
    def test_strictmod_freeze_type(self):
        codestr = """
        import __strict__
        class C:
            x = 1
        """
        with self.with_freeze_type_setting(True), self.in_checked_module(
            codestr
        ) as mod:
            C = mod.C
            self.assertEqual(C.x, 1)
            with self.assertRaises(TypeError):
                C.x = 2
            self.assertEqual(C.x, 1)

    def test_strictmod_mutable(self):
        codestr = """
        import __strict__
        from __strict__ import mutable

        @mutable
        class C:
            x = 1
        """
        code = self.check_and_compile(codestr)
        self.assertInBytecode(
            code,
            "STORE_GLOBAL",
            "mutable",
        )
        with self.with_freeze_type_setting(True), self.in_checked_module(
            codestr
        ) as mod:
            C = mod.C
            self.assertEqual(C.x, 1)
            C.x = 2
            self.assertEqual(C.x, 2)

    def test_strictmod_mutable_noanalyze(self):
        codestr = """
        import __strict__
        from __strict__ import mutable, allow_side_effects

        @mutable
        class C:
            x = 1
        """
        with self.with_freeze_type_setting(True), self.in_module(codestr) as mod:
            C = mod.C
            self.assertEqual(C.x, 1)
            C.x = 2
            self.assertEqual(C.x, 2)

    def test_strictmod_cached_property(self):
        codestr = """
        import __strict__
        from __strict__ import strict_slots, _mark_cached_property, mutable
        def dec(x):
            _mark_cached_property(x, False, dec)
            class C:
                def __get__(self, inst, ctx):
                    return x(inst)

            return C()

        @mutable
        @strict_slots
        class C:
            @dec
            def f(self):
                return 1
        """
        with self.with_freeze_type_setting(True), self.in_checked_module(
            codestr
        ) as mod:
            C = mod.C
            c = C()
            self.assertEqual(c.f, 1)
            self.assertEqual(c.f, 1)
            self.assertEqual(C.__slots__, ("f",))


if __name__ == "__main__":
    unittest.main()
