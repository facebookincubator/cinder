from __future__ import annotations

import ast
import symtable
import sys
import unittest
from compiler.strict import strict_compile
from compiler.strict.common import FIXED_MODULES
from compiler.strict.loader import StrictModule
from compiler.strict.rewriter import rewrite
from textwrap import dedent
from types import CoroutineType, FunctionType, ModuleType
from typing import Any, Dict, final, List, Optional, Set, Type, TypeVar
from weakref import ref

from .common import StrictTestWithCheckerBase


class RewriterTestCase(StrictTestWithCheckerBase):
    def compile_to_strict(
        self,
        code: str,
        builtins: Dict[str, Any] = __builtins__,
        modules: Optional[Dict[str, Dict[str, Any]]] = None,
        globals: Optional[Dict[str, Any]] = None,
    ) -> StrictModule:
        code = dedent(code)
        root = ast.parse(code)
        name = "foo"
        filename = "foo.py"
        symbols = symtable.symtable(code, filename, "exec")
        root = rewrite(
            root,
            symbols,
            filename,
            name,
            builtins=builtins,
        )
        c = strict_compile(name, filename, root)

        def freeze_type(freeze: Type[object]) -> None:
            pass

        def loose_slots(freeze: Type[object]) -> None:
            pass

        def strict_slots(typ: Type[object]) -> Type[object]:
            return typ

        fixed_modules = modules or dict(FIXED_MODULES)
        fixed_modules.update(
            __strict__={
                "freeze_type": freeze_type,
                "loose_slots": loose_slots,
                "strict_slots": strict_slots,
            }
        )
        additional_dicts = globals or {}
        additional_dicts.update(
            {"<fixed-modules>": fixed_modules, "<builtins>": builtins}
        )
        d, m = self._exec_strict_code(c, name, additional_dicts=additional_dicts)
        return m


@final
class ImmutableModuleTestCase(RewriterTestCase):
    def test_simple(self) -> None:
        code = """
x = 1
def f():
    return x
"""
        mod = self.compile_to_strict(code)
        self.assertEqual(mod.x, 1)
        self.assertEqual(type(mod.f), FunctionType)
        self.assertEqual(mod.f(), 1)
        self.assertEqual(mod.f.__name__, "f")

    def test_decorators(self) -> None:
        code = """
from __strict__ import strict_slots
def dec(x):
    return x

@dec
@strict_slots
def f():
    return 1
"""
        mod = self.compile_to_strict(code)
        self.assertEqual(type(mod.f), FunctionType)
        self.assertEqual(type(mod.dec), FunctionType)
        self.assertEqual(type(mod.f()), int)

        code = """
from __strict__ import strict_slots
def dec(x):
    return x

@dec
@strict_slots
class C:
    x = 1
"""
        mod = self.compile_to_strict(code)
        self.assertEqual(type(mod.C), type)
        self.assertEqual(type(mod.dec), FunctionType)
        self.assertEqual(type(mod.C.x), int)

    def test_visit_method_global(self) -> None:
        """test visiting an explicit global decl inside of a nested scope"""
        code = """
from __strict__ import strict_slots
X = 1
@strict_slots
class C:
    def f(self):
        global X
        X = 2
        return X
"""
        mod = self.compile_to_strict(code)
        self.assertEqual(mod.C().f(), 2)

    def test_class_def(self) -> None:
        code = """
from __strict__ import strict_slots
x = 42
@strict_slots
class C:
    def f(self):
        return x
    """
        mod = self.compile_to_strict(code)
        self.assertEqual(mod.C.__name__, "C")
        self.assertEqual(mod.C().f(), 42)

    def test_nested_class_def(self) -> None:
        code = """
from __strict__ import strict_slots
x = 42
@strict_slots
class C:
    def f(self):
        return x
    @strict_slots
    class D:
        def g(self):
            return x
    """
        mod = self.compile_to_strict(code)
        self.assertEqual(mod.C.__name__, "C")
        self.assertEqual(mod.C.__qualname__, "C")
        self.assertEqual(mod.C.D.__name__, "D")
        self.assertEqual(mod.C.D.__qualname__, "C.D")
        self.assertEqual(mod.C.f.__name__, "f")
        self.assertEqual(mod.C.f.__qualname__, "C.f")
        self.assertEqual(mod.C.D.g.__name__, "g")
        self.assertEqual(mod.C.D.g.__qualname__, "C.D.g")
        self.assertEqual(mod.C().f(), 42)
        self.assertEqual(mod.C.D().g(), 42)


@final
class LazyLoadingTestCases(RewriterTestCase):
    """test cases which verify the behavior of lazy loading is the same as
    non-lazy"""

    def test_lazy_load_exception(self) -> None:
        """lazy code raising an exception should run"""
        code = """
raise Exception('no way')
    """
        with self.assertRaises(Exception) as e:
            self.compile_to_strict(code)

        self.assertEqual(e.exception.args[0], "no way")

    def test_lazy_load_exception_2(self) -> None:
        code = """
from __strict__ import strict_slots
@strict_slots
class MyException(Exception):
    pass

raise MyException('no way')
    """

        with self.assertRaises(Exception) as e:
            self.compile_to_strict(code)

        self.assertEqual(type(e.exception).__name__, "MyException")

    def test_lazy_load_exception_3(self) -> None:
        code = """
from pickle import PicklingError

raise PicklingError('no way')
"""

        with self.assertRaises(Exception) as e:
            self.compile_to_strict(code)

        self.assertEqual(type(e.exception).__name__, "PicklingError")

    def test_lazy_load_exception_4(self) -> None:
        code = """
raise ShouldBeANameError()
"""

        with self.assertRaises(NameError):
            self.compile_to_strict(code)

    def test_lazy_load_no_reinit(self) -> None:
        """only run earlier initialization once"""
        code = """
try:
    y.append(0)
except:
    y = []
try:
    y.append(1)
    raise Exception()
except:
    pass
z = y
"""

        mod = self.compile_to_strict(code)
        self.assertEqual(mod.z, [1])

    def test_finish_initialization(self) -> None:
        """values need to be fully initialized upon their first access"""
        code = """
x = 1
y = x
x = 2
"""

        mod = self.compile_to_strict(code)
        self.assertEqual(mod.y, 1)
        self.assertEqual(mod.x, 2)

    def test_full_initialization(self) -> None:
        """values need to be fully initialized upon their first access"""
        code = """
x = 1
y = x
x = 2
"""

        mod = self.compile_to_strict(code)
        self.assertEqual(mod.x, 2)
        self.assertEqual(mod.y, 1)

    def test_transitive_closure(self) -> None:
        """we run the transitive closure of things required to be initialized"""

        code = """
x = 1
y = x
z = y
"""
        mod = self.compile_to_strict(code)
        self.assertEqual(mod.z, 1)

    def test_annotations(self) -> None:
        """annotations are properly initialized"""

        code = """
x: int = 1
    """

        mod = self.compile_to_strict(code)
        self.assertEqual(mod.__annotations__, {"x": int})
        self.assertEqual(mod.x, 1)

    def test_annotations_no_value(self) -> None:
        """annotations are properly initialized w/o values"""

        code = """
x: int
    """

        mod = self.compile_to_strict(code)
        self.assertEqual(mod.__annotations__, {"x": int})
        with self.assertRaises(AttributeError):
            mod.x

    def test_annotations_del(self) -> None:
        """values deleted after use are deleted, when accessed after initial var"""

        code = """
x = 1
y = x
del x
    """

        mod = self.compile_to_strict(code)
        self.assertEqual(mod.y, 1)
        with self.assertRaises(AttributeError):
            mod.x

    def test_annotations_del_2(self) -> None:
        """deleted values are deleted when accessed initially, previous values are okay"""

        code = """
x = 1
y = x
del x
    """

        mod = self.compile_to_strict(code)
        with self.assertRaises(AttributeError):
            mod.x
        self.assertEqual(mod.y, 1)

    def test_forward_dep(self) -> None:
        """forward dependencies cause all values to be initialized"""

        code = """
from __strict__ import strict_slots
@strict_slots
class C:
    pass
C.x = 42
    """

        mod = self.compile_to_strict(code)
        self.assertEqual(mod.C.x, 42)

    def test_not_init(self) -> None:
        """unassigned values don't show up (definite assignment would disallow this)"""

        code = """
x = 1
if x != 1:
    y = 2
    """

        mod = self.compile_to_strict(code)
        with self.assertRaises(AttributeError):
            mod.y

    def test_try_except_shadowed_handler_no_body_changes(self) -> None:
        """the try body doesn't get rewritten, but the except handler does"""

        code = """
try:
    x = 2
except Exception as min:
    pass
    """
        mod = self.compile_to_strict(code)
        self.assertEqual(mod.x, 2)
        self.assertFalse(hasattr(mod, "min"))
