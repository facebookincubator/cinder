# Copyright (c) Meta Platforms, Inc. and affiliates. (http://www.meta.com)

from __future__ import annotations

import ast
import builtins
from ast import AST
from types import CodeType
from typing import Any, Dict, Optional, Tuple, Type, TYPE_CHECKING

from .. import consts
from ..errors import ErrorSink
from ..optimizer import AstOptimizer
from ..pycodegen import find_futures
from ..strict import _static_module_ported
from ..symbols import SymbolVisitor
from .declaration_visitor import DeclarationVisitor
from .module_table import ModuleTable
from .type_binder import TypeBinder
from .types import (
    BoxFunction,
    CastFunction,
    Class,
    CRangeFunction,
    ExtremumFunction,
    IdentityDecorator,
    IsInstanceFunction,
    IsSubclassFunction,
    LenFunction,
    NumClass,
    Object,
    ProdAssertFunction,
    reflect_builtin_function,
    RevealTypeFunction,
    SortedFunction,
    TypeEnvironment,
    TypeName,
    UnboxFunction,
    Value,
)

if _static_module_ported:
    from _static import posix_clock_gettime_ns, rand, RAND_MAX
else:
    # pyre-fixme [21]: Could not find a name posix_clock_gettime_ns
    from __static__ import posix_clock_gettime_ns, rand, RAND_MAX


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
        if isinstance(slice, ast.Constant):
            svalue = slice.value
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
            "ArithmeticError": self.type_env.arithmetic_error,
            "AssertionError": self.type_env.assertion_error,
            "AttributeError": self.type_env.attribute_error,
            "BaseException": self.type_env.base_exception.exact_type(),
            "BlockingIOError": self.type_env.blocking_io_error,
            "BrokenPipeError": self.type_env.broken_pipe_error,
            "BufferError": self.type_env.buffer_error,
            "BytesWarning": self.type_env.bytes_warning,
            "ChildProcessError": self.type_env.child_process_error,
            "ConnectionAbortedError": self.type_env.connection_aborted_error,
            "ConnectionError": self.type_env.connection_error,
            "ConnectionRefusedError": self.type_env.connection_refused_error,
            "ConnectionResetError": self.type_env.connection_reset_error,
            "DeprecationWarning": self.type_env.deprecation_warning,
            "EOFError": self.type_env.eof_error,
            "Ellipsis": self.type_env.DYNAMIC,
            "EnvironmentError": self.type_env.environment_error,
            "Exception": self.type_env.exception.exact_type(),
            "FileExistsError": self.type_env.file_exists_error,
            "FileNotFoundError": self.type_env.file_not_found_error,
            "FloatingPointError": self.type_env.floating_point_error,
            "FutureWarning": self.type_env.future_warning,
            "GeneratorExit": self.type_env.generator_exit,
            "IOError": self.type_env.io_error,
            "ImportError": self.type_env.import_error,
            "ImportWarning": self.type_env.import_warning,
            "IndentationError": self.type_env.indentation_error,
            "IndexError": self.type_env.index_error,
            "InterruptedError": self.type_env.interrupted_error,
            "IsADirectoryError": self.type_env.is_a_directory_error,
            "KeyError": self.type_env.key_error,
            "KeyboardInterrupt": self.type_env.keyboard_interrupt,
            "LookupError": self.type_env.lookup_error,
            "MemoryError": self.type_env.memory_error,
            "ModuleNotFoundError": self.type_env.module_not_found_error,
            "NameError": self.type_env.name_error,
            "None": self.type_env.none.instance,
            "NotADirectoryError": self.type_env.not_a_directory_error,
            "NotImplemented": self.type_env.not_implemented,
            "NotImplementedError": self.type_env.not_implemented_error,
            "OSError": self.type_env.os_error,
            "OverflowError": self.type_env.overflow_error,
            "PendingDeprecationWarning": self.type_env.pending_deprecation_warning,
            "PermissionError": self.type_env.permission_error,
            "ProcessLookupError": self.type_env.process_lookup_error,
            "RecursionError": self.type_env.recursion_error,
            "ReferenceError": self.type_env.reference_error,
            "ResourceWarning": self.type_env.resource_warning,
            "RuntimeError": self.type_env.runtime_error,
            "RuntimeWarning": self.type_env.runtime_warning,
            "StopAsyncIteration": self.type_env.stop_async_iteration,
            "StopIteration": self.type_env.stop_iteration,
            "SyntaxError": self.type_env.syntax_error,
            "SyntaxWarning": self.type_env.syntax_warning,
            "SystemExit": self.type_env.system_exit,
            "TabError": self.type_env.tab_error,
            "TimeoutError": self.type_env.timeout_error,
            "TypeError": self.type_env.type_error,
            "UnboundLocalError": self.type_env.unbound_local_error,
            "UnicodeDecodeError": self.type_env.unicode_decode_error,
            "UnicodeEncodeError": self.type_env.unicode_encode_error,
            "UnicodeError": self.type_env.unicode_error,
            "UnicodeTranslateError": self.type_env.unicode_translate_error,
            "UnicodeWarning": self.type_env.unicode_warning,
            "UserWarning": self.type_env.user_warning,
            "ValueError": self.type_env.value_error,
            "Warning": self.type_env.warning,
            "ZeroDivisionError": self.type_env.zero_division_error,
            "__name__": self.type_env.str.instance,
            "bool": self.type_env.bool.exact_type(),
            "bytes": self.type_env.bytes.exact_type(),
            "bytearray": self.type_env.DYNAMIC,
            "classmethod": self.type_env.class_method,
            "complex": self.type_env.complex.exact_type(),
            "delattr": self.type_env.DYNAMIC,
            "dict": self.type_env.dict.exact_type(),
            "enumerate": self.type_env.DYNAMIC,
            "exit": self.type_env.DYNAMIC,
            "float": self.type_env.float.exact_type(),
            "frozenset": self.type_env.frozenset.exact_type(),
            "getattr": self.type_env.DYNAMIC,
            "globals": self.type_env.DYNAMIC,
            "hasattr": self.type_env.DYNAMIC,
            "memoryview": self.type_env.DYNAMIC,
            "int": self.type_env.int.exact_type(),
            "isinstance": IsInstanceFunction(self.type_env),
            "issubclass": IsSubclassFunction(self.type_env),
            "len": LenFunction(self.type_env.function, boxed=True),
            "list": self.type_env.list.exact_type(),
            "locals": self.type_env.DYNAMIC,
            "max": ExtremumFunction(self.type_env.function, is_min=False),
            "min": ExtremumFunction(self.type_env.function, is_min=True),
            "object": self.type_env.object.exact_type(),
            "property": self.type_env.property.exact_type(),
            "quit": self.type_env.DYNAMIC,
            "range": self.type_env.range,
            "reveal_type": RevealTypeFunction(self.type_env),
            "set": self.type_env.set.exact_type(),
            "setattr": self.type_env.DYNAMIC,
            "slice": self.type_env.DYNAMIC,
            "sorted": SortedFunction(self.type_env.function),
            "staticmethod": self.type_env.static_method,
            "str": self.type_env.str.exact_type(),
            "super": self.type_env.super,
            "tuple": self.type_env.tuple.exact_type(),
            "type": self.type_env.type.exact_type(),
            "<mutable>": IdentityDecorator(
                TypeName("__strict__", "<mutable>"), self.type_env
            ),
            "filter": self.type_env.DYNAMIC,
            "map": self.type_env.DYNAMIC,
            "reversed": self.type_env.DYNAMIC,
            "zip": self.type_env.DYNAMIC,
            # pyre-ignore[6]: Pyre can't know this callable is a BuiltinFunctionType
            "abs": reflect_builtin_function(abs, None, self.type_env),
            # pyre-ignore[6]: Pyre can't know this callable is a BuiltinFunctionType
            "all": reflect_builtin_function(all, None, self.type_env),
            # pyre-ignore[6]: Pyre can't know this callable is a BuiltinFunctionType
            "any": reflect_builtin_function(any, None, self.type_env),
            # pyre-ignore[6]: Pyre can't know this callable is a BuiltinFunctionType
            "ascii": reflect_builtin_function(ascii, None, self.type_env),
            # pyre-ignore[6]: Pyre can't know this callable is a BuiltinFunctionType
            "bin": reflect_builtin_function(bin, None, self.type_env),
            # pyre-ignore[6]: Pyre can't know this callable is a BuiltinFunctionType
            "breakpoint": reflect_builtin_function(breakpoint, None, self.type_env),
            # pyre-ignore[6]: Pyre can't know this callable is a BuiltinFunctionType
            "callable": reflect_builtin_function(callable, None, self.type_env),
            # pyre-ignore[6]: Pyre can't know this callable is a BuiltinFunctionType
            "chr": reflect_builtin_function(chr, None, self.type_env),
            "compile": reflect_builtin_function(compile, None, self.type_env),
            # pyre-ignore[6]: Pyre can't know this callable is a BuiltinFunctionType
            "dir": reflect_builtin_function(dir, None, self.type_env),
            "divmod": reflect_builtin_function(divmod, None, self.type_env),
            # pyre-ignore[6]: Pyre can't know this callable is a BuiltinFunctionType
            "eval": reflect_builtin_function(eval, None, self.type_env),
            # pyre-ignore[6]: Pyre can't know this callable is a BuiltinFunctionType
            "exec": reflect_builtin_function(exec, None, self.type_env),
            # pyre-ignore[6]: Pyre can't know this callable is a BuiltinFunctionType
            "format": reflect_builtin_function(format, None, self.type_env),
            # pyre-ignore[6]: Pyre can't know this callable is a BuiltinFunctionType
            "hash": reflect_builtin_function(hash, None, self.type_env),
            # pyre-ignore[6]: Pyre can't know this callable is a BuiltinFunctionType
            "hex": reflect_builtin_function(hex, None, self.type_env),
            # pyre-ignore[6]: Pyre can't know this callable is a BuiltinFunctionType
            "id": reflect_builtin_function(id, None, self.type_env),
            # pyre-ignore[6]: Pyre can't know this callable is a BuiltinFunctionType
            "input": reflect_builtin_function(input, None, self.type_env),
            "iter": reflect_builtin_function(iter, None, self.type_env),
            "next": reflect_builtin_function(next, None, self.type_env),
            # pyre-ignore[6]: Pyre can't know this callable is a BuiltinFunctionType
            "oct": reflect_builtin_function(oct, None, self.type_env),
            "open": reflect_builtin_function(open, None, self.type_env),
            # pyre-ignore[6]: Pyre can't know this callable is a BuiltinFunctionType
            "ord": reflect_builtin_function(ord, None, self.type_env),
            "pow": reflect_builtin_function(pow, None, self.type_env),
            "print": reflect_builtin_function(print, None, self.type_env),
            # pyre-ignore[6]: Pyre can't know this callable is a BuiltinFunctionType
            "repr": reflect_builtin_function(repr, None, self.type_env),
            "round": reflect_builtin_function(round, None, self.type_env),
            "sum": reflect_builtin_function(sum, None, self.type_env),
            "vars": reflect_builtin_function(vars, None, self.type_env),
        }
        strict_builtins = StrictBuiltins(builtins_children, self.type_env)
        typing_children = {
            "Annotated": self.type_env.annotated,
            "Any": self.type_env.dynamic,
            "Awaitable": self.type_env.dynamic,
            "Callable": self.type_env.dynamic,
            "Coroutine": self.type_env.dynamic,
            "ClassVar": self.type_env.classvar,
            "Collection": self.type_env.dynamic,
            # TODO: Need typed members for dict
            "Dict": self.type_env.dict,
            "List": self.type_env.list,
            "Final": self.type_env.final,
            "final": self.type_env.final_method,
            "Generic": self.type_env.dynamic,
            "Iterable": self.type_env.dynamic,
            "Iterator": self.type_env.dynamic,
            "Literal": self.type_env.literal,
            "NamedTuple": self.type_env.named_tuple,
            "Protocol": self.type_env.protocol,
            "Optional": self.type_env.optional,
            "overload": self.type_env.overload,
            "Union": self.type_env.union,
            "Tuple": self.type_env.tuple,
            "Type": self.type_env.dynamic,
            "TypedDict": self.type_env.typed_dict,
            "TypeVar": self.type_env.dynamic,
            "TYPE_CHECKING": self.type_env.bool.instance,
        }
        typing_extensions_children: Dict[str, Value] = {
            "Annotated": self.type_env.annotated,
            "Protocol": self.type_env.protocol,
            "TypedDict": self.type_env.typed_dict,
        }
        strict_modules_children: Dict[str, Value] = {
            "mutable": IdentityDecorator(
                TypeName("__strict__", "mutable"), self.type_env
            ),
            "strict_slots": IdentityDecorator(
                TypeName("__strict__", "strict_slots"), self.type_env
            ),
            "loose_slots": IdentityDecorator(
                TypeName("__strict__", "loose_slots"), self.type_env
            ),
            "freeze_type": IdentityDecorator(
                TypeName("__strict__", "freeze_type"), self.type_env
            ),
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
                "CheckedDict": self.type_env.checked_dict.exact_type(),
                "CheckedList": self.type_env.checked_list.exact_type(),
                "Enum": self.type_env.enum,
                "IntEnum": self.type_env.int_enum,
                "StringEnum": self.type_env.string_enum,
                "allow_weakrefs": self.type_env.allow_weakrefs,
                "box": BoxFunction(self.type_env.function),
                "cast": CastFunction(self.type_env.function),
                "clen": LenFunction(self.type_env.function, boxed=False),
                "crange": CRangeFunction(self.type_env.function),
                "ExcContextDecorator": self.type_env.exc_context_decorator.exact_type(),
                "ContextDecorator": self.type_env.context_decorator.exact_type(),
                "dynamic_return": self.type_env.dynamic_return,
                "size_t": self.type_env.uint64.exact_type(),
                "ssize_t": self.type_env.int64.exact_type(),
                "cbool": self.type_env.cbool.exact_type(),
                "inline": self.type_env.inline,
                # This is a way to disable the static compiler for
                # individual functions/methods
                "_donotcompile": self.type_env.donotcompile,
                "int8": self.type_env.int8.exact_type(),
                "int16": self.type_env.int16.exact_type(),
                "int32": self.type_env.int32.exact_type(),
                "int64": self.type_env.int64.exact_type(),
                "uint8": self.type_env.uint8.exact_type(),
                "uint16": self.type_env.uint16.exact_type(),
                "uint32": self.type_env.uint32.exact_type(),
                "uint64": self.type_env.uint64.exact_type(),
                "char": self.type_env.char.exact_type(),
                "double": self.type_env.double.exact_type(),
                "unbox": UnboxFunction(self.type_env.function),
                "checked_dicts": self.type_env.bool.instance,
                "prod_assert": ProdAssertFunction(self.type_env),
                "pydict": self.type_env.dict.exact_type(),
                "PyDict": self.type_env.dict.exact_type(),
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
                "native": self.type_env.native_decorator,
            },
        )

        self.modules["dataclasses"] = ModuleTable(
            "dataclasses",
            "Lib/dataclasses.py",
            self,
            {
                "dataclass": self.type_env.dataclass,
                "Field": self.type_env.dataclass_field,
                "InitVar": self.type_env.initvar,
                "dataclass": self.type_env.dataclass,
                "field": self.type_env.dataclass_field_function,
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
                    "spamobj": spam_obj.exact_type(),
                    "XXGeneric": self.type_env.xx_generic.exact_type(),
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
    ) -> Tuple[ast.Module, SymbolVisitor]:
        cached_tree = self.ast_cache.get(name)
        if cached_tree is None:
            tree = self.add_module(name, filename, tree, optimize)
        else:
            tree = cached_tree
        # Analyze variable scopes
        future_flags = find_futures(0, tree)
        s = self.code_generator._SymbolVisitor(future_flags)
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
        graph = self.code_generator.flow_graph(name, filename, s.scopes[tree])
        graph.setFlag(consts.CO_STATICALLY_COMPILED)
        graph.extra_consts.append(tuple(self.modules[name].imported_from.items()))

        future_flags = find_futures(0, tree)

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
            future_flags=future_flags,
        )
        code_gen.visit(tree)
        return code_gen

    def import_module(self, name: str, optimize: int) -> Optional[ModuleTable]:
        pass
