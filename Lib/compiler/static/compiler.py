# Copyright (c) Facebook, Inc. and its affiliates. (http://www.facebook.com)
from __future__ import annotations

import ast
import builtins
from ast import AST
from types import CodeType
from typing import Any, Optional, Dict, Tuple, Type, TYPE_CHECKING

from _static import (
    posix_clock_gettime_ns,
    RAND_MAX,
    rand,
)

from .. import consts
from ..errors import ErrorSink
from ..optimizer import AstOptimizer
from ..readonly.type_binder import ReadonlyTypeBinder
from ..symbols import SymbolVisitor
from .declaration_visitor import DeclarationVisitor
from .module_table import ModuleTable
from .type_binder import TypeBinder
from .types import (
    ALLOW_WEAKREFS_TYPE,
    ARRAY_TYPE,
    ASYNC_CACHED_PROPERTY_TYPE,
    BASE_EXCEPTION_TYPE,
    BOOL_TYPE,
    BUILTIN_GENERICS,
    BYTES_TYPE,
    BoxFunction,
    CBOOL_TYPE,
    CHAR_TYPE,
    CHECKED_DICT_TYPE,
    CHECKED_LIST_TYPE,
    CLASSVAR_TYPE,
    READONLY_TYPE,
    CLASS_METHOD_TYPE,
    COMPLEX_EXACT_TYPE,
    CONTEXT_DECORATOR_TYPE,
    CastFunction,
    Class,
    DICT_TYPE,
    DONOTCOMPILE_TYPE,
    DOUBLE_TYPE,
    DYNAMIC,
    DYNAMIC_RETURN_TYPE,
    ENUM_TYPE,
    EXCEPTION_TYPE,
    ExtremumFunction,
    FINAL_METHOD_TYPE,
    FINAL_TYPE,
    FLOAT_EXACT_TYPE,
    FUNCTION_TYPE,
    GenericTypesDict,
    IdentityDecorator,
    INLINE_TYPE,
    INT16_TYPE,
    INT32_TYPE,
    INT64_TYPE,
    INT8_TYPE,
    INT_EXACT_TYPE,
    IsInstanceFunction,
    IsSubclassFunction,
    LIST_EXACT_TYPE,
    LIST_TYPE,
    LenFunction,
    NAMED_TUPLE_TYPE,
    NONE_TYPE,
    NumClass,
    OBJECT_TYPE,
    OPTIONAL_TYPE,
    Object,
    PROTOCOL_TYPE,
    PROPERTY_TYPE,
    ReadonlyFunction,
    ProdAssertFunction,
    RevealTypeFunction,
    SET_EXACT_TYPE,
    SPAM_OBJ,
    STATIC_METHOD_TYPE,
    STR_EXACT_TYPE,
    SortedFunction,
    TUPLE_EXACT_TYPE,
    TUPLE_TYPE,
    TYPE_TYPE,
    TypeName,
    UINT16_TYPE,
    UINT32_TYPE,
    UINT64_TYPE,
    UINT8_TYPE,
    UNION_TYPE,
    UnboxFunction,
    VECTOR_TYPE,
    Value,
    XX_GENERIC_TYPE,
    reflect_builtin_function,
    CACHED_PROPERTY_TYPE,
)

if TYPE_CHECKING:
    from . import Static38CodeGenerator

try:
    import xxclassloader  # pyre-ignore[21]: unknown module
except ImportError:
    xxclassloader = None


class StrictBuiltins(Object[Class]):
    def __init__(self, builtins: Dict[str, Value]) -> None:
        super().__init__(DICT_TYPE)
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
        self.code_generator = code_generator
        builtins_children = {
            "object": OBJECT_TYPE,
            "type": TYPE_TYPE,
            "None": NONE_TYPE.instance,
            "int": INT_EXACT_TYPE,
            "classmethod": CLASS_METHOD_TYPE,
            "complex": COMPLEX_EXACT_TYPE,
            "str": STR_EXACT_TYPE,
            "bytes": BYTES_TYPE,
            "bool": BOOL_TYPE,
            "float": FLOAT_EXACT_TYPE,
            "len": LenFunction(FUNCTION_TYPE, boxed=True),
            "min": ExtremumFunction(FUNCTION_TYPE, is_min=True),
            "max": ExtremumFunction(FUNCTION_TYPE, is_min=False),
            "list": LIST_EXACT_TYPE,
            "tuple": TUPLE_EXACT_TYPE,
            "set": SET_EXACT_TYPE,
            "sorted": SortedFunction(FUNCTION_TYPE),
            "Exception": EXCEPTION_TYPE,
            "BaseException": BASE_EXCEPTION_TYPE,
            "isinstance": IsInstanceFunction(),
            "issubclass": IsSubclassFunction(),
            "staticmethod": STATIC_METHOD_TYPE,
            "reveal_type": RevealTypeFunction(),
            "property": PROPERTY_TYPE,
            "<mutable>": IdentityDecorator(TypeName("__strict__", "<mutable>")),
            "readonly_func": IdentityDecorator(TypeName("builtins", "readonly_func")),
            "Readonly": READONLY_TYPE,
            "readonly": ReadonlyFunction(),
        }
        strict_builtins = StrictBuiltins(builtins_children)
        typing_children = {
            "ClassVar": CLASSVAR_TYPE,
            # TODO: Need typed members for dict
            "Dict": DICT_TYPE,
            "List": LIST_TYPE,
            "Final": FINAL_TYPE,
            "final": FINAL_METHOD_TYPE,
            "NamedTuple": NAMED_TUPLE_TYPE,
            "Protocol": PROTOCOL_TYPE,
            "Optional": OPTIONAL_TYPE,
            "Union": UNION_TYPE,
            "Tuple": TUPLE_TYPE,
            "TYPE_CHECKING": BOOL_TYPE.instance,
        }
        strict_modules_children: Dict[str, Value] = {
            "mutable": IdentityDecorator(TypeName("__strict__", "mutable")),
            "strict_slots": IdentityDecorator(TypeName("__strict__", "strict_slots")),
            "loose_slots": IdentityDecorator(TypeName("__strict__", "loose_slots")),
            "freeze_type": IdentityDecorator(TypeName("__strict__", "freeze_type")),
        }

        builtins_children["<builtins>"] = strict_builtins
        self.modules["strict_modules"] = self.modules["__strict__"] = ModuleTable(
            "strict_modules", "<strict-modules>", self, strict_modules_children
        )
        fixed_modules: Dict[str, Value] = {
            "typing": StrictBuiltins(typing_children),
            "__strict__": StrictBuiltins(strict_modules_children),
            "strict_modules": StrictBuiltins(dict(strict_modules_children)),
        }

        builtins_children["<fixed-modules>"] = StrictBuiltins(fixed_modules)

        self.builtins = self.modules["builtins"] = ModuleTable(
            "builtins",
            "<builtins>",
            self,
            builtins_children,
        )
        self.typing = self.modules["typing"] = ModuleTable(
            "typing", "<typing>", self, typing_children
        )
        self.statics = self.modules["__static__"] = ModuleTable(
            "__static__",
            "<__static__>",
            self,
            {
                "Array": ARRAY_TYPE,
                "CheckedDict": CHECKED_DICT_TYPE,
                "CheckedList": CHECKED_LIST_TYPE,
                "Enum": ENUM_TYPE,
                "allow_weakrefs": ALLOW_WEAKREFS_TYPE,
                "box": BoxFunction(FUNCTION_TYPE),
                "cast": CastFunction(FUNCTION_TYPE),
                "clen": LenFunction(FUNCTION_TYPE, boxed=False),
                "ContextDecorator": CONTEXT_DECORATOR_TYPE,
                "dynamic_return": DYNAMIC_RETURN_TYPE,
                "size_t": UINT64_TYPE,
                "ssize_t": INT64_TYPE,
                "cbool": CBOOL_TYPE,
                "inline": INLINE_TYPE,
                # This is a way to disable the static compiler for
                # individual functions/methods
                "_donotcompile": DONOTCOMPILE_TYPE,
                "int8": INT8_TYPE,
                "int16": INT16_TYPE,
                "int32": INT32_TYPE,
                "int64": INT64_TYPE,
                "uint8": UINT8_TYPE,
                "uint16": UINT16_TYPE,
                "uint32": UINT32_TYPE,
                "uint64": UINT64_TYPE,
                "char": CHAR_TYPE,
                "double": DOUBLE_TYPE,
                "unbox": UnboxFunction(FUNCTION_TYPE),
                "checked_dicts": BOOL_TYPE.instance,
                "prod_assert": ProdAssertFunction(),
                "pydict": DICT_TYPE,
                "PyDict": DICT_TYPE,
                "Vector": VECTOR_TYPE,
                "RAND_MAX": NumClass(
                    TypeName("builtins", "int"),
                    pytype=int,
                    literal_value=RAND_MAX,
                    is_final=True,
                ).instance,
                "posix_clock_gettime_ns": reflect_builtin_function(
                    # pyre-ignore[6]: Pyre can't know this callable is a BuiltinFunctionType
                    posix_clock_gettime_ns
                ),
                # pyre-ignore[6]: Pyre can't know this callable is a BuiltinFunctionType
                "rand": reflect_builtin_function(rand),
                "set_type_static": DYNAMIC,
            },
        )

        self.modules["cinder"] = ModuleTable(
            "cinder",
            "<cinder>",
            self,
            {
                "cached_property": CACHED_PROPERTY_TYPE,
                "async_cached_property": ASYNC_CACHED_PROPERTY_TYPE,
            },
        )

        if xxclassloader is not None:
            self.modules["xxclassloader"] = ModuleTable(
                "xxclassloader",
                "<xxclassloader>",
                self,
                {
                    "spamobj": SPAM_OBJ,
                    "XXGeneric": XX_GENERIC_TYPE,
                    "foo": reflect_builtin_function(xxclassloader.foo),
                    "bar": reflect_builtin_function(xxclassloader.bar),
                    "neg": reflect_builtin_function(xxclassloader.neg),
                },
            )

        # We need to clone the dictionaries for each type so that as we populate
        # generic instantations that we don't store them in the global dict for
        # built-in types
        self.generic_types: GenericTypesDict = {
            k: dict(v) for k, v in BUILTIN_GENERICS.items()
        }

        if not error_sink:
            error_sink = ErrorSink()
        self.error_sink: ErrorSink = error_sink

    def __getitem__(self, name: str) -> ModuleTable:
        return self.modules[name]

    def __setitem__(self, name: str, value: ModuleTable) -> None:
        self.modules[name] = value

    def add_module(
        self, name: str, filename: str, tree: AST, optimize: int
    ) -> ast.Module:
        tree = AstOptimizer(optimize=optimize > 0).visit(tree)

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
        if name not in self.modules:
            tree = self.add_module(name, filename, tree, optimize)
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
            nodes_default_dynamic=not self.error_sink.throwing,
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
    ) -> CodeType:
        code_gen = self.code_gen(name, filename, tree, optimize, enable_patching)
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
        # Analyze using readonly type binder
        readonly_type_binder = ReadonlyTypeBinder(tree, filename, s)
        readonly_types = readonly_type_binder.get_types()
        if readonly_type_binder.error_sink.has_errors:
            raise readonly_type_binder.error_sink.errors[0]

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
            readonly_types=readonly_types,
            flags=0,
            optimization_lvl=optimize,
            enable_patching=enable_patching,
            builtins=builtins,
        )
        code_gen.visit(tree)
        return code_gen

    def import_module(self, name: str, optimize: int) -> Optional[ModuleTable]:
        pass
