# Copyright (c) Facebook, Inc. and its affiliates. (http://www.facebook.com)
from __future__ import annotations

import ast
import builtins
from ast import AST
from types import CodeType
from typing import Any, cast, Optional, Dict, Tuple, Type, TYPE_CHECKING

from _static import (
    posix_clock_gettime_ns,
    RAND_MAX,
    rand,
)

from .. import consts
from ..errors import ErrorSink
from ..optimizer import AstOptimizer
from ..symbols import SymbolVisitor
from .declaration_visitor import DeclarationVisitor
from .module_table import ModuleTable
from .type_binder import TypeBinder
from .type_env import TypeEnvironment
from .types import (
    BuiltinTypes,
    BUILTIN_TYPES,
    BoxFunction,
    CastFunction,
    Class,
    DYNAMIC,
    ExtremumFunction,
    IsInstanceFunction,
    IsSubclassFunction,
    LenFunction,
    NumClass,
    Object,
    ProdAssertFunction,
    RevealTypeFunction,
    SortedFunction,
    TypeName,
    UnboxFunction,
    Value,
    XX_GENERIC_TYPE,
    reflect_builtin_function,
)

if TYPE_CHECKING:
    from . import Static38CodeGenerator

try:
    import xxclassloader  # pyre-ignore[21]: unknown module
except ImportError:
    xxclassloader = None


class StrictBuiltins(Object[Class]):
    def __init__(self, builtins: Dict[str, Value], builtin_types: BuiltinTypes) -> None:
        super().__init__(builtin_types.dict)
        self.builtins = builtins

    def bind_subscr(
        self,
        node: ast.Subscript,
        type: Value,
        visitor: TypeBinder,
        type_ctx: Optional[Class] = None,
    ) -> None:
        slice = node.slice
        type = DYNAMIC
        if isinstance(slice, ast.Index):
            val = slice.value
            if isinstance(val, ast.Str):
                builtin = self.builtins.get(val.s)
                if builtin is not None:
                    type = builtin
            elif isinstance(val, ast.Constant):
                svalue = val.value
                if isinstance(svalue, str):
                    builtin = self.builtins.get(svalue)
                    if builtin is not None:
                        type = builtin

        visitor.set_type(node, type)


class Compiler:
    def __init__(
        self,
        code_generator: Type[Static38CodeGenerator],
        error_sink: Optional[ErrorSink] = None,
    ) -> None:
        self.modules: Dict[str, ModuleTable] = {}
        self.ast_cache: Dict[str, ast.Module] = {}
        self.code_generator = code_generator
        self.error_sink: ErrorSink = error_sink or ErrorSink()
        self.type_env: TypeEnvironment = TypeEnvironment()
        self.builtin_types: BuiltinTypes = BUILTIN_TYPES
        self.builtin_types.post_process(self.type_env)
        rand_max = NumClass(
            TypeName("builtins", "int"),
            self.builtin_types,
            pytype=int,
            literal_value=RAND_MAX,
            is_final=True,
        )
        builtins_children = {
            "object": self.builtin_types.object,
            "type": self.builtin_types.type,
            "None": self.builtin_types.none.instance,
            "int": self.builtin_types.int_exact,
            "classmethod": self.builtin_types.class_method,
            "complex": self.builtin_types.complex_exact,
            "str": self.builtin_types.str_exact,
            "bytes": self.builtin_types.bytes,
            "bool": self.builtin_types.bool,
            "float": self.builtin_types.float_exact,
            "len": LenFunction(self.builtin_types.function, boxed=True),
            "min": ExtremumFunction(self.builtin_types.function, is_min=True),
            "max": ExtremumFunction(self.builtin_types.function, is_min=False),
            "list": self.builtin_types.list_exact,
            "tuple": self.builtin_types.tuple_exact,
            "set": self.builtin_types.set_exact,
            "sorted": SortedFunction(self.builtin_types.function),
            "Exception": self.builtin_types.exception,
            "BaseException": self.builtin_types.base_exception,
            "isinstance": IsInstanceFunction(self.builtin_types),
            "issubclass": IsSubclassFunction(self.builtin_types),
            "staticmethod": self.builtin_types.static_method,
            "reveal_type": RevealTypeFunction(self.builtin_types),
            "property": self.builtin_types.property,
            "<mutable>": self.builtin_types.identity_decorator,
        }
        strict_builtins = StrictBuiltins(builtins_children, self.builtin_types)
        typing_children = {
            "ClassVar": self.builtin_types.classvar,
            # TODO: Need typed members for dict
            "Dict": self.builtin_types.dict,
            "List": self.builtin_types.list,
            "Final": self.builtin_types.final,
            "final": self.builtin_types.final_method,
            "NamedTuple": self.builtin_types.named_tuple,
            "Protocol": self.builtin_types.protocol,
            "Optional": self.builtin_types.optional,
            "Union": self.builtin_types.union,
            "Tuple": self.builtin_types.tuple,
            "TYPE_CHECKING": self.builtin_types.bool.instance,
        }
        typing_extensions_children: Dict[str, Value] = {
            "Annotated": self.builtin_types.annotated,
        }
        strict_modules_children: Dict[str, Value] = {
            "mutable": self.builtin_types.identity_decorator,
            "strict_slots": self.builtin_types.identity_decorator,
            "loose_slots": self.builtin_types.identity_decorator,
            "freeze_type": self.builtin_types.identity_decorator,
        }

        builtins_children["<builtins>"] = strict_builtins
        self.modules["strict_modules"] = self.modules["__strict__"] = ModuleTable(
            "strict_modules", "<strict-modules>", self, strict_modules_children
        )
        fixed_modules: Dict[str, Value] = {
            "typing": StrictBuiltins(typing_children, self.builtin_types),
            "typing_extensions": StrictBuiltins(
                typing_extensions_children, self.builtin_types
            ),
            "__strict__": StrictBuiltins(strict_modules_children, self.builtin_types),
            "strict_modules": StrictBuiltins(
                dict(strict_modules_children), self.builtin_types
            ),
        }

        builtins_children["<fixed-modules>"] = StrictBuiltins(
            fixed_modules, self.builtin_types
        )

        self.builtins = self.modules["builtins"] = ModuleTable(
            "builtins",
            "<builtins>",
            self,
            builtins_children,
        )
        self.typing = self.modules["typing"] = ModuleTable(
            "typing", "<typing>", self, typing_children
        )
        self.modules["typing_extensions"] = ModuleTable(
            "typing_extensions", "<typing_extensions>", self, typing_extensions_children
        )
        self.statics = self.modules["__static__"] = ModuleTable(
            "__static__",
            "<__static__>",
            self,
            {
                "Array": self.builtin_types.array,
                "CheckedDict": self.builtin_types.checked_dict,
                "CheckedList": self.builtin_types.checked_list,
                "Enum": self.builtin_types.enum,
                "allow_weakrefs": self.builtin_types.allow_weakrefs,
                "box": BoxFunction(self.builtin_types.function),
                "cast": CastFunction(self.builtin_types.function),
                "clen": LenFunction(self.builtin_types.function, boxed=False),
                "ContextDecorator": self.builtin_types.context_decorator,
                "dynamic_return": self.builtin_types.dynamic_return,
                "size_t": self.builtin_types.uint64,
                "ssize_t": self.builtin_types.int64,
                "cbool": self.builtin_types.cbool,
                "inline": self.builtin_types.inline,
                # This is a way to disable the static compiler for
                # individual functions/methods
                "_donotcompile": self.builtin_types.donotcompile,
                "int8": self.builtin_types.int8,
                "int16": self.builtin_types.int16,
                "int32": self.builtin_types.int32,
                "int64": self.builtin_types.int64,
                "uint8": self.builtin_types.uint8,
                "uint16": self.builtin_types.uint16,
                "uint32": self.builtin_types.uint32,
                "uint64": self.builtin_types.uint64,
                "char": self.builtin_types.char,
                "double": self.builtin_types.double,
                "unbox": UnboxFunction(self.builtin_types.function),
                "checked_dicts": self.builtin_types.bool.instance,
                "prod_assert": ProdAssertFunction(self.builtin_types),
                "pydict": self.builtin_types.dict,
                "PyDict": self.builtin_types.dict,
                "Vector": self.builtin_types.vector,
                "RAND_MAX": rand_max.instance,
                "posix_clock_gettime_ns": reflect_builtin_function(
                    # pyre-ignore[6]: Pyre can't know this callable is a BuiltinFunctionType
                    posix_clock_gettime_ns,
                    None,
                    self.type_env,
                    self.builtin_types,
                ),
                "rand": reflect_builtin_function(
                    # pyre-ignore[6]: Pyre can't know this callable is a BuiltinFunctionType
                    rand,
                    None,
                    self.type_env,
                    self.builtin_types,
                ),
                "set_type_static": DYNAMIC,
            },
        )

        self.modules["cinder"] = ModuleTable(
            "cinder",
            "<cinder>",
            self,
            {
                "cached_property": self.builtin_types.cached_property,
                "async_cached_property": self.builtin_types.async_cached_property,
            },
        )

        if xxclassloader is not None:
            spam_obj = self.builtin_types.spam_obj
            assert spam_obj is not None
            self.modules["xxclassloader"] = ModuleTable(
                "xxclassloader",
                "<xxclassloader>",
                self,
                {
                    "spamobj": spam_obj,
                    "XXGeneric": XX_GENERIC_TYPE,
                    "foo": reflect_builtin_function(
                        xxclassloader.foo,
                        None,
                        self.type_env,
                        self.builtin_types,
                    ),
                    "bar": reflect_builtin_function(
                        xxclassloader.bar,
                        None,
                        self.type_env,
                        self.builtin_types,
                    ),
                    "neg": reflect_builtin_function(
                        xxclassloader.neg,
                        None,
                        self.type_env,
                        self.builtin_types,
                    ),
                },
            )

    def __getitem__(self, name: str) -> ModuleTable:
        return self.modules[name]

    def __setitem__(self, name: str, value: ModuleTable) -> None:
        self.modules[name] = value

    def add_module(
        self, name: str, filename: str, tree: AST, optimize: int
    ) -> ast.Module:
        tree = AstOptimizer(optimize=optimize > 0).visit(tree)
        self.ast_cache[name] = tree

        decl_visit = DeclarationVisitor(name, filename, self, optimize)
        decl_visit.visit(tree)
        decl_visit.finish_bind()
        return tree

    def bind(
        self,
        name: str,
        filename: str,
        tree: AST,
        optimize: int,
        enable_patching: bool = False,
    ) -> None:
        self._bind(name, filename, tree, optimize, enable_patching)

    def _bind(
        self,
        name: str,
        filename: str,
        tree: AST,
        optimize: int,
        enable_patching: bool = False,
    ) -> Tuple[AST, SymbolVisitor]:
        cached_tree = self.ast_cache.get(name)
        if cached_tree is None:
            tree = self.add_module(name, filename, tree, optimize)
        else:
            tree = cached_tree
        # Analyze variable scopes
        s = self.code_generator._SymbolVisitor()
        s.visit(tree)

        # Analyze the types of objects within local scopes
        type_binder = TypeBinder(
            s,
            filename,
            self,
            name,
            optimize,
            enable_patching=enable_patching,
        )
        type_binder.visit(tree)
        return tree, s

    def compile(
        self,
        name: str,
        filename: str,
        tree: AST,
        optimize: int,
        enable_patching: bool = False,
        builtins: Dict[str, Any] = builtins.__dict__,
    ) -> CodeType:
        code_gen = self.code_gen(
            name, filename, tree, optimize, enable_patching, builtins
        )
        return code_gen.getCode()

    def code_gen(
        self,
        name: str,
        filename: str,
        tree: AST,
        optimize: int,
        enable_patching: bool = False,
        builtins: Dict[str, Any] = builtins.__dict__,
    ) -> Static38CodeGenerator:
        tree, s = self._bind(name, filename, tree, optimize, enable_patching)
        if self.error_sink.has_errors:
            raise self.error_sink.errors[0]

        # Compile the code w/ the static compiler
        graph = self.code_generator.flow_graph(
            name, filename, s.scopes[tree], peephole_enabled=True
        )
        graph.setFlag(consts.CO_STATICALLY_COMPILED)

        code_gen = self.code_generator(
            None,
            tree,
            s,
            graph,
            self,
            name,
            flags=0,
            optimization_lvl=optimize,
            enable_patching=enable_patching,
            builtins=builtins,
        )
        code_gen.visit(tree)
        return code_gen

    def import_module(self, name: str, optimize: int) -> Optional[ModuleTable]:
        pass
