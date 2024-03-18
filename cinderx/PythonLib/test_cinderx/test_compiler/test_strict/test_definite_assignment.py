from __future__ import annotations

from textwrap import dedent
from typing import final, Optional, Sequence

from cinderx.strictmodule import StrictAnalysisResult, StrictModuleLoader

from .common import StrictTestBase


@final
class DefiniteAssignmentTests(StrictTestBase):
    def analyze(
        self,
        code: str,
        mod_name: str = "mod",
        import_path: Optional[Sequence[str]] = None,
        allow_list_prefix: Optional[Sequence[str]] = None,
        stub_root: str = "",
    ) -> StrictAnalysisResult:
        code = dedent(code)
        compiler = StrictModuleLoader(
            import_path or [], stub_root, allow_list_prefix or [], [], True
        )

        module = compiler.check_source(code, f"{mod_name}.py", mod_name, [])
        return module

    def assertNoError(
        self,
        code: str,
        mod_name: str = "mod",
        import_path: Optional[Sequence[str]] = None,
        allow_list_prefix: Optional[Sequence[str]] = None,
        stub_root: str = "",
    ):
        m = self.analyze(code, mod_name, import_path, allow_list_prefix, stub_root)
        self.assertEqual(m.is_valid, True)
        self.assertEqual(m.errors, [])

    def assertError(
        self,
        code: str,
        err: str,
        mod_name: str = "mod",
        import_path: Optional[Sequence[str]] = None,
        allow_list_prefix: Optional[Sequence[str]] = None,
        stub_root: str = "",
    ):
        m = self.analyze(code, mod_name, import_path, allow_list_prefix, stub_root)
        self.assertEqual(m.is_valid, True)
        self.assertTrue(len(m.errors) > 0)
        self.assertTrue(err in m.errors[0][0])

    def test_simple_not_assigned(self) -> None:
        test_exec = """
import __strict__
abc + 1
"""
        self.assertError(test_exec, "NameError")

    def test_simple_del_not_assigned(self) -> None:
        test_exec = """
import __strict__
del abc
"""
        self.assertNoError(test_exec)

    def test_simple_assign_del_ok(self) -> None:
        test_exec = """
import __strict__
abc = 1
del abc
"""
        self.assertNoError(test_exec)

    def test_simple_assign_double_del(self) -> None:
        test_exec = """
import __strict__
abc = 1
del abc
del abc
"""
        self.assertNoError(test_exec)

    def test_simple_if(self) -> None:
        test_exec = """
import __strict__
if False:
    abc = 1
abc + 1
"""
        self.assertError(test_exec, "NameError")

    def test_simple_if_del(self) -> None:
        test_exec = """
import __strict__
abc = 1
if True:
    del abc
abc + 1
"""
        self.assertError(test_exec, "NameError")

    def test_simple_if_else(self) -> None:
        test_exec = """
import __strict__
if str:
    foo = 1
else:
    abc = 2
abc + 1
"""
        self.assertError(test_exec, "NameError")

    def test_simple_if_else_del(self) -> None:
        test_exec = """
import __strict__
abc = 1
if str:
    pass
else:
    del abc
abc + 1
"""
        self.assertNoError(test_exec)

    def test_simple_if_ok(self) -> None:
        test_exec = """
import __strict__
if str:
    abc = 1
else:
    abc = 2
abc
"""
        self.assertNoError(test_exec)

    def test_func_dec(self) -> None:
        test_exec = """
import __strict__
@abc
def f(x): pass
"""
        self.assertError(test_exec, "NameError")

    def test_func_self_default(self) -> None:
        test_exec = """
import __strict__
def f(x = f()): pass
"""
        self.assertError(test_exec, "NameError")

    def test_async_func_dec(self) -> None:
        test_exec = """
import __strict__
@abc
async def f(x): pass
"""
        self.assertError(test_exec, "NameError")

    def test_async_func_self_default(self) -> None:
        test_exec = """
import __strict__
async def f(x = f()): pass
"""
        self.assertError(test_exec, "NameError")

    def test_while(self) -> None:
        test_exec = """
import __strict__
while False:
    abc = 1
abc + 1
"""
        self.assertError(test_exec, "NameError")

    def test_while_else(self) -> None:
        test_exec = """
import __strict__
while False:
    abc = 1
else:
    abc = 1
abc
"""
        self.assertNoError(test_exec)

    def test_while_del(self) -> None:
        test_exec = """
import __strict__
abc = 1
while str:
    del abc
    break
abc + 1
"""
        self.assertError(test_exec, "NameError")

    def test_while_else_del(self) -> None:
        test_exec = """
import __strict__
abc = 1
while False:
    pass
else:
    del abc
x = abc + 1
"""
        self.assertError(test_exec, "NameError")

    def test_while_del_else(self) -> None:
        test_exec = """
import __strict__
abc = 1
x = 1
while x > 0:
    del abc
    x = x - 1
else:
    abc + 1
"""
        self.assertError(test_exec, "NameError")

    def test_class_defined(self) -> None:
        test_exec = """
import __strict__
class C:
    pass

C
"""
        self.assertNoError(test_exec)

    def test_class_defined_with_func(self) -> None:
        test_exec = """
import __strict__
class C:
    def __init__(self):
        pass

C
"""
        self.assertNoError(test_exec)

    def test_class_scoping(self) -> None:
        test_exec = """
import __strict__
class C:
    abc = 42

x = abc + 1
"""
        self.assertError(test_exec, "NameError")

    def test_class_uninit_global_read(self) -> None:
        test_exec = """
import __strict__
class C:
    x = abc + 1

"""
        self.assertError(test_exec, "NameError")

    def test_class_uninit_class_read(self) -> None:
        test_exec = """
import __strict__
class C:
    if str:
        abc = 42
    abc + 1
"""
        self.assertNoError(test_exec)

    def test_nested_class_uninit_read(self) -> None:
        test_exec = """
import __strict__
class C:
    abc = 42
    class D:
        x = abc + 1
"""
        self.assertError(test_exec, "NameError")

    def test_class_undef_dec(self) -> None:
        test_exec = """
import __strict__
@abc
class C:
    pass
"""
        self.assertError(test_exec, "NameError")

    def test_uninit_aug_assign(self) -> None:
        test_exec = """
import __strict__
abc += 1
"""
        self.assertError(test_exec, "NameError")

    def test_aug_assign(self) -> None:
        test_exec = """
import __strict__
abc = 0
abc += 1
    """

        self.assertNoError(test_exec)

    def test_with_no_assign(self) -> None:
        test_exec = """
import __strict__
class A:
    def __enter__(self):
        pass
    def __exit__(self, exc_tp, exc, tb):
        pass
with A():
    abc = 1
abc + 1
"""

        self.assertNoError(test_exec)

    def test_with_var(self) -> None:
        test_exec = """
import __strict__
class A:
    def __enter__(self):
        pass
    def __exit__(self, exc_tp, exc, tb):
        pass
with A() as abc:
    pass
abc
"""

        self.assertNoError(test_exec)

    def test_with_var_destructured(self) -> None:
        test_exec = """
import __strict__
class A:
    def __enter__(self):
        return 1, 3
    def __exit__(self, exc_tp, exc, tb):
        pass
with A() as (abc, foo):
    pass
abc
foo
"""

        self.assertNoError(test_exec)

    def test_import(self) -> None:
        test_exec = """
import __strict__
import abc
abc
"""

        self.assertNoError(test_exec)

    def test_import_as(self) -> None:
        test_exec = """
import __strict__
import foo as abc
abc
"""

        self.assertNoError(test_exec)

    def test_import_from(self) -> None:
        test_exec = """
import __strict__
from foo import abc
abc
"""

        self.assertNoError(test_exec)

    def test_import_from_as(self) -> None:
        test_exec = """
import __strict__
from foo import bar as abc
abc
"""

        self.assertNoError(test_exec)

    def test_del_in_finally(self) -> None:
        test_exec = """
import __strict__
try:
    abc = 1
finally:
    del abc
"""
        self.assertNoError(test_exec)

    def test_del_in_finally_2(self) -> None:
        test_exec = """
import __strict__
abc = 1
try:
    pass
finally:
    del abc
abc + 1
"""
        self.assertError(test_exec, "NameError")

    def test_finally_no_del(self) -> None:
        test_exec = """
import __strict__
try:
    abc = 1
finally:
    pass
abc
    """
        self.assertNoError(test_exec)

    def test_finally_not_defined(self) -> None:
        test_exec = """
import __strict__
try:
    abc = 1
finally:
    abc + 1
"""
        self.assertNoError(test_exec)

    def test_try_finally_deletes_apply(self) -> None:
        test_exec = """
import __strict__
abc = 1
try:
    del abc
finally:
    pass
abc + 1
"""
        self.assertError(test_exec, "NameError")

    def test_try_except_var_defined(self) -> None:
        test_exec = """
import __strict__
try:
    pass
except Exception as abc:
    abc
"""
        self.assertNoError(test_exec)

    def test_try_except_var_not_defined_after(self) -> None:
        test_exec = """
import __strict__
try:
    pass
except Exception as abc:
    pass
abc + 1
"""
        self.assertError(test_exec, "NameError")

    def test_try_except_no_try_define(self) -> None:
        test_exec = """
import __strict__
try:
    abc = 1
except Exception:
    pass
abc + 1
"""
        self.assertNoError(test_exec)

    def test_try_except_no_except_define(self) -> None:
        test_exec = """
import __strict__
try:
    pass
except Exception:
    abc = 1
abc + 1
"""
        self.assertError(test_exec, "NameError")

    def test_try_except_dels_assumed(self) -> None:
        test_exec = """
import __strict__
abc = 1
try:
    del abc
except Exception:
    pass
abc + 1
"""
        self.assertError(test_exec, "NameError")

    def test_try_except_dels_assumed_in_except(self) -> None:
        test_exec = """
import __strict__
abc = 1
try:
    del abc
except Exception:
    abc + 1
"""
        self.assertNoError(test_exec)

    def test_try_except_except_dels_assumed(self) -> None:
        test_exec = """
import __strict__
abc = 1
try:
    pass
except Exception:
    del abc
abc + 1
"""
        self.assertNoError(test_exec)

    def test_try_except_finally(self) -> None:
        test_exec = """
import __strict__
try:
    pass
except Exception:
    pass
finally:
    abc = 1
abc
"""
        self.assertNoError(test_exec)

    def test_try_except_finally_try_not_assumed(self) -> None:
        test_exec = """
import __strict__
try:
    abc = 1
except Exception:
    pass
finally:
    abc + 1
"""
        self.assertNoError(test_exec)

    def test_try_except_finally_except_not_assumed(self) -> None:
        test_exec = """
import __strict__
try:
    pass
except Exception:
    abc = 1
finally:
    abc + 1
"""
        self.assertError(test_exec, "NameError")

    def test_try_except_else_try_assumed(self) -> None:
        test_exec = """
import __strict__
try:
    abc = 1
except Exception:
    pass
else:
    abc
"""

        self.assertNoError(test_exec)

    def test_try_except_else_try_assumed_del(self) -> None:
        test_exec = """
import __strict__
try:
    abc = 1
except Exception:
    pass
else:
    del abc
"""

        self.assertNoError(test_exec)

    def test_try_except_else_except_not_assumed(self) -> None:
        test_exec = """
import __strict__
try:
    pass
except Exception:
    abc = 1
else:
    x = abc + 1
"""
        self.assertError(test_exec, "NameError")

    def test_try_except_else_except_del_not_assumed(self) -> None:
        test_exec = """
import __strict__
abc = 1
try:
    pass
except Exception:
    del abc
else:
    x = abc + 1
"""
        self.assertNoError(test_exec)

    def test_try_except_else_assign_not_assumed_for_finally(self) -> None:
        test_exec = """
import __strict__
try:
    pass
except Exception:
    pass
else:
    abc = 1
finally:
    x = abc + 1
"""
        self.assertNoError(test_exec)

    def test_try_except_finally_del_assumed(self) -> None:
        test_exec = """
import __strict__
abc = 1
try:
    pass
except Exception:
    del abc
finally:
    x = abc + 1
"""
        self.assertNoError(test_exec)

    def test_lambda_not_assigned(self) -> None:
        test_exec = """
import __strict__
x = (lambda x=abc + 1: 42)
"""
        self.assertError(test_exec, "NameError")

    def test_lambda_ok(self) -> None:
        test_exec = """
import __strict__
x = lambda x: abc
"""
        self.assertNoError(test_exec)

    def test_list_comp(self) -> None:
        test_exec = """
import __strict__
foo = [1, 2, 3]
bar = [x for x in foo]
"""
        self.assertNoError(test_exec)

    def test_list_comp_undef(self) -> None:
        test_exec = """
import __strict__
bar = [x for x in abc]
"""
        self.assertError(test_exec, "NameError")

    def test_list_comp_if(self) -> None:
        test_exec = """
import __strict__
foo = [1, 2, 3]
bar = [x for x in foo if x]
"""
        self.assertNoError(test_exec)

    def test_set_comp(self) -> None:
        test_exec = """
import __strict__
foo = [1, 2, 3]
bar = {x for x in foo}
"""
        self.assertNoError(test_exec)

    def test_set_comp_undef(self) -> None:
        test_exec = """
import __strict__
bar = {x for x in abc}
"""
        self.assertError(test_exec, "NameError")

    def test_set_comp_undef_value(self) -> None:
        test_exec = """
import __strict__
foo = [1, 2, 3]
bar = {(x, abc) for x in foo}
"""
        self.assertError(test_exec, "NameError")

    def test_set_comp_if(self) -> None:
        test_exec = """
import __strict__
foo = [1, 2, 3]
bar = {x for x in foo if x}
"""
        self.assertNoError(test_exec)

    def test_gen_comp(self) -> None:
        test_exec = """
import __strict__
foo = [1, 2, 3]
bar = (x for x in foo)
"""
        self.assertNoError(test_exec)

    def test_gen_comp_undef(self) -> None:
        test_exec = """
import __strict__
bar = (x for x in abc)
"""
        self.assertError(test_exec, "NameError")

    def test_gen_comp_undef_value(self) -> None:
        test_exec = """
import __strict__
foo = [1, 2, 3]
bar = ((x, abc) for x in foo)
"""
        self.assertError(test_exec, "NameError")

    def test_gen_comp_if(self) -> None:
        test_exec = """
import __strict__
foo = [1, 2, 3]
bar = (x for x in foo if x)
"""
        self.assertNoError(test_exec)

    def test_dict_comp(self) -> None:
        test_exec = """
import __strict__
foo = [1, 2, 3]
bar = {x:x for x in foo}
"""
        self.assertNoError(test_exec)

    def test_dict_comp_undef(self) -> None:
        test_exec = """
import __strict__
bar = {x:x for x in abc}
"""
        self.assertError(test_exec, "NameError")

    def test_dict_comp_if(self) -> None:
        test_exec = """
import __strict__
foo = [1, 2, 3]
bar = {x:x for x in foo if x}
"""
        self.assertNoError(test_exec)

    def test_self_assign(self) -> None:
        test_exec = """
import __strict__
abc = abc + 1
"""
        self.assertError(test_exec, "NameError")

    def test_ann_assign_not_defined(self) -> None:
        test_exec = """
import __strict__
abc: int
abc + 1
"""
        self.assertError(test_exec, "NameError")

    def test_expected_globals_name(self) -> None:
        test_exec = """
import __strict__
x = __name__
"""
        self.assertNoError(test_exec)

    def test_raise_unreachable(self) -> None:
        test_exec = """
import __strict__
x = 0
if x:
    raise Exception
    abc = 2
else:
    abc = 1

abc + 1
"""
        self.assertNoError(test_exec)
