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
from .types import (
    BoxFunction,
    CastFunction,
    Class,
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
    TypeEnvironment,
    UnboxFunction,
    Value,
    reflect_builtin_function,
)

if TYPE_CHECKING:
    from . import Static38CodeGenerator

try:
    import xxclassloader  # pyre-ignore[21]: unknown module
except ImportError:
    xxclassloader = None


class StrictBuiltins(Object[Class]):
    def __init__(self, builtins: Dict[str, Value], type_env: TypeEnvironment) -> None:
        super().__init__(type_env.dict)
        self.builtins = builtins

    def bind_subscr(
        self,
        node: ast.Subscript,
        type: Value,
        visitor: TypeBinder,
        type_ctx: Optional[Class] = None,
    ) -> None:
        slice = node.slice
        type = visitor.type_env.DYNAMIC
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
        rand_max = NumClass(
            TypeName("builtins", "int"),
            self.type_env,
            pytype=int,
            literal_value=RAND_MAX,
            is_final=True,
        )
        builtins_children = {
            "object": self.type_env.object,
            "type": self.type_env.type,
            "None": self.type_env.none.instance,
            "int": self.type_env.int.exact_type(),
            "classmethod": self.type_env.class_method,
            "complex": self.type_env.complex.exact_type(),
            "str": self.type_env.str.exact_type(),
            "bytes": self.type_env.bytes,
            "bool": self.type_env.bool,
            "float": self.type_env.float.exact_type(),
            "len": LenFunction(self.type_env.function, boxed=True),
            "min": ExtremumFunction(self.type_env.function, is_min=True),
            "max": ExtremumFunction(self.type_env.function, is_min=False),
            "list": self.type_env.list.exact_type(),
            "tuple": self.type_env.tuple.exact_type(),
            "set": self.type_env.set.exact_type(),
            "sorted": SortedFunction(self.type_env.function),
            "Exception": self.type_env.exception,
            "BaseException": self.type_env.base_exception,
            "isinstance": IsInstanceFunction(self.type_env),
            "issubclass": IsSubclassFunction(self.type_env),
            "staticmethod": self.type_env.static_method,
            "reveal_type": RevealTypeFunction(self.type_env),
            "property": self.type_env.property,
            "<mutable>": self.type_env.identity_decorator,
        }
        strict_builtins = StrictBuiltins(builtins_children, self.type_env)
        typing_children = {
            "ClassVar": self.type_env.classvar,
            # TODO: Need typed members for dict
            "Dict": self.type_env.dict,
            "List": self.type_env.list,
            "Final": self.type_env.final,
            "final": self.type_env.final_method,
            "NamedTuple": self.type_env.named_tuple,
            "Protocol": self.type_env.protocol,
            "Optional": self.type_env.optional,
            "Union": self.type_env.union,
            "Tuple": self.type_env.tuple,
            "TYPE_CHECKING": self.type_env.bool.instance,
        }
        typing_extensions_children: Dict[str, Value] = {
            "Annotated": self.type_env.annotated,
        }
        strict_modules_children: Dict[str, Value] = {
            "mutable": self.type_env.identity_decorator,
            "strict_slots": self.type_env.identity_decorator,
            "loose_slots": self.type_env.identity_decorator,
            "freeze_type": self.type_env.identity_decorator,
        }

        builtins_children["<builtins>"] = strict_builtins
        self.modules["strict_modules"] = self.modules["__strict__"] = ModuleTable(
            "strict_modules", "<strict-modules>", self, strict_modules_children
        )
        fixed_modules: Dict[str, Value] = {
            "typing": StrictBuiltins(typing_children, self.type_env),
            "typing_extensions": StrictBuiltins(
                typing_extensions_children, self.type_env
            ),
            "__strict__": StrictBuiltins(strict_modules_children, self.type_env),
            "strict_modules": StrictBuiltins(
                dict(strict_modules_children), self.type_env
            ),
        }

        builtins_children["<fixed-modules>"] = StrictBuiltins(
            fixed_modules, self.type_env
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
                "Array": self.type_env.array,
                "CheckedDict": self.type_env.checked_dict,
                "CheckedList": self.type_env.checked_list,
                "Enum": self.type_env.enum,
                "allow_weakrefs": self.type_env.allow_weakrefs,
                "box": BoxFunction(self.type_env.function),
                "cast": CastFunction(self.type_env.function),
                "clen": LenFunction(self.type_env.function, boxed=False),
                "ContextDecorator": self.type_env.context_decorator,
                "dynamic_return": self.type_env.dynamic_return,
                "size_t": self.type_env.uint64,
                "ssize_t": self.type_env.int64,
                "cbool": self.type_env.cbool,
                "inline": self.type_env.inline,
                # This is a way to disable the static compiler for
                # individual functions/methods
                "_donotcompile": self.type_env.donotcompile,
                "int8": self.type_env.int8,
                "int16": self.type_env.int16,
                "int32": self.type_env.int32,
                "int64": self.type_env.int64,
                "uint8": self.type_env.uint8,
                "uint16": self.type_env.uint16,
                "uint32": self.type_env.uint32,
                "uint64": self.type_env.uint64,
                "char": self.type_env.char,
                "double": self.type_env.double,
                "unbox": UnboxFunction(self.type_env.function),
                "checked_dicts": self.type_env.bool.instance,
                "prod_assert": ProdAssertFunction(self.type_env),
                "pydict": self.type_env.dict,
                "PyDict": self.type_env.dict,
                "Vector": self.type_env.vector,
                "RAND_MAX": rand_max.instance,
                "posix_clock_gettime_ns": reflect_builtin_function(
                    # pyre-ignore[6]: Pyre can't know this callable is a BuiltinFunctionType
                    posix_clock_gettime_ns,
                    None,
                    self.type_env,
                ),
                "rand": reflect_builtin_function(
                    # pyre-ignore[6]: Pyre can't know this callable is a BuiltinFunctionType
                    rand,
                    None,
                    self.type_env,
                ),
                "set_type_static": self.type_env.DYNAMIC,
            },
        )

        self.modules["cinder"] = ModuleTable(
            "cinder",
            "<cinder>",
            self,
            {
                "cached_property": self.type_env.cached_property,
                "async_cached_property": self.type_env.async_cached_property,
            },
        )

        if xxclassloader is not None:
            spam_obj = self.type_env.spam_obj
            assert spam_obj is not None
            self.modules["xxclassloader"] = ModuleTable(
                "xxclassloader",
                "<xxclassloader>",
                self,
                {
                    "spamobj": spam_obj,
                    "XXGeneric": self.type_env.xx_generic,
                    "foo": reflect_builtin_function(
                        xxclassloader.foo,
                        None,
                        self.type_env,
                    ),
                    "bar": reflect_builtin_function(
                        xxclassloader.bar,
                        None,
                        self.type_env,
                    ),
                    "neg": reflect_builtin_function(
                        xxclassloader.neg,
                        None,
                        self.type_env,
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
