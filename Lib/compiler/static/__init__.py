# Copyright (c) Facebook, Inc. and its affiliates. (http://www.facebook.com)
from __future__ import annotations

import ast
import sys
from ast import (
    AST,
    And,
    AnnAssign,
    Assign,
    AsyncFor,
    AsyncFunctionDef,
    AsyncWith,
    Attribute,
    AugAssign,
    Await,
    BinOp,
    BoolOp,
    Bytes,
    Call,
    ClassDef,
    Compare,
    Constant,
    DictComp,
    Ellipsis,
    For,
    FormattedValue,
    FunctionDef,
    GeneratorExp,
    If,
    IfExp,
    Import,
    ImportFrom,
    Index,
    Is,
    IsNot,
    JoinedStr,
    Lambda,
    ListComp,
    Module,
    Name,
    NameConstant,
    Num,
    Return,
    SetComp,
    Slice,
    Starred,
    Str,
    Subscript,
    Try,
    UnaryOp,
    While,
    With,
    Yield,
    YieldFrom,
    cmpop,
    expr,
    copy_location,
)
from contextlib import contextmanager
from enum import IntEnum
from functools import partial
from typing import (
    Callable as typingCallable,
    Collection,
    Dict,
    Generator,
    Generic,
    Iterable,
    List,
    Mapping,
    NoReturn,
    Optional,
    Sequence,
    Set,
    Tuple,
    Type,
    TypeVar,
    Union,
    cast,
)

from __static__ import chkdict  # pyre-ignore[21]: unknown module
from _static import (  # pyre-fixme[21]: Could not find module `_static`.
    TYPED_BOOL,
    TYPED_INT_8BIT,
    TYPED_INT_16BIT,
    TYPED_INT_32BIT,
    TYPED_INT_64BIT,
    TYPED_OBJECT,
    TYPED_ARRAY,
    TYPED_INT_UNSIGNED,
    TYPED_INT_SIGNED,
    TYPED_INT8,
    TYPED_INT16,
    TYPED_INT32,
    TYPED_INT64,
    TYPED_UINT8,
    TYPED_UINT16,
    TYPED_UINT32,
    TYPED_UINT64,
    SEQ_LIST,
    SEQ_TUPLE,
    SEQ_LIST_INEXACT,
    SEQ_ARRAY_INT8,
    SEQ_ARRAY_INT16,
    SEQ_ARRAY_INT32,
    SEQ_ARRAY_INT64,
    SEQ_ARRAY_UINT8,
    SEQ_ARRAY_UINT16,
    SEQ_ARRAY_UINT32,
    SEQ_ARRAY_UINT64,
    SEQ_SUBSCR_UNCHECKED,
    SEQ_REPEAT_INEXACT_SEQ,
    SEQ_REPEAT_INEXACT_NUM,
    SEQ_REPEAT_REVERSED,
    SEQ_REPEAT_PRIMITIVE_NUM,
    PRIM_OP_EQ_INT,
    PRIM_OP_NE_INT,
    PRIM_OP_LT_INT,
    PRIM_OP_LE_INT,
    PRIM_OP_GT_INT,
    PRIM_OP_GE_INT,
    PRIM_OP_LT_UN_INT,
    PRIM_OP_LE_UN_INT,
    PRIM_OP_GT_UN_INT,
    PRIM_OP_GE_UN_INT,
    PRIM_OP_EQ_DBL,
    PRIM_OP_NE_DBL,
    PRIM_OP_LT_DBL,
    PRIM_OP_LE_DBL,
    PRIM_OP_GT_DBL,
    PRIM_OP_GE_DBL,
    PRIM_OP_ADD_INT,
    PRIM_OP_SUB_INT,
    PRIM_OP_MUL_INT,
    PRIM_OP_DIV_INT,
    PRIM_OP_DIV_UN_INT,
    PRIM_OP_MOD_INT,
    PRIM_OP_MOD_UN_INT,
    PRIM_OP_LSHIFT_INT,
    PRIM_OP_RSHIFT_INT,
    PRIM_OP_RSHIFT_UN_INT,
    PRIM_OP_XOR_INT,
    PRIM_OP_OR_INT,
    PRIM_OP_AND_INT,
    PRIM_OP_NEG_INT,
    PRIM_OP_INV_INT,
    PRIM_OP_NEG_DBL,
    PRIM_OP_ADD_DBL,
    PRIM_OP_SUB_DBL,
    PRIM_OP_MUL_DBL,
    PRIM_OP_DIV_DBL,
    PRIM_OP_MOD_DBL,
    PROM_OP_POW_DBL,
    FAST_LEN_INEXACT,
    FAST_LEN_LIST,
    FAST_LEN_DICT,
    FAST_LEN_SET,
    FAST_LEN_TUPLE,
    FAST_LEN_ARRAY,
    FAST_LEN_STR,
    TYPED_DOUBLE,
    RAND_MAX,
    posix_clock_gettime_ns,
    rand,
)

from .. import symbols, opcode38static
from ..consts import SC_LOCAL, SC_GLOBAL_EXPLICIT, SC_GLOBAL_IMPLICIT
from ..opcodebase import Opcode
from ..optimizer import AstOptimizer
from ..pyassem import Block, PyFlowGraph, PyFlowGraphCinder, IndexedSet
from ..pycodegen import (
    AugAttribute,
    AugName,
    AugSubscript,
    CodeGenerator,
    CinderCodeGenerator,
    compile,
    Delegator,
    FOR_LOOP,
    wrap_aug,
)
from ..symbols import Scope, SymbolVisitor, ModuleScope, ClassScope
from ..unparse import to_expr
from .errors import ErrorSink
from .types import (
    ALLOW_WEAKREFS_TYPE,
    ARRAY_EXACT_TYPE,
    BASE_EXCEPTION_TYPE,
    BOOL_TYPE,
    BUILTIN_GENERICS,
    BYTES_TYPE,
    BoxFunction,
    BuiltinFunction,
    BuiltinFunctionType,
    CBOOL_TYPE,
    CHAR_TYPE,
    CHECKED_DICT_EXACT_TYPE,
    CHECKED_DICT_TYPE,
    CInstance,
    CLASSVAR_TYPE,
    COMPLEX_EXACT_TYPE,
    CONSTANT_TYPES,
    CType,
    CastFunction,
    CheckedDictInstance,
    Class,
    ClassVar,
    CodeType,
    DICT_EXACT_TYPE,
    DICT_TYPE,
    DONOTCOMPILE_TYPE,
    DOUBLE_TYPE,
    DYNAMIC,
    DYNAMIC_RETURN_TYPE,
    DYNAMIC_TYPE,
    DeclarationVisitor,
    EXCEPTION_TYPE,
    ExtremumFunction,
    FINAL_METHOD_TYPE,
    FINAL_TYPE,
    FLOAT_EXACT_TYPE,
    FUNCTION_TYPE,
    FinalClass,
    Function,
    GenericClass,
    GenericTypesDict,
    GenericVisitor,
    INLINE_TYPE,
    INT16_TYPE,
    INT32_TYPE,
    INT64_TYPE,
    INT8_TYPE,
    INT_EXACT_TYPE,
    IsInstanceEffect,
    IsInstanceFunction,
    IsSubclassFunction,
    LIST_EXACT_TYPE,
    LIST_TYPE,
    LenFunction,
    ModuleTable,
    NAMED_TUPLE_TYPE,
    NONE_TYPE,
    NO_EFFECT,
    NarrowingEffect,
    NumClass,
    OBJECT,
    OBJECT_TYPE,
    OPTIONAL_TYPE,
    PROTOCOL_TYPE,
    ResolvedTypeRef,
    RevealTypeFunction,
    SET_EXACT_TYPE,
    SLICE_TYPE,
    SPAM_OBJ,
    STATIC_METHOD_TYPE,
    STR_EXACT_TYPE,
    Slot,
    SortedFunction,
    StaticMethod,
    StrictBuiltins,
    TType,
    TUPLE_EXACT_TYPE,
    TUPLE_TYPE,
    TYPE_TYPE,
    TypeDescr,
    TypeName,
    UINT16_TYPE,
    UINT32_TYPE,
    UINT64_TYPE,
    UINT8_TYPE,
    UNION_TYPE,
    UnboxFunction,
    UnionInstance,
    VECTOR_TYPE,
    Value,
    XX_GENERIC_TYPE,
    _TMP_VAR_PREFIX,
    parse_typed_signature,
)


try:
    import xxclassloader  # pyre-ignore[21]: unknown module
    from xxclassloader import spamobj
except ImportError:
    spamobj = None


def exec_static(
    source: str,
    locals: Dict[str, object],
    globals: Dict[str, object],
    modname: str = "<module>",
) -> None:
    code = compile(
        source, "<module>", "exec", compiler=StaticCodeGenerator, modname=modname
    )
    exec(code, locals, globals)  # noqa: P204


def reflect_builtin_function(obj: BuiltinFunctionType) -> BuiltinFunction:
    sig = getattr(obj, "__typed_signature__", None)
    if sig is not None:
        signature, return_type = parse_typed_signature(sig)
        method = BuiltinFunction(
            obj.__name__,
            obj.__module__,
            signature,
            ResolvedTypeRef(return_type),
        )
    else:
        method = BuiltinFunction(obj.__name__, obj.__module__)
    return method



class SymbolTable:
    def __init__(self, error_sink: Optional[ErrorSink] = None) -> None:
        self.modules: Dict[str, ModuleTable] = {}
        builtins_children = {
            "object": OBJECT_TYPE,
            "type": TYPE_TYPE,
            "None": NONE_TYPE.instance,
            "int": INT_EXACT_TYPE,
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

        builtins_children["<builtins>"] = strict_builtins
        builtins_children["<fixed-modules>"] = StrictBuiltins(
            {"typing": StrictBuiltins(typing_children)}
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
        self.statics = self.modules["__static__"] = ModuleTable(
            "__static__",
            "<__static__>",
            self,
            {
                "Array": ARRAY_EXACT_TYPE,
                "CheckedDict": CHECKED_DICT_EXACT_TYPE,
                "allow_weakrefs": ALLOW_WEAKREFS_TYPE,
                "box": BoxFunction(FUNCTION_TYPE),
                "cast": CastFunction(FUNCTION_TYPE),
                "clen": LenFunction(FUNCTION_TYPE, boxed=False),
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
                "nonchecked_dicts": BOOL_TYPE.instance,
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
                    posix_clock_gettime_ns
                ),
                "rand": reflect_builtin_function(rand),
            },
        )

        if SPAM_OBJ is not None:
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

    def add_module(self, name: str, filename: str, tree: AST) -> None:
        decl_visit = DeclarationVisitor(name, filename, self)
        decl_visit.visit(tree)
        decl_visit.finish_bind()

    def bind(self, name: str, filename: str, tree: AST, optimize: int = 0) -> None:
        self._bind(name, filename, tree, optimize)

    def _bind(
        self, name: str, filename: str, tree: AST, optimize: int = 0
    ) -> Tuple[AST, SymbolVisitor]:
        if name not in self.modules:
            self.add_module(name, filename, tree)

        tree = AstOptimizer(optimize=optimize > 0).visit(tree)

        # Analyze variable scopes
        s = SymbolVisitor()
        s.visit(tree)

        # Analyze the types of objects within local scopes
        type_binder = TypeBinder(s, filename, self, name, optimize)
        type_binder.visit(tree)
        return tree, s

    def compile(
        self, name: str, filename: str, tree: AST, optimize: int = 0
    ) -> CodeType:
        tree, s = self._bind(name, filename, tree, optimize)
        if self.error_sink.has_errors:
            raise self.error_sink.errors[0]

        # Compile the code w/ the static compiler
        graph = StaticCodeGenerator.flow_graph(
            name, filename, s.scopes[tree], peephole_enabled=True
        )
        graph.setFlag(StaticCodeGenerator.consts.CO_STATICALLY_COMPILED)

        code_gen = StaticCodeGenerator(
            None, tree, s, graph, self, name, flags=0, optimization_lvl=optimize
        )
        code_gen.visit(tree)

        return code_gen.getCode()

    def import_module(self, name: str) -> None:
        pass


class LocalsBranch:
    """Handles branching and merging local variable types"""

    def __init__(self, scope: BindingScope) -> None:
        self.scope = scope
        self.entry_locals: Dict[str, Value] = dict(scope.local_types)

    def copy(self) -> Dict[str, Value]:
        """Make a copy of the current local state"""
        return dict(self.scope.local_types)

    def restore(self, state: Optional[Dict[str, Value]] = None) -> None:
        """Restore the locals to the state when we entered"""
        self.scope.local_types.clear()
        self.scope.local_types.update(state or self.entry_locals)

    def merge(self, entry_locals: Optional[Dict[str, Value]] = None) -> None:
        """Merge the entry locals, or a specific copy, into the current locals"""
        # TODO: What about del's?
        if entry_locals is None:
            entry_locals = self.entry_locals
        local_types = self.scope.local_types
        for key, value in entry_locals.items():
            if key in local_types:
                if value != local_types[key]:
                    local_types[key] = self._join(value, local_types[key])
                continue

    def _join(self, *types: Value) -> Value:
        if len(types) == 1:
            return types[0]

        return UNION_TYPE.make_generic_type(
            tuple(t.inexact().klass for t in types), self.scope.generic_types
        ).instance


class TypeDeclaration:
    def __init__(self, typ: Value, is_final: bool = False) -> None:
        self.type = typ
        self.is_final = is_final


class BindingScope:
    def __init__(self, node: AST, generic_types: GenericTypesDict) -> None:
        self.node = node
        self.local_types: Dict[str, Value] = {}
        self.decl_types: Dict[str, TypeDeclaration] = {}
        self.generic_types = generic_types

    def branch(self) -> LocalsBranch:
        return LocalsBranch(self)

    def declare(
        self, name: str, typ: Value, is_final: bool = False, is_inferred: bool = False
    ) -> TypeDeclaration:
        # For an unannotated assignment (is_inferred=True), we declare dynamic
        # type; this disallows later re-declaration, but allows any type to be
        # assigned later, so `x = None; if flag: x = "foo"` works.
        decl = TypeDeclaration(DYNAMIC if is_inferred else typ, is_final)
        self.decl_types[name] = decl
        self.local_types[name] = typ
        return decl


class ModuleBindingScope(BindingScope):
    def __init__(
        self, node: ast.Module, module: ModuleTable, generic_types: GenericTypesDict
    ) -> None:
        super().__init__(node, generic_types)
        self.module = module

    def declare(
        self, name: str, typ: Value, is_final: bool = False, is_inferred: bool = False
    ) -> TypeDeclaration:
        # at module scope we will go ahead and set a declared type even without
        # an annotation, but we don't want to infer the exact type; should be
        # able to reassign to a subtype
        if is_inferred:
            typ = typ.inexact()
            is_inferred = False
        self.module.children[name] = typ
        return super().declare(name, typ, is_final=is_final, is_inferred=is_inferred)


class TerminalKind(IntEnum):
    NonTerminal = 0
    BreakOrContinue = 1
    Return = 2


class TypeBinder(GenericVisitor):
    """Walks an AST and produces an optionally strongly typed AST, reporting errors when
    operations are occuring that are not sound.  Strong types are based upon places where
    annotations occur which opt-in the strong typing"""

    def __init__(
        self,
        symbols: SymbolVisitor,
        filename: str,
        symtable: SymbolTable,
        module_name: str,
        optimize: int = 0,
    ) -> None:
        super().__init__(module_name, filename, symtable)
        self.symbols = symbols
        self.scopes: List[BindingScope] = []
        self.symtable = symtable
        self.cur_mod: ModuleTable = symtable[module_name]
        self.optimize = optimize
        self.terminals: Dict[AST, TerminalKind] = {}
        self.inline_depth = 0
        self.inline_calls = 0

    @property
    def local_types(self) -> Dict[str, Value]:
        return self.binding_scope.local_types

    @property
    def decl_types(self) -> Dict[str, TypeDeclaration]:
        return self.binding_scope.decl_types

    @property
    def binding_scope(self) -> BindingScope:
        return self.scopes[-1]

    @property
    def scope(self) -> AST:
        return self.binding_scope.node

    def maybe_set_local_type(self, name: str, local_type: Value) -> Value:
        decl = self.get_target_decl(name)
        assert decl is not None
        decl_type = decl.type
        if local_type is DYNAMIC or not decl_type.klass.can_be_narrowed:
            local_type = decl_type
        self.local_types[name] = local_type
        return local_type

    def maybe_get_current_class(self) -> Optional[Class]:
        current: ModuleTable | Class = self.cur_mod
        result = None
        for scope in self.scopes:
            node = scope.node
            if isinstance(node, ClassDef):
                result = current.resolve_name(node.name)
                if not isinstance(result, Class):
                    return None
                current = result
        return result

    def visit(
        self, node: Union[AST, Sequence[AST]], *args: object
    ) -> Optional[NarrowingEffect]:
        """This override is only here to give Pyre the return type information."""
        ret = super().visit(node, *args)
        if ret is not None:
            assert isinstance(ret, NarrowingEffect)
            return ret
        return None

    def get_final_literal(self, node: AST) -> Optional[ast.Constant]:
        return self.cur_mod.get_final_literal(node, self.symbols.scopes[self.scope])

    def declare_local(
        self,
        target: ast.Name,
        typ: Value,
        is_final: bool = False,
        is_inferred: bool = False,
    ) -> None:
        if target.id in self.decl_types:
            self.syntax_error(f"Cannot redefine local variable {target.id}", target)
        if isinstance(typ, CInstance):
            self.check_primitive_scope(target)
        self.binding_scope.declare(
            target.id, typ, is_final=is_final, is_inferred=is_inferred
        )

    def check_static_import_flags(self, node: Module) -> None:
        saw_doc_str = False
        for stmt in node.body:
            if isinstance(stmt, ast.Expr):
                val = stmt.value
                if isinstance(val, ast.Constant) and isinstance(val.value, str):
                    if saw_doc_str:
                        break
                    saw_doc_str = True
                else:
                    break
            elif isinstance(stmt, ast.Import):
                continue
            elif isinstance(stmt, ast.ImportFrom):
                if stmt.module == "__static__.compiler_flags":
                    for name in stmt.names:
                        if name.name == "nonchecked_dicts":
                            self.cur_mod.nonchecked_dicts = True
                        elif name.name == "noframe":
                            self.cur_mod.noframe = True

    def visitModule(self, node: Module) -> None:
        self.scopes.append(
            ModuleBindingScope(
                node, self.cur_mod, generic_types=self.symtable.generic_types
            )
        )

        self.check_static_import_flags(node)

        for stmt in node.body:
            self.visit(stmt)

        self.scopes.pop()

    def set_param(self, arg: ast.arg, arg_type: Class, scope: BindingScope) -> None:
        scope.declare(arg.arg, arg_type.instance)
        self.set_type(arg, arg_type.instance)

    def _visitParameters(self, args: ast.arguments, scope: BindingScope) -> None:
        if args.defaults:
            for default in args.defaults:
                self.visit(default)

        if args.kw_defaults:
            for default in args.kw_defaults:
                if default is not None:
                    self.visit(default)

        default_index = len(args.defaults or []) - (
            len(args.posonlyargs) + len(args.args)
        )
        for arg in args.posonlyargs:
            ann = arg.annotation
            if ann:
                self.visitExpectedType(
                    ann, DYNAMIC, "argument annotation cannot be a primitive"
                )
                arg_type = self.cur_mod.resolve_annotation(ann) or DYNAMIC_TYPE
            elif arg.arg in scope.decl_types:
                # Already handled self
                default_index += 1
                continue
            else:
                arg_type = DYNAMIC_TYPE
            if default_index >= 0:
                self.check_can_assign_from(
                    arg_type,
                    self.get_type(args.defaults[default_index]).klass,
                    args.defaults[default_index],
                )
            default_index += 1
            self.set_param(arg, arg_type, scope)

        for arg in args.args:
            ann = arg.annotation
            if ann:
                self.visitExpectedType(
                    ann, DYNAMIC, "argument annotation cannot be a primitive"
                )
                arg_type = self.cur_mod.resolve_annotation(ann) or DYNAMIC_TYPE
            elif arg.arg in scope.decl_types:
                # Already handled self
                default_index += 1
                continue
            else:
                arg_type = DYNAMIC_TYPE

            if default_index >= 0:
                self.check_can_assign_from(
                    arg_type,
                    self.get_type(args.defaults[default_index]).klass,
                    args.defaults[default_index],
                )
            default_index += 1
            self.set_param(arg, arg_type, scope)

        vararg = args.vararg
        if vararg:
            ann = vararg.annotation
            if ann:
                self.visitExpectedType(
                    ann, DYNAMIC, "argument annotation cannot be a primitive"
                )

            self.set_param(vararg, TUPLE_EXACT_TYPE, scope)

        default_index = len(args.kw_defaults or []) - len(args.kwonlyargs)
        for arg in args.kwonlyargs:
            ann = arg.annotation
            if ann:
                self.visitExpectedType(
                    ann, DYNAMIC, "argument annotation cannot be a primitive"
                )
                arg_type = self.cur_mod.resolve_annotation(ann) or DYNAMIC_TYPE
            else:
                arg_type = DYNAMIC_TYPE

            if default_index >= 0:
                default = args.kw_defaults[default_index]
                if default is not None:
                    self.check_can_assign_from(
                        arg_type,
                        self.get_type(default).klass,
                        default,
                    )
            default_index += 1
            self.set_param(arg, arg_type, scope)

        kwarg = args.kwarg
        if kwarg:
            ann = kwarg.annotation
            if ann:
                self.visitExpectedType(
                    ann, DYNAMIC, "argument annotation cannot be a primitive"
                )
            self.set_param(kwarg, DICT_EXACT_TYPE, scope)

    def _visitFunc(self, node: Union[FunctionDef, AsyncFunctionDef]) -> None:
        scope = BindingScope(node, generic_types=self.symtable.generic_types)
        for decorator in node.decorator_list:
            self.visitExpectedType(
                decorator, DYNAMIC, "decorator cannot be a primitive"
            )
        cur_scope = self.scope

        if (
            not node.decorator_list
            and isinstance(cur_scope, ClassDef)
            and node.args.args
        ):
            # Handle type of "self"
            klass = self.cur_mod.resolve_name(cur_scope.name)
            if isinstance(klass, Class):
                self.set_param(node.args.args[0], klass, scope)
            else:
                self.set_param(node.args.args[0], DYNAMIC_TYPE, scope)

        self._visitParameters(node.args, scope)

        returns = None if node.args in self.cur_mod.dynamic_returns else node.returns
        if returns:
            # We store the return type on the node for the function as we otherwise
            # don't need to store type information for it
            expected = self.cur_mod.resolve_annotation(returns) or DYNAMIC_TYPE
            self.set_type(node, expected.instance)
            self.visitExpectedType(
                returns, DYNAMIC, "return annotation cannot be a primitive"
            )
        else:
            self.set_type(node, DYNAMIC)

        self.scopes.append(scope)

        for stmt in node.body:
            self.visit(stmt)

        self.scopes.pop()

    def visitFunctionDef(self, node: FunctionDef) -> None:
        self._visitFunc(node)

    def visitAsyncFunctionDef(self, node: AsyncFunctionDef) -> None:
        self._visitFunc(node)

    def visitClassDef(self, node: ClassDef) -> None:
        parent_scope = self.scope
        if isinstance(parent_scope, (FunctionDef, AsyncFunctionDef)):
            self.syntax_error(
                f"Cannot declare class `{node.name}` inside a function, `{parent_scope.name}`",
                node,
            )

        for decorator in node.decorator_list:
            self.visitExpectedType(
                decorator, DYNAMIC, "decorator cannot be a primitive"
            )

        for kwarg in node.keywords:
            self.visitExpectedType(
                kwarg.value, DYNAMIC, "class kwarg cannot be a primitive"
            )

        for base in node.bases:
            self.visitExpectedType(base, DYNAMIC, "class base cannot be a primitive")

        self.scopes.append(
            BindingScope(node, generic_types=self.symtable.generic_types)
        )

        for stmt in node.body:
            self.visit(stmt)

        self.scopes.pop()

    def set_type(
        self,
        node: AST,
        type: Value,
    ) -> None:
        self.cur_mod.types[node] = type

    def get_type(self, node: AST) -> Value:
        assert node in self.cur_mod.types, f"node not found: {node}, {node.lineno}"
        return self.cur_mod.types[node]

    def get_node_data(
        self, key: Union[AST, Delegator], data_type: Type[TType]
    ) -> TType:
        return cast(TType, self.cur_mod.node_data[key, data_type])

    def set_node_data(
        self, key: Union[AST, Delegator], data_type: Type[TType], value: TType
    ) -> None:
        self.cur_mod.node_data[key, data_type] = value

    def check_primitive_scope(self, node: Name) -> None:
        cur_scope = self.symbols.scopes[self.scope]
        var_scope = cur_scope.check_name(node.id)
        if var_scope != SC_LOCAL or isinstance(self.scope, Module):
            self.syntax_error("cannot use primitives in global or closure scope", node)

    def get_var_scope(self, var_id: str) -> Optional[int]:
        cur_scope = self.symbols.scopes[self.scope]
        var_scope = cur_scope.check_name(var_id)
        return var_scope

    def _check_final_attribute_reassigned(
        self,
        target: AST,
        assignment: Optional[AST],
    ) -> None:
        member = None
        klass = None
        member_name = None

        # Try to look up the Class and associated Slot
        scope = self.scope
        if isinstance(target, ast.Name) and isinstance(scope, ast.ClassDef):
            klass = self.maybe_get_current_class()
            assert isinstance(klass, Class)
            member_name = target.id
            member = klass.get_member(member_name)
        elif isinstance(target, ast.Attribute):
            klass = self.get_type(target.value).klass
            member_name = target.attr
            member = klass.get_member(member_name)

        # Ensure we don't reassign to Finals
        if (
            klass is not None
            and member is not None
            and (
                (
                    isinstance(member, Slot)
                    and member.is_final
                    and member.assignment != assignment
                )
                or (isinstance(member, Function) and member.is_final)
            )
        ):
            self.syntax_error(
                f"Cannot assign to a Final attribute of {klass.instance.name}:{member_name}",
                target,
            )

    def visitAnnAssign(self, node: AnnAssign) -> None:
        self.visitExpectedType(
            node.annotation, DYNAMIC, "annotation can not be a primitive value"
        )

        target = node.target
        comp_type = (
            self.cur_mod.resolve_annotation(node.annotation, is_declaration=True)
            or DYNAMIC_TYPE
        )
        is_final = False
        if isinstance(comp_type, ClassVar):
            if not isinstance(self.scope, ClassDef):
                self.syntax_error(
                    "ClassVar is allowed only in class attribute annotations.", node
                )
            comp_type = comp_type.inner_type()
        if isinstance(comp_type, FinalClass):
            is_final = True
            comp_type = comp_type.inner_type()

        if isinstance(target, Name):
            self.declare_local(target, comp_type.instance, is_final)
            self.set_type(target, comp_type.instance)

        self.visit(target)
        value = node.value
        if value:
            self.visitExpectedType(value, comp_type.instance)
            if isinstance(target, Name):
                # We could be narrowing the type after the assignment, so we update it here
                # even though we assigned it above (but we never narrow primtives)
                new_type = self.get_type(value)
                local_type = self.maybe_set_local_type(target.id, new_type)
                self.set_type(target, local_type)

            self._check_final_attribute_reassigned(target, node)

    def visitAugAssign(self, node: AugAssign) -> None:
        self.visit(node.target)
        target_type = self.get_type(node.target).inexact()
        self.visit(node.value, target_type)
        self.set_type(node, target_type)

    def visitAssign(self, node: Assign) -> None:
        # Sometimes, we need to propagate types from the target to the value to allow primitives to be handled
        # correctly.  So we compute the narrowest target type. (Other checks do happen later).
        # e.g: `x: int8 = 1` means we need `1` to be of type `int8`
        narrowest_target_type = None
        for target in reversed(node.targets):
            cur_type = None
            if isinstance(target, ast.Name):
                # This is a name, it could be unassigned still
                decl_type = self.get_target_decl(target.id)
                if decl_type is not None:
                    cur_type = decl_type.type
            elif isinstance(target, (ast.Tuple, ast.List)):
                # TODO: We should walk into the tuple/list and use it to infer
                # types down on the RHS if we can
                self.visit(target)
            else:
                # This is an attribute or subscript, the assignment can't change the type
                self.visit(target)
                cur_type = self.get_type(target)

            if cur_type is not None and (
                narrowest_target_type is None
                or narrowest_target_type.klass.can_assign_from(cur_type.klass)
            ):
                narrowest_target_type = cur_type

        self.visit(node.value, narrowest_target_type)
        value_type = self.get_type(node.value)
        for target in reversed(node.targets):
            self.assign_value(target, value_type, src=node.value, assignment=node)

        self.set_type(node, value_type)

    def check_can_assign_from(
        self,
        dest: Class,
        src: Class,
        node: AST,
        reason: str = "type mismatch: {} cannot be assigned to {}",
    ) -> None:
        if not dest.can_assign_from(src) and (
            src is not DYNAMIC_TYPE or isinstance(dest, CType)
        ):
            self.syntax_error(
                reason.format(src.instance.name, dest.instance.name),
                node,
            )

    def visitAssert(self, node: ast.Assert) -> None:
        effect = self.visit(node.test) or NO_EFFECT
        effect.apply(self.local_types)
        self.set_node_data(node, NarrowingEffect, effect)
        message = node.msg
        if message:
            self.visitExpectedType(
                message, DYNAMIC, "assert message cannot be a primitive"
            )

    def visitBoolOp(
        self, node: BoolOp, type_ctx: Optional[Class] = None
    ) -> NarrowingEffect:
        effect = NO_EFFECT
        final_type = None
        if isinstance(node.op, And):
            for value in node.values:
                new_effect = self.visit(value) or NO_EFFECT
                effect = effect.and_(new_effect)
                final_type = self.widen(final_type, self.get_type(value))

                # apply the new effect as short circuiting would
                # eliminate it.
                new_effect.apply(self.local_types)

            # we undo the effect as we have no clue what context we're in
            # but then we return the combined effect in case we're being used
            # in a conditional context
            effect.undo(self.local_types)
        elif isinstance(node.op, ast.Or):
            for value in node.values[:-1]:
                new_effect = self.visit(value) or NO_EFFECT
                effect = effect.or_(new_effect)

                old_type = self.get_type(value)
                # The or expression will only return the `value` we're visiting if it's
                # effect holds, so we visit it assuming that the narrowing effects apply.
                new_effect.apply(self.local_types)
                self.visit(value)
                new_effect.undo(self.local_types)

                final_type = self.widen(final_type, self.get_type(value))
                self.set_type(value, old_type)

                new_effect.reverse(self.local_types)
            # We know nothing about the last node of an or, so we simply widen with its type.
            new_effect = self.visit(node.values[-1]) or NO_EFFECT
            final_type = self.widen(final_type, self.get_type(node.values[-1]))

            effect.undo(self.local_types)
            effect = effect.or_(new_effect)
        else:
            for value in node.values:
                self.visit(value)
                final_type = self.widen(final_type, self.get_type(value))

        self.set_type(node, final_type or DYNAMIC)
        return effect

    def visitBinOp(
        self, node: BinOp, type_ctx: Optional[Class] = None
    ) -> NarrowingEffect:
        self.visit(node.left, type_ctx)
        self.visit(node.right, type_ctx)

        ltype = self.get_type(node.left)
        rtype = self.get_type(node.right)

        tried_right = False
        if ltype.klass in rtype.klass.mro[1:]:
            if rtype.bind_reverse_binop(node, self, type_ctx):
                return NO_EFFECT
            tried_right = True

        if ltype.bind_binop(node, self, type_ctx):
            return NO_EFFECT

        if not tried_right:
            rtype.bind_reverse_binop(node, self, type_ctx)

        return NO_EFFECT

    def visitUnaryOp(
        self, node: UnaryOp, type_ctx: Optional[Class] = None
    ) -> NarrowingEffect:
        effect = self.visit(node.operand, type_ctx)
        self.get_type(node.operand).bind_unaryop(node, self, type_ctx)
        if (
            effect is not None
            and effect is not NO_EFFECT
            and isinstance(node.op, ast.Not)
        ):
            return effect.not_()
        return NO_EFFECT

    def visitLambda(
        self, node: Lambda, type_ctx: Optional[Class] = None
    ) -> NarrowingEffect:
        scope = BindingScope(node, generic_types=self.symtable.generic_types)
        self._visitParameters(node.args, scope)

        self.scopes.append(scope)
        self.visitExpectedType(
            node.body, DYNAMIC, "lambda cannot return primitive value"
        )
        self.scopes.pop()

        self.set_type(node, DYNAMIC)
        return NO_EFFECT

    def visitIfExp(
        self, node: IfExp, type_ctx: Optional[Class] = None
    ) -> NarrowingEffect:
        effect = self.visit(node.test) or NO_EFFECT
        effect.apply(self.local_types)
        self.visit(node.body)
        effect.reverse(self.local_types)
        self.visit(node.orelse)
        effect.undo(self.local_types)

        # Select the most compatible types that we can, or fallback to
        # dynamic if we can coerce to dynamic, otherwise report an error.
        body_t = self.get_type(node.body)
        else_t = self.get_type(node.orelse)
        if body_t.klass.can_assign_from(else_t.klass):
            self.set_type(node, body_t)
        elif else_t.klass.can_assign_from(body_t.klass):
            self.set_type(node, else_t)
        elif DYNAMIC_TYPE.can_assign_from(
            body_t.klass
        ) and DYNAMIC_TYPE.can_assign_from(else_t.klass):
            self.set_type(node, DYNAMIC)
        else:
            self.syntax_error(
                f"if expression has incompatible types: {body_t.name} and {else_t.name}",
                node,
            )
        return NO_EFFECT

    def visitSlice(
        self, node: Slice, type_ctx: Optional[Class] = None
    ) -> NarrowingEffect:
        lower = node.lower
        if lower:
            self.visitExpectedType(lower, DYNAMIC, "slice indices cannot be primitives")
        upper = node.upper
        if upper:
            self.visitExpectedType(upper, DYNAMIC, "slice indices cannot be primitives")
        step = node.step
        if step:
            self.visitExpectedType(step, DYNAMIC, "slice indices cannot be primitives")
        self.set_type(node, SLICE_TYPE.instance)
        return NO_EFFECT

    def widen(self, existing: Optional[Value], new: Value) -> Value:
        if existing is None or new.klass.can_assign_from(existing.klass):
            return new
        elif existing.klass.can_assign_from(new.klass):
            return existing

        res = UNION_TYPE.make_generic_type(
            (existing.klass, new.klass), self.symtable.generic_types
        ).instance
        return res

    def visitDict(
        self, node: ast.Dict, type_ctx: Optional[Class] = None
    ) -> NarrowingEffect:
        key_type: Optional[Value] = None
        value_type: Optional[Value] = None
        for k, v in zip(node.keys, node.values):
            if k:
                self.visitExpectedType(k, DYNAMIC, "dict keys cannot be primitives")
                key_type = self.widen(key_type, self.get_type(k))
                self.visitExpectedType(v, DYNAMIC, "dict keys cannot be primitives")
                value_type = self.widen(value_type, self.get_type(v))
            else:
                self.visitExpectedType(v, DYNAMIC, "dict splat cannot be a primitive")
                d_type = self.get_type(v).klass
                if (
                    d_type.generic_type_def is CHECKED_DICT_TYPE
                    or d_type.generic_type_def is CHECKED_DICT_EXACT_TYPE
                ):
                    assert isinstance(d_type, GenericClass)
                    key_type = self.widen(key_type, d_type.type_args[0].instance)
                    value_type = self.widen(value_type, d_type.type_args[1].instance)
                elif d_type in (DICT_TYPE, DICT_EXACT_TYPE, DYNAMIC_TYPE):
                    key_type = DYNAMIC
                    value_type = DYNAMIC

        self.set_dict_type(node, key_type, value_type, type_ctx, is_exact=True)
        return NO_EFFECT

    def set_dict_type(
        self,
        node: ast.expr,
        key_type: Optional[Value],
        value_type: Optional[Value],
        type_ctx: Optional[Class],
        is_exact: bool = False,
    ) -> Value:
        if self.cur_mod.nonchecked_dicts or not isinstance(
            type_ctx, CheckedDictInstance
        ):
            # This is not a checked dict, or the user opted out of checked dicts
            if type_ctx in (DICT_TYPE.instance, DICT_EXACT_TYPE.instance):
                typ = type_ctx
            elif is_exact:
                typ = DICT_EXACT_TYPE.instance
            else:
                typ = DICT_TYPE.instance
            assert typ is not None
            self.set_type(node, typ)
            return typ

        # Calculate the type that is inferred by the keys and values
        assert type_ctx is not None
        type_class = type_ctx.klass
        assert type_class.generic_type_def in (
            CHECKED_DICT_EXACT_TYPE,
            CHECKED_DICT_TYPE,
        )
        assert isinstance(type_class, GenericClass)
        if key_type is None:
            key_type = type_class.type_args[0].instance

        if value_type is None:
            value_type = type_class.type_args[1].instance

        checked_dict_typ = CHECKED_DICT_EXACT_TYPE if is_exact else CHECKED_DICT_TYPE

        gen_type = checked_dict_typ.make_generic_type(
            (key_type.klass, value_type.klass), self.symtable.generic_types
        )

        self.set_type(node, type_ctx)
        # We can use the type context to have a type which is wider than the
        # inferred types.  But we need to make sure that the keys/values are compatible
        # with the wider type, and if not, we'll report that the inferred type isn't
        # compatible.
        if not type_class.type_args[0].can_assign_from(
            key_type.klass
        ) or not type_class.type_args[1].can_assign_from(value_type.klass):
            self.check_can_assign_from(type_class, gen_type, node)
        return type_ctx

    def visitSet(
        self, node: ast.Set, type_ctx: Optional[Class] = None
    ) -> NarrowingEffect:
        for elt in node.elts:
            self.visitExpectedType(elt, DYNAMIC, "set members cannot be primitives")
        self.set_type(node, SET_EXACT_TYPE.instance)
        return NO_EFFECT

    def visitGeneratorExp(
        self, node: GeneratorExp, type_ctx: Optional[Class] = None
    ) -> NarrowingEffect:
        self.visit_comprehension(node, node.generators, node.elt)
        self.set_type(node, DYNAMIC)
        return NO_EFFECT

    def visitListComp(
        self, node: ListComp, type_ctx: Optional[Class] = None
    ) -> NarrowingEffect:
        self.visit_comprehension(node, node.generators, node.elt)
        self.set_type(node, LIST_EXACT_TYPE.instance)
        return NO_EFFECT

    def visitSetComp(
        self, node: SetComp, type_ctx: Optional[Class] = None
    ) -> NarrowingEffect:
        self.visit_comprehension(node, node.generators, node.elt)
        self.set_type(node, SET_EXACT_TYPE.instance)
        return NO_EFFECT

    def get_target_decl(self, name: str) -> Optional[TypeDeclaration]:
        decl_type = self.decl_types.get(name)
        if decl_type is None:
            scope_type = self.get_var_scope(name)
            if scope_type in (SC_GLOBAL_EXPLICIT, SC_GLOBAL_IMPLICIT):
                decl_type = self.scopes[0].decl_types.get(name)
        return decl_type

    def assign_value(
        self,
        target: expr,
        value: Value,
        src: Optional[expr] = None,
        assignment: Optional[AST] = None,
    ) -> None:
        if isinstance(target, Name):
            decl_type = self.get_target_decl(target.id)
            if decl_type is None:
                self.declare_local(target, value, is_inferred=True)
            else:
                if decl_type.is_final:
                    self.syntax_error("Cannot assign to a Final variable", target)
                self.check_can_assign_from(decl_type.type.klass, value.klass, target)

            local_type = self.maybe_set_local_type(target.id, value)
            self.set_type(target, local_type)
        elif isinstance(target, (ast.Tuple, ast.List)):
            if isinstance(src, (ast.Tuple, ast.List)) and len(target.elts) == len(
                src.elts
            ):
                for target, inner_value in zip(target.elts, src.elts):
                    self.assign_value(
                        target, self.get_type(inner_value), src=inner_value
                    )
            elif isinstance(src, ast.Constant):
                t = src.value
                if isinstance(t, tuple) and len(t) == len(target.elts):
                    for target, inner_value in zip(target.elts, t):
                        self.assign_value(target, CONSTANT_TYPES[type(inner_value)])
                else:
                    for val in target.elts:
                        self.assign_value(val, DYNAMIC)
            else:
                for val in target.elts:
                    self.assign_value(val, DYNAMIC)
        else:
            self.check_can_assign_from(self.get_type(target).klass, value.klass, target)
        self._check_final_attribute_reassigned(target, assignment)

    def visitDictComp(
        self, node: DictComp, type_ctx: Optional[Class] = None
    ) -> NarrowingEffect:
        self.visit(node.generators[0].iter)

        scope = BindingScope(node, generic_types=self.symtable.generic_types)
        self.scopes.append(scope)

        iter_type = self.get_type(node.generators[0].iter).get_iter_type(
            node.generators[0].iter, self
        )

        self.assign_value(node.generators[0].target, iter_type)
        for if_ in node.generators[0].ifs:
            self.visit(if_)

        for gen in node.generators[1:]:
            self.visit(gen.iter)
            iter_type = self.get_type(gen.iter).get_iter_type(gen.iter, self)
            self.assign_value(gen.target, iter_type)

        self.visitExpectedType(
            node.key, DYNAMIC, "dictionary comprehension key cannot be a primitive"
        )
        self.visitExpectedType(
            node.value, DYNAMIC, "dictionary comprehension value cannot be a primitive"
        )

        self.scopes.pop()

        key_type = self.get_type(node.key)
        value_type = self.get_type(node.value)
        self.set_dict_type(node, key_type, value_type, type_ctx, is_exact=True)

        return NO_EFFECT

    def visit_comprehension(
        self, node: ast.expr, generators: List[ast.comprehension], *elts: ast.expr
    ) -> None:
        self.visit(generators[0].iter)

        scope = BindingScope(node, generic_types=self.symtable.generic_types)
        self.scopes.append(scope)

        iter_type = self.get_type(generators[0].iter).get_iter_type(
            generators[0].iter, self
        )

        self.assign_value(generators[0].target, iter_type)
        for if_ in generators[0].ifs:
            self.visit(if_)

        for gen in generators[1:]:
            self.visit(gen.iter)
            iter_type = self.get_type(gen.iter).get_iter_type(gen.iter, self)
            self.assign_value(gen.target, iter_type)

        for elt in elts:
            self.visitExpectedType(
                elt, DYNAMIC, "generator element cannot be a primitive"
            )

        self.scopes.pop()

    def visitAwait(
        self, node: Await, type_ctx: Optional[Class] = None
    ) -> NarrowingEffect:
        self.visitExpectedType(node.value, DYNAMIC, "cannot await a primitive value")
        self.set_type(node, DYNAMIC)
        return NO_EFFECT

    def visitYield(
        self, node: Yield, type_ctx: Optional[Class] = None
    ) -> NarrowingEffect:
        value = node.value
        if value is not None:
            self.visitExpectedType(value, DYNAMIC, "cannot yield a primitive value")
        self.set_type(node, DYNAMIC)
        return NO_EFFECT

    def visitYieldFrom(
        self, node: YieldFrom, type_ctx: Optional[Class] = None
    ) -> NarrowingEffect:
        self.visitExpectedType(
            node.value, DYNAMIC, "cannot yield from a primitive value"
        )
        self.set_type(node, DYNAMIC)
        return NO_EFFECT

    def visitIndex(
        self, node: Index, type_ctx: Optional[Class] = None
    ) -> NarrowingEffect:
        self.visit(node.value, type_ctx)
        self.set_type(node, self.get_type(node.value))
        return NO_EFFECT

    def visitCompare(
        self, node: Compare, type_ctx: Optional[Class] = None
    ) -> NarrowingEffect:
        if len(node.ops) == 1 and isinstance(node.ops[0], (Is, IsNot)):
            left = node.left
            right = node.comparators[0]
            other = None

            self.set_type(node, BOOL_TYPE.instance)
            self.set_type(node.ops[0], BOOL_TYPE.instance)

            self.visit(left)
            self.visit(right)

            if isinstance(left, (Constant, NameConstant)) and left.value is None:
                other = right
            elif isinstance(right, (Constant, NameConstant)) and right.value is None:
                other = left

            if other is not None and isinstance(other, Name):
                var_type = self.get_type(other)

                if (
                    isinstance(var_type, UnionInstance)
                    and not var_type.klass.is_generic_type_definition
                ):
                    effect = IsInstanceEffect(other, var_type, NONE_TYPE.instance, self)
                    if isinstance(node.ops[0], IsNot):
                        effect = effect.not_()
                    return effect

        self.visit(node.left)
        left = node.left
        ltype = self.get_type(node.left)
        node.ops = [type(op)() for op in node.ops]
        for comparator, op in zip(node.comparators, node.ops):
            self.visit(comparator)
            rtype = self.get_type(comparator)

            tried_right = False
            if ltype.klass in rtype.klass.mro[1:]:
                if ltype.bind_reverse_compare(
                    node, left, op, comparator, self, type_ctx
                ):
                    continue
                tried_right = True

            if ltype.bind_compare(node, left, op, comparator, self, type_ctx):
                continue

            if not tried_right:
                rtype.bind_reverse_compare(node, left, op, comparator, self, type_ctx)

            ltype = rtype
            right = comparator
        return NO_EFFECT

    def visitCall(
        self, node: Call, type_ctx: Optional[Class] = None
    ) -> NarrowingEffect:
        self.visit(node.func)
        result = self.get_type(node.func).bind_call(node, self, type_ctx)
        return result

    def visitFormattedValue(
        self, node: FormattedValue, type_ctx: Optional[Class] = None
    ) -> NarrowingEffect:
        self.visitExpectedType(
            node.value, DYNAMIC, "cannot use primitive in formatted value"
        )
        self.set_type(node, DYNAMIC)
        return NO_EFFECT

    def visitJoinedStr(
        self, node: JoinedStr, type_ctx: Optional[Class] = None
    ) -> NarrowingEffect:
        for value in node.values:
            self.visit(value)

        self.set_type(node, STR_EXACT_TYPE.instance)
        return NO_EFFECT

    def visitConstant(
        self, node: Constant, type_ctx: Optional[Class] = None
    ) -> NarrowingEffect:
        if type_ctx is not None:
            type_ctx.bind_constant(node, self)
        else:
            DYNAMIC.bind_constant(node, self)
        return NO_EFFECT

    def visitAttribute(
        self, node: Attribute, type_ctx: Optional[Class] = None
    ) -> NarrowingEffect:
        self.visit(node.value)
        self.get_type(node.value).bind_attr(node, self, type_ctx)
        return NO_EFFECT

    def visitSubscript(
        self, node: Subscript, type_ctx: Optional[Class] = None
    ) -> NarrowingEffect:
        self.visit(node.value)
        self.visit(node.slice)
        val_type = self.get_type(node.value)
        val_type.bind_subscr(node, self.get_type(node.slice), self, type_ctx)
        return NO_EFFECT

    def visitStarred(
        self, node: Starred, type_ctx: Optional[Class] = None
    ) -> NarrowingEffect:
        self.visitExpectedType(
            node.value, DYNAMIC, "cannot use primitive in starred expression"
        )
        self.set_type(node, DYNAMIC)
        return NO_EFFECT

    def visitName(
        self, node: Name, type_ctx: Optional[Class] = None
    ) -> NarrowingEffect:
        cur_scope = self.symbols.scopes[self.scope]
        scope = cur_scope.check_name(node.id)
        if scope == SC_LOCAL and not isinstance(self.scope, Module):
            var_type = self.local_types.get(node.id, DYNAMIC)
            self.set_type(node, var_type)
        else:
            self.set_type(node, self.cur_mod.resolve_name(node.id) or DYNAMIC)

        type = self.get_type(node)
        if (
            isinstance(type, UnionInstance)
            and not type.klass.is_generic_type_definition
        ):
            effect = IsInstanceEffect(node, type, NONE_TYPE.instance, self)
            return effect.not_()

        return NO_EFFECT

    def visitExpectedType(
        self,
        node: AST,
        expected: Value,
        reason: str = "type mismatch: {} cannot be assigned to {}",
        blame: Optional[AST] = None,
    ) -> Optional[NarrowingEffect]:
        res = self.visit(node, expected)
        self.check_can_assign_from(
            expected.klass, self.get_type(node).klass, blame or node, reason
        )
        return res

    def visitList(
        self, node: ast.List, type_ctx: Optional[Class] = None
    ) -> NarrowingEffect:
        for elt in node.elts:
            self.visitExpectedType(elt, DYNAMIC)
        self.set_type(node, LIST_EXACT_TYPE.instance)
        return NO_EFFECT

    def visitTuple(
        self, node: ast.Tuple, type_ctx: Optional[Class] = None
    ) -> NarrowingEffect:
        for elt in node.elts:
            self.visitExpectedType(elt, DYNAMIC)
        self.set_type(node, TUPLE_EXACT_TYPE.instance)
        return NO_EFFECT

    def set_terminal_kind(self, node: AST, level: TerminalKind) -> None:
        current = self.terminals.get(node, TerminalKind.NonTerminal)
        if current < level:
            self.terminals[node] = level

    def visitContinue(self, node: ast.Continue) -> None:
        self.set_terminal_kind(node, TerminalKind.BreakOrContinue)

    def visitBreak(self, node: ast.Break) -> None:
        self.set_terminal_kind(node, TerminalKind.BreakOrContinue)

    def visitReturn(self, node: Return) -> None:
        self.set_terminal_kind(node, TerminalKind.Return)
        value = node.value
        if value is not None:
            cur_scope = self.binding_scope
            func = cur_scope.node
            expected = DYNAMIC
            if isinstance(func, (ast.FunctionDef, ast.AsyncFunctionDef)):
                func_returns = func.returns
                if func_returns:
                    expected = (
                        self.cur_mod.resolve_annotation(func_returns) or DYNAMIC_TYPE
                    ).instance

            self.visit(value, expected)
            returned = self.get_type(value).klass
            if returned is not DYNAMIC_TYPE and not expected.klass.can_assign_from(
                returned
            ):
                self.syntax_error(
                    f"return type must be {expected.name}, not "
                    + str(self.get_type(value).name),
                    node,
                )

    def visitImportFrom(self, node: ImportFrom) -> None:
        mod_name = node.module
        if node.level or not mod_name:
            raise NotImplementedError("relative imports aren't supported")

        if mod_name == "__static__":
            for alias in node.names:
                name = alias.name
                if name == "*":
                    self.syntax_error("from __static__ import * is disallowed", node)
                elif name not in self.symtable.statics.children:
                    self.syntax_error(f"unsupported static import {name}", node)

    def visit_until_terminates(self, nodes: List[ast.stmt]) -> TerminalKind:
        for stmt in nodes:
            self.visit(stmt)
            if stmt in self.terminals:
                return self.terminals[stmt]

        return TerminalKind.NonTerminal

    def visitIf(self, node: If) -> None:
        branch = self.binding_scope.branch()

        effect = self.visit(node.test) or NO_EFFECT
        effect.apply(self.local_types)

        terminates = self.visit_until_terminates(node.body)

        if node.orelse:
            if_end = branch.copy()
            branch.restore()

            effect.reverse(self.local_types)
            else_terminates = self.visit_until_terminates(node.orelse)
            if else_terminates:
                if terminates:
                    # We're the least severe terminal of our two children
                    self.terminals[node] = min(terminates, else_terminates)
                else:
                    branch.restore(if_end)
            elif not terminates:
                # Merge end of orelse with end of if
                branch.merge(if_end)
        elif terminates:
            effect.reverse(self.local_types)
        else:
            # Merge end of if w/ opening (with test effect reversed)
            branch.merge(effect.reverse(branch.entry_locals))

    def visitTry(self, node: Try) -> None:
        branch = self.binding_scope.branch()
        self.visit(node.body)

        branch.merge()
        post_try = branch.copy()
        merges = []

        if node.orelse:
            self.visit(node.orelse)
            merges.append(branch.copy())

        for handler in node.handlers:
            branch.restore(post_try)
            self.visit(handler)
            merges.append(branch.copy())

        branch.restore(post_try)
        for merge in merges:
            branch.merge(merge)

        if node.finalbody:
            self.visit(node.finalbody)

    def visitExceptHandler(self, node: ast.ExceptHandler) -> None:
        htype = node.type
        hname = None
        if htype:
            self.visit(htype)
            handler_type = self.get_type(htype)
            hname = node.name
            if hname:
                if handler_type is DYNAMIC or not isinstance(handler_type, Class):
                    handler_type = DYNAMIC_TYPE

                decl_type = self.decl_types.get(hname)
                if decl_type and decl_type.is_final:
                    self.syntax_error("Cannot assign to a Final variable", node)

                self.binding_scope.declare(hname, handler_type.instance)

        self.visit(node.body)
        if hname is not None:
            del self.decl_types[hname]
            del self.local_types[hname]

    def visitWhile(self, node: While) -> None:
        branch = self.scopes[-1].branch()

        effect = self.visit(node.test) or NO_EFFECT
        effect.apply(self.local_types)

        while_returns = self.visit_until_terminates(node.body) == TerminalKind.Return
        if while_returns:
            branch.restore()
            effect.reverse(self.local_types)
        else:
            branch.merge(effect.reverse(branch.entry_locals))

        if node.orelse:
            # The or-else can happen after the while body, or without executing
            # it, but it can only happen after the while condition evaluates to
            # False.
            effect.reverse(self.local_types)
            self.visit(node.orelse)

            branch.merge()

    def visitFor(self, node: For) -> None:
        self.visit(node.iter)
        target_type = self.get_type(node.iter).get_iter_type(node.iter, self)
        self.visit(node.target)
        self.assign_value(node.target, target_type)
        self.visit(node.body)
        self.visit(node.orelse)

    def visitwithitem(self, node: ast.withitem) -> None:
        self.visit(node.context_expr)
        optional_vars = node.optional_vars
        if optional_vars:
            self.visit(optional_vars)
            self.assign_value(optional_vars, DYNAMIC)


class PyFlowGraph38Static(PyFlowGraphCinder):
    opcode: Opcode = opcode38static.opcode


class Static38CodeGenerator(CinderCodeGenerator):
    flow_graph = PyFlowGraph38Static
    _default_cache: Dict[Type[ast.AST], typingCallable[[...], None]] = {}

    def __init__(
        self,
        parent: Optional[CodeGenerator],
        node: AST,
        symbols: SymbolVisitor,
        graph: PyFlowGraph,
        symtable: SymbolTable,
        modname: str,
        flags: int = 0,
        optimization_lvl: int = 0,
    ) -> None:
        super().__init__(parent, node, symbols, graph, flags, optimization_lvl)
        self.symtable = symtable
        self.modname = modname
        # Use this counter to allocate temporaries for loop indices
        self._tmpvar_loopidx_count = 0
        self.cur_mod: ModuleTable = self.symtable.modules[modname]

    def _is_static_compiler_disabled(self, node: AST) -> bool:
        if not isinstance(node, (AsyncFunctionDef, FunctionDef, ClassDef)):
            # Static compilation can only be disabled for functions and classes.
            return False
        scope = self.scope
        fn = None
        if isinstance(scope, ClassScope):
            klass = self.cur_mod.resolve_name(scope.name)
            if klass:
                assert isinstance(klass, Class)
                if klass.donotcompile:
                    # If static compilation is disabled on the entire class, it's skipped for all contained
                    # methods too.
                    return True
                fn = klass.get_own_member(node.name)

        if fn is None:
            # Wasn't a method, let's check if it's a module level function
            fn = self.cur_mod.resolve_name(node.name)

        if isinstance(fn, (Function, StaticMethod)):
            return (
                fn.donotcompile
                if isinstance(fn, Function)
                else fn.function.donotcompile
            )

        return False

    def make_child_codegen(
        self,
        tree: AST,
        graph: PyFlowGraph,
        codegen_type: Optional[Type[CinderCodeGenerator]] = None,
    ) -> CodeGenerator:
        if self._is_static_compiler_disabled(tree):
            return super().make_child_codegen(
                tree, graph, codegen_type=CinderCodeGenerator
            )
        graph.setFlag(self.consts.CO_STATICALLY_COMPILED)
        if self.cur_mod.noframe:
            graph.setFlag(self.consts.CO_NO_FRAME)
        gen = StaticCodeGenerator(
            self,
            tree,
            self.symbols,
            graph,
            symtable=self.symtable,
            modname=self.modname,
            optimization_lvl=self.optimization_lvl,
        )
        if not isinstance(tree, ast.ClassDef):
            self._processArgTypes(tree, gen)
        return gen

    def _processArgTypes(self, node: AST, gen: Static38CodeGenerator) -> None:
        arg_checks = []
        cellvars = gen.graph.cellvars
        # pyre-fixme[16]: When node is a comprehension (i.e., not a FunctionDef
        # or Lambda), our caller manually adds an args attribute.
        args: ast.arguments = node.args
        is_comprehension = not isinstance(
            node, (ast.AsyncFunctionDef, ast.FunctionDef, ast.Lambda)
        )

        for i, arg in enumerate(args.posonlyargs):
            t = self.get_type(arg)
            if t is not DYNAMIC and t is not OBJECT:
                arg_checks.append(self._calculate_idx(arg.arg, i, cellvars))
                arg_checks.append(t.klass.type_descr)

        for i, arg in enumerate(args.args):
            # Comprehension nodes don't have arguments when they're typed; make
            # up for that here.
            t = DYNAMIC if is_comprehension else self.get_type(arg)
            if t is not DYNAMIC and t is not OBJECT:
                arg_checks.append(
                    self._calculate_idx(arg.arg, i + len(args.posonlyargs), cellvars)
                )
                arg_checks.append(t.klass.type_descr)

        for i, arg in enumerate(args.kwonlyargs):
            t = self.get_type(arg)
            if t is not DYNAMIC and t is not OBJECT:
                arg_checks.append(
                    self._calculate_idx(
                        arg.arg,
                        i + len(args.posonlyargs) + len(args.args),
                        cellvars,
                    )
                )
                arg_checks.append(t.klass.type_descr)

        # we should never emit arg checks for object
        assert not any(td == ("builtins", "object") for td in arg_checks[1::2])

        gen.emit("CHECK_ARGS", tuple(arg_checks))

    def get_type(self, node: Union[AST, Delegator]) -> Value:
        return self.cur_mod.types[node]

    def get_node_data(
        self, key: Union[AST, Delegator], data_type: Type[TType]
    ) -> TType:
        return cast(TType, self.cur_mod.node_data[key, data_type])

    def set_node_data(
        self, key: Union[AST, Delegator], data_type: Type[TType], value: TType
    ) -> None:
        self.cur_mod.node_data[key, data_type] = value

    @classmethod
    # pyre-fixme[14]: `make_code_gen` overrides method defined in
    #  `Python37CodeGenerator` inconsistently.
    def make_code_gen(
        cls,
        module_name: str,
        tree: AST,
        filename: str,
        flags: int,
        optimize: int,
        peephole_enabled: bool = True,
        ast_optimizer_enabled: bool = True,
    ) -> Static38CodeGenerator:
        # TODO: Parsing here should really be that we run declaration visitor over all nodes,
        # and then perform post processing on the symbol table, and then proceed to analysis
        # and compilation
        symtable = SymbolTable()
        decl_visit = DeclarationVisitor(module_name, filename, symtable)
        decl_visit.visit(tree)

        for module in symtable.modules.values():
            module.finish_bind()

        if ast_optimizer_enabled:
            tree = AstOptimizer(optimize=optimize > 0).visit(tree)

        s = symbols.SymbolVisitor()
        s.visit(tree)

        graph = cls.flow_graph(
            module_name, filename, s.scopes[tree], peephole_enabled=peephole_enabled
        )
        graph.setFlag(cls.consts.CO_STATICALLY_COMPILED)

        type_binder = TypeBinder(s, filename, symtable, module_name, optimize)
        type_binder.visit(tree)

        code_gen = cls(None, tree, s, graph, symtable, module_name, flags, optimize)
        code_gen.visit(tree)
        return code_gen

    def make_function_graph(
        self,
        func: FunctionDef,
        filename: str,
        scopes: Dict[AST, Scope],
        class_name: str,
        name: str,
        first_lineno: int,
    ) -> PyFlowGraph:
        graph = super().make_function_graph(
            func, filename, scopes, class_name, name, first_lineno
        )

        # we tagged the graph as CO_STATICALLY_COMPILED, and the last co_const entry
        # will inform the runtime of the return type for the code object.
        ret_type = self.get_type(func)
        type_descr = ret_type.klass.type_descr
        graph.extra_consts.append(type_descr)
        return graph

    @contextmanager
    def new_loopidx(self) -> Generator[str, None, None]:
        self._tmpvar_loopidx_count += 1
        try:
            yield f"{_TMP_VAR_PREFIX}.{self._tmpvar_loopidx_count}"
        finally:
            self._tmpvar_loopidx_count -= 1

    def _resolve_class(self, node: ClassDef) -> Optional[Class]:
        cur_mod = self.symtable.modules[self.modname]
        klass = cur_mod.resolve_name(node.name)
        if not isinstance(klass, Class) or klass is DYNAMIC_TYPE:
            return
        return klass

    def store_type_name_and_flags(self, node: ClassDef) -> None:
        klass = self._resolve_class(node)
        if klass:
            method = "set_type_static_final" if klass.is_final else "set_type_static"
            self.emit("INVOKE_FUNCTION", (("_static", method), 1))
        self.storeName(node.name)

    def walkClassBody(self, node: ClassDef, gen: CodeGenerator) -> None:
        super().walkClassBody(node, gen)
        klass = self._resolve_class(node)
        if not klass:
            return

        class_mems = [
            name
            for name, value in klass.members.items()
            if isinstance(value, Slot) and not value.is_classvar
        ]
        if klass.allow_weakrefs:
            class_mems.append("__weakref__")

        # In the future we may want a compatibility mode where we add
        # __dict__ and __weakref__
        gen.emit("LOAD_CONST", tuple(class_mems))
        gen.emit("STORE_NAME", "__slots__")

        count = 0
        for name, value in klass.members.items():
            if not isinstance(value, Slot):
                continue

            if value.is_classvar:
                continue

            if value.decl_type is DYNAMIC_TYPE:
                continue

            gen.emit("LOAD_CONST", name)
            gen.emit("LOAD_CONST", value.type_descr)
            count += 1

        if count:
            gen.emit("BUILD_MAP", count)
            gen.emit("STORE_NAME", "__slot_types__")

    def visitModule(self, node: Module) -> None:
        if not self.cur_mod.nonchecked_dicts:
            self.emit("LOAD_CONST", 0)
            self.emit("LOAD_CONST", ("chkdict",))
            self.emit("IMPORT_NAME", "_static")
            self.emit("IMPORT_FROM", "chkdict")
            self.emit("STORE_NAME", "dict")

        super().visitModule(node)

    def emit_module_return(self, node: ast.Module) -> None:
        self.emit("LOAD_CONST", tuple(self.cur_mod.named_finals.keys()))
        self.emit("STORE_NAME", "__final_constants__")
        super().emit_module_return(node)

    def visitAssert(self, node: ast.Assert) -> None:
        super().visitAssert(node)
        # Only add casts when the assert is optimized out.
        if not self.optimization_lvl:
            return
        # Since we're narrowing types in asserts, we need to ensure we cast
        # all narrowed locals when asserts are optimized away.
        effect = self.get_node_data(node, NarrowingEffect)
        # As all of our effects store the final type, we can apply the effects on
        # an empty dictionary to get an overapproximation of what we need to cast.
        effect_types: Dict[str, Value] = {}
        effect_name_nodes: Dict[str, ast.Name] = {}
        effect.apply(effect_types, effect_name_nodes)
        for key, value in effect_types.items():
            if value.klass is not DYNAMIC:
                value.emit_name(effect_name_nodes[key], self)
                self.emit("CAST", value.klass.type_descr)
                self.emit("POP_TOP")

    def visitAugAttribute(self, node: AugAttribute, mode: str) -> None:
        if mode == "load":
            self.visit(node.value)
            self.emit("DUP_TOP")
            load = ast.Attribute(node.value, node.attr, ast.Load())
            load.lineno = node.lineno
            load.col_offset = node.col_offset
            self.get_type(node.value).emit_attr(load, self)
        elif mode == "store":
            self.emit("ROT_TWO")
            self.get_type(node.value).emit_attr(node, self)

    def visitAugSubscript(self, node: AugSubscript, mode: str) -> None:
        if mode == "load":
            self.get_type(node.value).emit_subscr(node.obj, 1, self)
        elif mode == "store":
            self.get_type(node.value).emit_store_subscr(node.obj, self)

    def visitAttribute(self, node: Attribute) -> None:
        self.update_lineno(node)
        if isinstance(node.ctx, ast.Load) and self._is_super_call(node.value):
            self.emit("LOAD_GLOBAL", "super")
            load_arg = self._emit_args_for_super(node.value, node.attr)
            self.emit("LOAD_ATTR_SUPER", load_arg)
        else:
            self.visit(node.value)
            self.get_type(node.value).emit_attr(node, self)

    def emit_type_check(self, dest: Class, src: Class, node: AST) -> None:
        if src is DYNAMIC_TYPE and dest is not OBJECT_TYPE and dest is not DYNAMIC_TYPE:
            assert not isinstance(dest, CType)
            self.emit("CAST", dest.type_descr)
        else:
            assert dest.can_assign_from(src)

    def visitAssignTarget(
        self, elt: expr, stmt: AST, value: Optional[expr] = None
    ) -> None:
        if isinstance(elt, (ast.Tuple, ast.List)):
            self._visitUnpack(elt)
            if isinstance(value, ast.Tuple) and len(value.elts) == len(elt.elts):
                for target, inner_value in zip(elt.elts, value.elts):
                    self.visitAssignTarget(target, stmt, inner_value)
            else:
                for target in elt.elts:
                    self.visitAssignTarget(target, stmt, None)
        else:
            if value is not None:
                self.emit_type_check(
                    self.get_type(elt).klass, self.get_type(value).klass, stmt
                )
            else:
                self.emit_type_check(self.get_type(elt).klass, DYNAMIC_TYPE, stmt)
            self.visit(elt)

    def visitAssign(self, node: Assign) -> None:
        self.set_lineno(node)
        self.visit(node.value)
        dups = len(node.targets) - 1
        for i in range(len(node.targets)):
            elt = node.targets[i]
            if i < dups:
                self.emit("DUP_TOP")
            if isinstance(elt, ast.AST):
                self.visitAssignTarget(elt, node, node.value)

    def visitAnnAssign(self, node: ast.AnnAssign) -> None:
        self.set_lineno(node)
        value = node.value
        if value:
            self.visit(value)
            self.emit_type_check(
                self.get_type(node.target).klass, self.get_type(value).klass, node
            )
            self.visit(node.target)
        target = node.target
        if isinstance(target, ast.Name):
            # If we have a simple name in a module or class, store the annotation
            if node.simple and isinstance(self.tree, (ast.Module, ast.ClassDef)):
                self.emitStoreAnnotation(target.id, node.annotation)
        elif isinstance(target, ast.Attribute):
            if not node.value:
                self.checkAnnExpr(target.value)
        elif isinstance(target, ast.Subscript):
            if not node.value:
                self.checkAnnExpr(target.value)
                self.checkAnnSubscr(target.slice)
        else:
            raise SystemError(
                f"invalid node type {type(node).__name__} for annotated assignment"
            )

        if not node.simple:
            self.checkAnnotation(node)

    def visitConstant(self, node: Constant) -> None:
        self.get_type(node).emit_constant(node, self)

    def get_final_literal(self, node: AST) -> Optional[ast.Constant]:
        return self.cur_mod.get_final_literal(node, self.scope)

    def visitName(self, node: Name) -> None:
        final_val = self.get_final_literal(node)
        if final_val is not None:
            # visit the constant directly
            return self.defaultVisit(final_val)
        self.get_type(node).emit_name(node, self)

    def visitAugAssign(self, node: AugAssign) -> None:
        self.get_type(node.target).emit_augassign(node, self)

    def visitAugName(self, node: AugName, mode: str) -> None:
        self.get_type(node).emit_augname(node, self, mode)

    def visitCompare(self, node: Compare) -> None:
        self.update_lineno(node)
        self.visit(node.left)
        cleanup = self.newBlock("cleanup")
        left = node.left
        for op, code in zip(node.ops[:-1], node.comparators[:-1]):
            optype = self.get_type(op)
            ltype = self.get_type(left)
            if ltype != optype:
                optype.emit_convert(ltype, self)
            self.emitChainedCompareStep(op, code, cleanup)
            left = code
        # now do the last comparison
        if node.ops:
            op = node.ops[-1]
            optype = self.get_type(op)
            ltype = self.get_type(left)
            if ltype != optype:
                optype.emit_convert(ltype, self)
            code = node.comparators[-1]
            self.visit(code)
            rtype = self.get_type(code)
            if rtype != optype:
                optype.emit_convert(rtype, self)
            optype.emit_compare(op, self)
        if len(node.ops) > 1:
            end = self.newBlock("end")
            self.emit("JUMP_FORWARD", end)
            self.nextBlock(cleanup)
            self.emit("ROT_TWO")
            self.emit("POP_TOP")
            self.nextBlock(end)

    def emitChainedCompareStep(
        self, op: cmpop, value: AST, cleanup: Block, always_pop: bool = False
    ) -> None:
        optype = self.get_type(op)
        self.visit(value)
        rtype = self.get_type(value)
        if rtype != optype:
            optype.emit_convert(rtype, self)
        self.emit("DUP_TOP")
        self.emit("ROT_THREE")
        optype.emit_compare(op, self)
        method = optype.emit_jumpif_only if always_pop else optype.emit_jumpif_pop_only
        method(cleanup, False, self)
        self.nextBlock(label="compare_or_cleanup")

    def visitBoolOp(self, node: BoolOp) -> None:
        end = self.newBlock()
        for child in node.values[:-1]:
            self.get_type(child).emit_jumpif_pop(
                child, end, type(node.op) == ast.Or, self
            )
            self.nextBlock()
        self.visit(node.values[-1])
        self.nextBlock(end)

    def visitBinOp(self, node: BinOp) -> None:
        self.get_type(node).emit_binop(node, self)

    def visitUnaryOp(self, node: UnaryOp, type_ctx: Optional[Class] = None) -> None:
        self.get_type(node).emit_unaryop(node, self)

    def visitCall(self, node: Call) -> None:
        self.get_type(node.func).emit_call(node, self)

    def visitSubscript(self, node: ast.Subscript, aug_flag: bool = False) -> None:
        self.get_type(node.value).emit_subscr(node, aug_flag, self)

    def _visitReturnValue(self, value: ast.AST, expected: Class) -> None:
        self.visit(value)
        if expected is not DYNAMIC_TYPE and self.get_type(value) is DYNAMIC:
            self.emit("CAST", expected.type_descr)

    def visitReturn(self, node: ast.Return) -> None:
        self.checkReturn(node)
        expected = self.get_type(self.tree).klass
        self.set_lineno(node)
        value = node.value
        is_return_constant = isinstance(value, ast.Constant)
        opcode = "RETURN_VALUE"
        oparg = 0
        if value:
            if not is_return_constant:
                self._visitReturnValue(value, expected)
                self.unwind_setup_entries(preserve_tos=True)
            else:
                self.unwind_setup_entries(preserve_tos=False)
                self._visitReturnValue(value, expected)
            if isinstance(expected, CType):
                opcode = "RETURN_INT"
                oparg = expected.instance.as_oparg()
        else:
            self.unwind_setup_entries(preserve_tos=False)
            self.emit("LOAD_CONST", None)

        self.emit(opcode, oparg)

    def visitDictComp(self, node: DictComp) -> None:
        dict_type = self.get_type(node)
        if dict_type in (DICT_TYPE.instance, DICT_EXACT_TYPE.instance):
            return super().visitDictComp(node)
        klass = dict_type.klass

        assert isinstance(klass, GenericClass) and (
            klass.type_def is CHECKED_DICT_TYPE
            or klass.type_def is CHECKED_DICT_EXACT_TYPE
        ), dict_type
        self.compile_comprehension(
            node,
            sys.intern("<dictcomp>"),
            node.key,
            node.value,
            "BUILD_CHECKED_MAP",
            (dict_type.klass.type_descr, 0),
        )

    def compile_subgendict(
        self, node: ast.Dict, begin: int, end: int, dict_descr: TypeDescr
    ) -> None:
        n = end - begin
        for i in range(begin, end):
            k = node.keys[i]
            assert k is not None
            self.visit(k)
            self.visit(node.values[i])

        self.emit("BUILD_CHECKED_MAP", (dict_descr, n))

    def visitDict(self, node: ast.Dict) -> None:
        dict_type = self.get_type(node)
        if dict_type in (DICT_TYPE.instance, DICT_EXACT_TYPE.instance):
            return super().visitDict(node)
        klass = dict_type.klass

        assert isinstance(klass, GenericClass) and (
            klass.type_def is CHECKED_DICT_TYPE
            or klass.type_def is CHECKED_DICT_EXACT_TYPE
        ), dict_type

        self.update_lineno(node)
        elements = 0
        is_unpacking = False
        built_final_dict = False

        # This is similar to the normal dict code generation, but instead of relying
        # upon an opcode for BUILD_MAP_UNPACK we invoke the update method on the
        # underlying dict type.  Therefore the first dict that we create becomes
        # the final dict.  This allows us to not introduce a new opcode, but we should
        # also be able to dispatch the INVOKE_METHOD rather efficiently.
        dict_descr = dict_type.klass.type_descr
        update_descr = dict_descr + ("update",)
        for i, (k, v) in enumerate(zip(node.keys, node.values)):
            is_unpacking = k is None
            if elements == 0xFFFF or (elements and is_unpacking):
                self.compile_subgendict(node, i - elements, i, dict_descr)
                built_final_dict = True
                elements = 0

            if is_unpacking:
                if not built_final_dict:
                    # {**foo, ...}, we need to generate the empty dict
                    self.emit("BUILD_CHECKED_MAP", (dict_descr, 0))
                    built_final_dict = True
                self.emit("DUP_TOP")
                self.visit(v)

                self.emit_invoke_method(update_descr, 1)
                self.emit("POP_TOP")
            else:
                elements += 1

        if elements or not built_final_dict:
            if built_final_dict:
                self.emit("DUP_TOP")
            self.compile_subgendict(
                node, len(node.keys) - elements, len(node.keys), dict_descr
            )
            if built_final_dict:
                self.emit_invoke_method(update_descr, 1)
                self.emit("POP_TOP")

    def visitFor(self, node: ast.For) -> None:
        iter_type = self.get_type(node.iter)
        return iter_type.emit_forloop(node, self)

    def emit_invoke_method(self, descr: TypeDescr, arg_count: int) -> None:
        # Emit a zero EXTENDED_ARG before so that we can optimize and insert the
        # arg count
        self.emit("EXTENDED_ARG", 0)
        self.emit("INVOKE_METHOD", (descr, arg_count))

    def defaultVisit(self, node: object, *args: object) -> None:
        self.node = node
        klass = node.__class__
        meth = self._default_cache.get(klass, None)
        if meth is None:
            className = klass.__name__
            meth = getattr(
                super(Static38CodeGenerator, Static38CodeGenerator),
                "visit" + className,
                StaticCodeGenerator.generic_visit,
            )
            self._default_cache[klass] = meth
        return meth(self, node, *args)

    def compileJumpIf(self, test: AST, next: Block, is_if_true: bool) -> None:
        self.get_type(test).emit_jumpif(test, next, is_if_true, self)

    def _calculate_idx(
        self, arg_name: str, non_cellvar_pos: int, cellvars: IndexedSet
    ) -> int:
        try:
            offset = cellvars.index(arg_name)
        except ValueError:
            return non_cellvar_pos
        else:
            # the negative sign indicates to the runtime/JIT that this is a cellvar
            return -(offset + 1)


StaticCodeGenerator = Static38CodeGenerator
