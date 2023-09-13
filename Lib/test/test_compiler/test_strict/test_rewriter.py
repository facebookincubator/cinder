from __future__ import annotations

import ast
import symtable
import sys
import unittest
from compiler.strict import strict_compile
from compiler.strict.common import FIXED_MODULES
from compiler.strict.loader import StrictModule
from compiler.strict.preprocessor import ENABLE_SLOTS_DECORATOR
from compiler.strict.rewriter import rewrite
from textwrap import dedent
from types import CoroutineType, FunctionType, ModuleType
from typing import Any, Dict, final, List, Optional, Set, Type, TypeVar
from weakref import ref

from .common import StrictTestWithCheckerBase


class RewriterTestPreprocessor(ast.NodeVisitor):
    def visit_ClassDef(self, node: ast.ClassDef):
        name = ast.Name(ENABLE_SLOTS_DECORATOR, ast.Load())
        name.lineno = node.lineno
        name.col_offset = node.col_offset
        node.decorator_list.append(name)


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
        RewriterTestPreprocessor().visit(root)
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
class SlotificationTestCase(RewriterTestCase):
    def test_init(self) -> None:
        """__init__ assignemnts are initialized"""
        code = """
from __strict__ import strict_slots
@strict_slots
class C:
    def __init__(self):
        self.x = 42
    """
        mod = self.compile_to_strict(code)
        inst = mod.C()
        self.assertEqual(inst.x, 42)
        with self.assertRaises(AttributeError):
            inst.y = 100

    def test_init_seq_tuple(self) -> None:
        """__init__ assignemnts are initialized"""
        code = """
from __strict__ import strict_slots
@strict_slots
class C:
    def __init__(self):
        (self.x, self.y) = 42, 100
    """
        mod = self.compile_to_strict(code)
        inst = mod.C()
        self.assertEqual(inst.x, 42)
        self.assertEqual(inst.y, 100)
        with self.assertRaises(AttributeError):
            inst.z = 100

    def test_init_seq_list(self) -> None:
        """__init__ assignemnts are initialized"""
        code = """
from __strict__ import strict_slots
@strict_slots
class C:
    def __init__(self):
        [self.x, self.y] = 42, 100
    """
        mod = self.compile_to_strict(code)
        inst = mod.C()
        self.assertEqual(inst.x, 42)
        self.assertEqual(inst.y, 100)
        with self.assertRaises(AttributeError):
            inst.z = 100

    def test_init_seq_nested(self) -> None:
        """__init__ assignemnts are initialized"""
        code = """
from __strict__ import strict_slots
@strict_slots
class C:
    def __init__(self):
        [self.x, (self.y, self.z)] = 42, (100, 200)
    """
        mod = self.compile_to_strict(code)
        inst = mod.C()
        self.assertEqual(inst.x, 42)
        self.assertEqual(inst.y, 100)
        self.assertEqual(inst.z, 200)
        with self.assertRaises(AttributeError):
            inst.w = 100

    def test_init_self_renamed(self) -> None:
        """self doesn't need to be called self..."""
        code = """
from __strict__ import strict_slots
@strict_slots
class C:
    def __init__(weirdo):
        weirdo.x = 42
    """
        mod = self.compile_to_strict(code)
        inst = mod.C()
        self.assertEqual(inst.x, 42)
        with self.assertRaises(AttributeError):
            inst.y = 100

    def test_init_ann(self) -> None:
        """__init__ annotated assignemnts are initialized"""
        code = """
from __strict__ import strict_slots
@strict_slots
class C:
    def __init__(self):
        self.x: int = 42
        self.y: int
        self.init_y()

    def init_y(self):
        self.y = 100
    """
        mod = self.compile_to_strict(code)
        inst = mod.C()
        self.assertEqual(inst.x, 42)
        self.assertEqual(inst.y, 100)

    def test_init_ann_self_renamed(self) -> None:
        """self doesn't need to be called self..."""
        code = """
from __strict__ import strict_slots
@strict_slots
class C:
    def __init__(weirdo):
        weirdo.x: int = 42
    """
        mod = self.compile_to_strict(code)
        inst = mod.C()
        self.assertEqual(inst.x, 42)

    def test_class_ann(self) -> None:
        """class annotations w/o assignments get promoted to instance vars"""
        code = """
from __strict__ import strict_slots
@strict_slots
class C:
    x: int
    def __init__(self):
        self.init_x()

    def init_x(self):
        self.x = 42
    """
        mod = self.compile_to_strict(code)
        inst = mod.C()
        self.assertEqual(inst.x, 42)
        with self.assertRaises(AttributeError):
            inst.y = 100

    def test_class_ann_assigned(self) -> None:
        """class annotations w/ assignments aren't promoted"""
        code = """
from __strict__ import strict_slots
@strict_slots
class C:
    x: int = 42
    """
        mod = self.compile_to_strict(code)
        inst = mod.C()
        with self.assertRaises(AttributeError):

            inst.x = 100

    def test_no_class_ann(self) -> None:
        """only __init__ assignments count"""
        code = """
from __strict__ import strict_slots
@strict_slots
class C:
    def __init__(self):
        self.init_x()

    def init_x(self):
        self.x = 42
    """
        mod = self.compile_to_strict(code)
        with self.assertRaises(AttributeError):
            mod.C()

    def test_bad_init(self) -> None:
        """__init__ is missing self"""
        code = """
from __strict__ import strict_slots
@strict_slots
class C:
    def __init__():
        self.x = 42
    """
        mod = self.compile_to_strict(code)
        with self.assertRaises(TypeError):
            mod.C()

    def test_bad_init_ann(self) -> None:
        """__init__ is missing self"""
        code = """
from __strict__ import strict_slots
@strict_slots
class C:
    def __init__():
        self.x: int = 42
    """
        mod = self.compile_to_strict(code)
        with self.assertRaises(TypeError):
            mod.C()

    def test_fixed_module_import(self) -> None:
        code = """
from typing import TypeVar
x = TypeVar('foo')
    """
        modules = {"typing": {"TypeVar": TypeVar}}
        globals = {"__name__": "test_fixed_module_import"}
        mod = self.compile_to_strict(code, modules=modules, globals=globals)
        self.assertEqual(mod.x.__name__, "foo")

    def test_fixed_module_import_replaced(self) -> None:
        @final
        class FakeTypeVar:
            def __init__(self, value: str) -> None:
                self.value = value

        code = """
from typing import TypeVar
x = TypeVar('foo')
    """
        mod = self.compile_to_strict(code, modules={"typing": {"TypeVar": FakeTypeVar}})
        self.assertEqual(mod.x.value, "foo")

    def test_fixed_module_import_multiple_values(self) -> None:
        code = """
from typing import TypeVar, Dict
"""
        mod = self.compile_to_strict(
            code, modules={"typing": {"TypeVar": 42, "Dict": 100}}
        )
        self.assertEqual(mod.TypeVar, 42)
        self.assertEqual(mod.Dict, 100)

    def test_fixed_module_unknown(self) -> None:
        code = """
from typing import collections
    """
        mod = self.compile_to_strict(code, modules={"typing": {"TypeVar": TypeVar}})
        self.assertEqual(type(mod.collections), ModuleType)

    def test_fixed_module_mixed_unknown(self) -> None:
        code = """
from typing import collections, TypeVar
x = TypeVar('foo')
    """
        modules = {"typing": {"TypeVar": TypeVar}}
        globals = {"__name__": "test_fixed_module_mixed_unknown"}
        mod = self.compile_to_strict(code, modules=modules, globals=globals)
        self.assertEqual(type(mod.collections), ModuleType)
        self.assertEqual(mod.x.__name__, "foo")

    def test_private_members(self) -> None:
        code = """
from __strict__ import strict_slots
@strict_slots
class C:
    def __x(self):
        return 42

    def g(self):
        return self.__x()
    """
        mod = self.compile_to_strict(code)
        a = mod.C()
        self.assertEqual(a.g(), 42)
        self.assertEqual(a._C__x(), 42)

    def test_dotted_from_imports(self) -> None:
        code = """
from xml.dom import SyntaxErr
    """
        mod = self.compile_to_strict(code)
        self.assertEqual(type(mod.SyntaxErr), type)

    def test_dotted_imports(self) -> None:
        code = """
import xml.dom
    """
        mod = self.compile_to_strict(code)
        self.assertEqual(type(mod.xml), ModuleType)

    def test_future_imports(self) -> None:
        code = """
from __future__ import annotations
def f():
    def g() -> doesntexist:
        return 1
    return g
    """
        mod = self.compile_to_strict(code)
        self.assertEqual(1, mod.f()())

        code = """
def f():
    def g() -> doesntexist:
        return 1
    return g
    """
        mod = self.compile_to_strict(code)
        with self.assertRaises(NameError):
            mod.f()

    def test_decorator_with_generator(self) -> None:
        code = """
def mydec(gen):
    def myfunc(x):
        for i in gen:
            return x
    return myfunc


@mydec(x for x in (1, 2, 3))
def f():
    return 42
"""
        mod = self.compile_to_strict(code)
        self.assertEqual(mod.f(), 42)

    def test_lambda(self) -> None:
        code = """
x = lambda: 42
"""
        mod = self.compile_to_strict(code)
        self.assertEqual(mod.x(), 42)

    def test_nested_lambda(self) -> None:
        code = """
def f():
    return lambda: 42
"""
        mod = self.compile_to_strict(code)
        self.assertEqual(mod.f()(), 42)

    def test_nested_lambdas(self) -> None:
        code = """
def f():
    return lambda: 42, lambda: 100
"""
        mod = self.compile_to_strict(code)
        self.assertEqual(mod.f()[0](), 42)
        self.assertEqual(mod.f()[1](), 100)

    def test_nested_lambdas_and_funcs(self) -> None:
        code = """
def f():
    x = lambda: 42
    def f():
        return 100
    return x, f
"""
        mod = self.compile_to_strict(code)
        self.assertEqual(mod.f()[0](), 42)
        self.assertEqual(mod.f()[1](), 100)

    def test_async_func(self) -> None:
        code = """
async def f():
    pass
"""
        mod = self.compile_to_strict(code)
        self.assertEqual(type(mod.f()), CoroutineType)

    def test_async_func_shadowed(self) -> None:
        code = """
async def min():
    pass
"""
        mod = self.compile_to_strict(code)
        self.assertEqual(type(mod.min()), CoroutineType)

    def test_class_shadowed(self) -> None:
        code = """
from __strict__ import strict_slots
@strict_slots
class min:
    pass
"""
        mod = self.compile_to_strict(code)
        self.assertEqual(type(mod.min), type)

    def test_func_shadowed(self) -> None:
        code = """
def min():
    return 'abc'
"""
        mod = self.compile_to_strict(code)
        self.assertEqual(mod.min(), "abc")

    def test_accessed_before_shadowed(self) -> None:
        code = """
x = min(1,2)
def min():
    return 'abc'
"""
        mod = self.compile_to_strict(code)
        self.assertEqual(mod.x, 1)
        self.assertEqual(mod.min(), "abc")

    def test_deleted_shadowed_func(self) -> None:
        code = """
def min():
    return 'abc'
del min
x = min(1,2)
"""
        mod = self.compile_to_strict(code)
        self.assertEqual(mod.x, 1)
        self.assertFalse(hasattr(mod, "min"))

    def test_deleted_shadowed_async_func(self) -> None:
        code = """
async def min():
    return 'abc'
del min
x = min(1,2)
"""
        mod = self.compile_to_strict(code)
        self.assertEqual(mod.x, 1)
        self.assertFalse(hasattr(mod, "min"))

    def test_deleted_shadowed_class(self) -> None:
        code = """
class min:
    pass
del min
x = min(1,2)
"""
        mod = self.compile_to_strict(code)
        self.assertEqual(mod.x, 1)
        self.assertFalse(hasattr(mod, "min"))

    def test_async_func_first(self) -> None:
        code = """
async def f():
    pass

def g():
    pass
"""
        mod = self.compile_to_strict(code)
        self.assertEqual(type(mod.f()), CoroutineType)
        self.assertEqual(type(mod.g), FunctionType)

    def test_explicit_dict(self) -> None:
        """declaring __dict__ at the class level allows arbitrary attributes to be added"""
        code = """
from typing import Dict, Any
from __strict__ import strict_slots
@strict_slots
class C:
    __dict__: Dict[str, Any]
    """
        mod = self.compile_to_strict(
            code, modules={"typing": {"Dict": Dict, "Any": Any}}
        )
        inst = mod.C()
        inst.x = 100

    def test_explicit_weakref(self) -> None:
        """declaring __weakref__ at the class level allows weak references"""
        code = """
from typing import Any
from __strict__ import strict_slots
@strict_slots
class C:
    __weakref__: Any
    """
        mod = self.compile_to_strict(code, modules={"typing": {"Any": Any}})
        inst = mod.C()
        r = ref(inst)
        self.assertEqual(r(), inst)

    def test_weakref(self) -> None:
        """lack of __weakref__ disallows weak references to instances"""
        code = """
from __strict__ import strict_slots
@strict_slots
class C:
    pass
    """
        mod = self.compile_to_strict(code, modules={"typing": {"Any": Any}})
        with self.assertRaises(TypeError):
            ref(mod.C())


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
