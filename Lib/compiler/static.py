from __future__ import annotations

import ast
import linecache
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
)
from contextlib import contextmanager, nullcontext
from enum import IntEnum
from functools import partial
from types import BuiltinFunctionType, CodeType, MethodDescriptorType
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
    rand,
)

from . import symbols, opcode38static
from .consts import SC_LOCAL, SC_GLOBAL_EXPLICIT, SC_GLOBAL_IMPLICIT
from .opcodebase import Opcode
from .optimizer import AstOptimizer
from .pyassem import Block, PyFlowGraph, PyFlowGraphCinder
from .pycodegen import (
    AugAttribute,
    AugName,
    AugSubscript,
    CodeGenerator,
    CinderCodeGenerator,
    Delegator,
    compile,
    wrap_aug,
    FOR_LOOP,
)
from .symbols import Scope, SymbolVisitor, ModuleScope, ClassScope
from .unparse import to_expr
from .visitor import ASTVisitor, ASTRewriter, TAst


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


CBOOL_TYPE: CIntType
INT8_TYPE: CIntType
INT16_TYPE: CIntType
INT32_TYPE: CIntType
INT64_TYPE: CIntType
INT64_VALUE: CIntInstance
SIGNED_CINT_TYPES: Sequence[CIntType]
INT_TYPE: NumClass
INT_EXACT_TYPE: NumClass
FLOAT_TYPE: NumClass
COMPLEX_TYPE: NumClass
BOOL_TYPE: Class
ARRAY_TYPE: Class
DICT_TYPE: Class
LIST_TYPE: Class
TUPLE_TYPE: Class
SET_TYPE: Class

OBJECT_TYPE: Class
OBJECT: Value

DYNAMIC_TYPE: DynamicClass
DYNAMIC: DynamicInstance
FUNCTION_TYPE: Class
METHOD_TYPE: Class
MEMBER_TYPE: Class
NONE_TYPE: Class
TYPE_TYPE: Class
ARG_TYPE: Class
SLICE_TYPE: Class

CHAR_TYPE: CIntType
DOUBLE_TYPE: CDoubleType

# Prefix for temporary var names. It's illegal in normal
# Python, so there's no chance it will ever clash with a
# user defined name.
_TMP_VAR_PREFIX = "_pystatic_.0._tmp__"


CMPOP_SIGILS: Mapping[Type[cmpop], str] = {
    ast.Lt: "<",
    ast.Gt: ">",
    ast.Eq: "==",
    ast.NotEq: "!=",
    ast.LtE: "<=",
    ast.GtE: ">=",
    ast.Is: "is",
    ast.IsNot: "is",
}


def syntax_error(msg: str, filename: str, node: AST) -> TypedSyntaxError:
    lineno, offset, source_line = error_location(filename, node)
    return TypedSyntaxError(msg, (filename, lineno, offset, source_line))


def error_location(filename: str, node: AST) -> Tuple[int, int, Optional[str]]:
    source_line = linecache.getline(filename, node.lineno)
    return (node.lineno, node.col_offset, source_line or None)


@contextmanager
def error_context(filename: str, node: AST) -> Generator[None, None, None]:
    """Add error location context to any TypedSyntaxError raised in with block."""
    try:
        yield
    except TypedSyntaxError as exc:
        if exc.filename is None:
            exc.filename = filename
        if (exc.lineno, exc.offset) == (None, None):
            exc.lineno, exc.offset, exc.text = error_location(filename, node)
        raise


class TypeRef:
    """Stores unresolved typed references, capturing the referring module
    as well as the annotation"""

    def __init__(self, module: ModuleTable, ref: ast.expr) -> None:
        self.module = module
        self.ref = ref

    def resolved(self, is_declaration: bool = False) -> Class:
        res = self.module.resolve_annotation(self.ref, is_declaration=is_declaration)
        if res is None:
            return DYNAMIC_TYPE
        return res

    def __repr__(self) -> str:
        return f"TypeRef({self.module.name}, {ast.dump(self.ref)})"


class ResolvedTypeRef(TypeRef):
    def __init__(self, type: Class) -> None:
        self._resolved = type

    def resolved(self, is_declaration: bool = False) -> Class:
        return self._resolved

    def __repr__(self) -> str:
        return f"ResolvedTypeRef({self.resolved()})"


# Pyre doesn't support recursive generics, so we can't represent the recursively
# nested tuples that make up a type_descr. Fortunately we don't need to, since
# we don't parse them in Python, we just generate them and emit them as
# constants. So just call them `Tuple[object, ...]`
TypeDescr = Tuple[object, ...]


class TypeName:
    def __init__(self, module: str, name: str) -> None:
        self.module = module
        self.name = name

    @property
    def type_descr(self) -> TypeDescr:
        """The metadata emitted into the const pool to describe a type.

        For normal types this is just the fully qualified type name as a tuple
        ('mypackage', 'mymod', 'C'). For optional types we have an extra '?'
        element appended. For generic types we append a tuple of the generic
        args' type_descrs.
        """
        return (self.module, self.name)

    @property
    def friendly_name(self) -> str:
        if self.module and self.module not in ("builtins", "__static__", "typing"):
            return f"{self.module}.{self.name}"
        return self.name


class GenericTypeName(TypeName):
    def __init__(self, module: str, name: str, args: Tuple[Class, ...]) -> None:
        super().__init__(module, name)
        self.args = args

    @property
    def type_descr(self) -> TypeDescr:
        gen_args: List[TypeDescr] = []
        for arg in self.args:
            gen_args.append(arg.type_descr)
        return (self.module, self.name, tuple(gen_args))

    @property
    def friendly_name(self) -> str:
        args = ", ".join(arg.instance.name for arg in self.args)
        return f"{super().friendly_name}[{args}]"


GenericTypeIndex = Tuple["Class", ...]
GenericTypesDict = Dict["Class", Dict[GenericTypeIndex, "Class"]]


class SymbolTable:
    def __init__(self) -> None:
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
            # TODO: Need typed members for dict
            "Dict": DICT_TYPE,
            "List": LIST_TYPE,
            "Final": FINAL_TYPE,
            "final": FINAL_METHOD_TYPE,
            "NamedTuple": NAMED_TUPLE_TYPE,
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
                    TypeName("builtins", "int"), pytype=int, literal_value=RAND_MAX
                ).instance,
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

    def __getitem__(self, name: str) -> ModuleTable:
        return self.modules[name]

    def __setitem__(self, name: str, value: ModuleTable) -> None:
        self.modules[name] = value

    def add_module(self, name: str, filename: str, tree: AST) -> None:
        decl_visit = DeclarationVisitor(name, filename, self)
        decl_visit.visit(tree)
        decl_visit.finish_bind()

    def compile(
        self, name: str, filename: str, tree: AST, optimize: int = 0
    ) -> CodeType:
        if name not in self.modules:
            self.add_module(name, filename, tree)

        tree = AstOptimizer(optimize=optimize > 0).visit(tree)

        # Analyze variable scopes
        s = SymbolVisitor()
        s.visit(tree)

        # Analyze the types of objects within local scopes
        type_binder = TypeBinder(s, filename, self, name, optimize)
        type_binder.visit(tree)

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


TType = TypeVar("TType")


class ModuleTable:
    def __init__(
        self,
        name: str,
        filename: str,
        symtable: SymbolTable,
        members: Optional[Dict[str, Value]] = None,
    ) -> None:
        self.name = name
        self.filename = filename
        self.children: Dict[str, Value] = members or {}
        self.symtable = symtable
        self.types: Dict[Union[AST, Delegator], Value] = {}
        self.node_data: Dict[Tuple[Union[AST, Delegator], object], object] = {}
        self.nonchecked_dicts = False
        self.tinyframe = False
        self.noframe = False
        self.decls: List[Tuple[AST, Optional[Value]]] = []
        # TODO: final constants should be typed to literals, and
        # this should be removed in the future
        self.named_finals: Dict[str, ast.Constant] = {}
        # Functions in this module that have been decorated with
        # `dynamic_return`. We actually store their `.args` node in here, not
        # the `FunctionDef` node itself, since strict modules rewriter will
        # replace the latter in between decls visit and type binding / codegen.
        self.dynamic_returns: Set[ast.AST] = set()
        # Have we completed our first pass through the module, populating
        # imports and types defined in the module? Until we have, resolving
        # type annotations is not safe.
        self.first_pass_done = False

    def finish_bind(self) -> None:
        self.first_pass_done = True
        for node, value in self.decls:
            with error_context(self.filename, node):
                if value is not None:
                    value.finish_bind(self)
                elif isinstance(node, ast.AnnAssign):
                    typ = self.resolve_annotation(node.annotation, is_declaration=True)
                    if isinstance(typ, FinalClass):
                        target = node.target
                        value = node.value
                        if not value:
                            raise TypedSyntaxError(
                                "Must assign a value when declaring a Final"
                            )
                        elif (
                            not isinstance(typ, CType)
                            and isinstance(target, ast.Name)
                            and isinstance(value, ast.Constant)
                        ):
                            self.named_finals[target.id] = value

        # We don't need these anymore...
        self.decls.clear()

    def resolve_type(self, node: ast.AST) -> Optional[Class]:
        # TODO handle Call
        return self._resolve(node, self.resolve_type)

    def _resolve(
        self,
        node: ast.AST,
        _resolve: typingCallable[[ast.AST], Optional[Class]],
        _resolve_subscr_target: Optional[
            typingCallable[[ast.AST], Optional[Class]]
        ] = None,
    ) -> Optional[Class]:
        if isinstance(node, ast.Name):
            res = self.resolve_name(node.id)
            if isinstance(res, Class):
                return res
        elif isinstance(node, Subscript):
            slice = node.slice
            if isinstance(slice, Index):
                val = (_resolve_subscr_target or _resolve)(node.value)
                if val is not None:
                    value = slice.value
                    if isinstance(value, ast.Tuple):
                        anns = []
                        for elt in value.elts:
                            ann = _resolve(elt) or DYNAMIC_TYPE
                            anns.append(ann)
                        values = tuple(anns)
                        gen = val.make_generic_type(values, self.symtable.generic_types)
                        return gen or val
                    else:
                        index = _resolve(value) or DYNAMIC_TYPE
                        gen = val.make_generic_type(
                            (index,), self.symtable.generic_types
                        )
                        return gen or val
        # TODO handle Attribute

    def resolve_annotation(
        self,
        node: ast.AST,
        is_declaration: bool = False,
    ) -> Optional[Class]:
        assert self.first_pass_done, (
            "Type annotations cannot be resolved until after initial pass, "
            "so that all imports and types are available."
        )

        with error_context(self.filename, node):
            klass = self._resolve_annotation(node)

            if isinstance(klass, FinalClass) and not is_declaration:
                raise TypedSyntaxError(
                    "Final annotation is only valid in initial declaration "
                    "of attribute or module-level constant",
                )

            # Even if we know that e.g. `builtins.str` is the exact `str` type and
            # not a subclass, and it's useful to track that knowledge, when we
            # annotate `x: str` that annotation should not exclude subclasses.
            return inexact_type(klass) if klass else None

    def _resolve_annotation(self, node: ast.AST) -> Optional[Class]:
        # First try to resolve non-annotation-specific forms. For resolving the
        # outer target of a subscript (e.g. `Final` in `Final[int]`) we pass
        # `is_declaration=True` to allow `Final` in that position; if in fact
        # we are not resolving a declaration, the outer `resolve_annotation`
        # (our caller) will still catch the generic Final that we end up
        # returning.
        typ = self._resolve(
            node,
            self.resolve_annotation,
            _resolve_subscr_target=partial(
                self.resolve_annotation, is_declaration=True
            ),
        )
        if typ:
            return typ
        elif isinstance(node, ast.Str):
            # pyre-ignore[16]: `AST` has no attribute `body`.
            return self.resolve_annotation(ast.parse(node.s, "", "eval").body)
        elif isinstance(node, ast.Constant):
            sval = node.value
            if sval is None:
                return NONE_TYPE
            elif isinstance(sval, str):
                return self.resolve_annotation(ast.parse(node.value, "", "eval").body)
        elif isinstance(node, NameConstant) and node.value is None:
            return NONE_TYPE
        elif isinstance(node, ast.BinOp) and isinstance(node.op, ast.BitOr):
            ltype = self.resolve_annotation(node.left)
            rtype = self.resolve_annotation(node.right)
            if ltype is None or rtype is None:
                return None
            return UNION_TYPE.make_generic_type(
                (ltype, rtype), self.symtable.generic_types
            )

    def resolve_name(self, name: str) -> Optional[Value]:
        return self.children.get(name) or self.symtable.builtins.children.get(name)

    def get_final_literal(self, node: AST, scope: Scope) -> Optional[ast.Constant]:
        if not isinstance(node, Name):
            return None

        final_val = self.named_finals.get(node.id, None)
        if (
            final_val is not None
            and isinstance(node.ctx, ast.Load)
            and (
                # Ensure the name is not shadowed in the local scope
                isinstance(scope, ModuleScope)
                or node.id not in scope.defs
            )
        ):
            return final_val


TClass = TypeVar("TClass", bound="Class", covariant=True)
TClassInv = TypeVar("TClassInv", bound="Class")


class Value:
    """base class for all values tracked at compile time."""

    def __init__(self, klass: Class) -> None:
        """name: the name of the value, for instances this is used solely for
        debug/reporting purposes.  In Class subclasses this will be the
        qualified name (e.g. module.Foo).
        klass: the Class of this object"""
        self.klass = klass

    @property
    def name(self) -> str:
        return type(self).__name__

    def finish_bind(self, module: ModuleTable) -> None:
        pass

    def make_generic_type(
        self, index: GenericTypeIndex, generic_types: GenericTypesDict
    ) -> Optional[Class]:
        pass

    def get_iter_type(self, node: ast.expr, visitor: TypeBinder) -> Value:
        """returns the type that is produced when iterating over this value"""
        raise visitor.syntax_error(f"cannot iterate over {self.name}", node)

    def as_oparg(self) -> int:
        raise TypeError(f"{self.name} not valid here")

    def bind_attr(
        self, node: ast.Attribute, visitor: TypeBinder, type_ctx: Optional[Class]
    ) -> None:
        visitor.visit(node.value)
        raise visitor.syntax_error(f"cannot load attribute from {self.name}", node)

    def bind_call(
        self, node: ast.Call, visitor: TypeBinder, type_ctx: Optional[Class]
    ) -> NarrowingEffect:
        raise visitor.syntax_error(f"cannot call {self.name}", node)

    def check_args_for_primitives(self, node: ast.Call, visitor: TypeBinder) -> None:
        for arg in node.args:
            if isinstance(visitor.get_type(arg), CInstance):
                raise visitor.syntax_error("Call argument cannot be a primitive", arg)
        for arg in node.keywords:
            if isinstance(visitor.get_type(arg.value), CInstance):
                raise visitor.syntax_error(
                    "Call argument cannot be a primitive", arg.value
                )

    def bind_descr_get(
        self,
        node: ast.Attribute,
        inst: Optional[Object[TClassInv]],
        ctx: TClassInv,
        visitor: TypeBinder,
        type_ctx: Optional[Class],
    ) -> None:
        raise visitor.syntax_error(f"cannot get descriptor {self.name}", node)

    def bind_decorate_function(
        self, visitor: DeclarationVisitor, fn: Function | StaticMethod
    ) -> Optional[Value]:
        return None

    def bind_decorate_class(self, klass: Class) -> Class:
        return DYNAMIC_TYPE

    def bind_subscr(
        self, node: ast.Subscript, type: Value, visitor: TypeBinder
    ) -> None:
        raise visitor.syntax_error(f"cannot index {self.name}", node)

    def emit_subscr(
        self, node: ast.Subscript, aug_flag: bool, code_gen: Static38CodeGenerator
    ) -> None:
        code_gen.defaultVisit(node, aug_flag)

    def emit_store_subscr(
        self, node: ast.Subscript, code_gen: Static38CodeGenerator
    ) -> None:
        code_gen.emit("ROT_THREE")
        code_gen.emit("STORE_SUBSCR")

    def emit_call(self, node: ast.Call, code_gen: Static38CodeGenerator) -> None:
        code_gen.defaultVisit(node)

    def emit_attr(
        self, node: Union[ast.Attribute, AugAttribute], code_gen: Static38CodeGenerator
    ) -> None:
        if isinstance(node.ctx, ast.Store):
            code_gen.emit("STORE_ATTR", code_gen.mangle(node.attr))
        elif isinstance(node.ctx, ast.Del):
            code_gen.emit("DELETE_ATTR", code_gen.mangle(node.attr))
        else:
            code_gen.emit("LOAD_ATTR", code_gen.mangle(node.attr))

    def bind_compare(
        self,
        node: ast.Compare,
        left: expr,
        op: cmpop,
        right: expr,
        visitor: TypeBinder,
        type_ctx: Optional[Class],
    ) -> bool:
        raise visitor.syntax_error(f"cannot compare with {self.name}", node)

    def bind_reverse_compare(
        self,
        node: ast.Compare,
        left: expr,
        op: cmpop,
        right: expr,
        visitor: TypeBinder,
        type_ctx: Optional[Class],
    ) -> bool:
        raise visitor.syntax_error(f"cannot reverse  with {self.name}", node)

    def emit_compare(self, op: cmpop, code_gen: Static38CodeGenerator) -> None:
        code_gen.defaultEmitCompare(op)

    def bind_binop(
        self, node: ast.BinOp, visitor: TypeBinder, type_ctx: Optional[Class]
    ) -> bool:
        raise visitor.syntax_error(f"cannot bin op with {self.name}", node)

    def bind_reverse_binop(
        self, node: ast.BinOp, visitor: TypeBinder, type_ctx: Optional[Class]
    ) -> bool:
        raise visitor.syntax_error(f"cannot reverse bin op with {self.name}", node)

    def bind_unaryop(
        self, node: ast.UnaryOp, visitor: TypeBinder, type_ctx: Optional[Class]
    ) -> None:
        raise visitor.syntax_error(f"cannot reverse unary op with {self.name}", node)

    def emit_binop(self, node: ast.BinOp, code_gen: Static38CodeGenerator) -> None:
        code_gen.defaultVisit(node)

    def emit_forloop(self, node: ast.For, code_gen: Static38CodeGenerator) -> None:
        start = code_gen.newBlock("default_forloop_start")
        anchor = code_gen.newBlock("default_forloop_anchor")
        after = code_gen.newBlock("default_forloop_after")

        code_gen.set_lineno(node)
        code_gen.push_loop(FOR_LOOP, start, after)
        code_gen.visit(node.iter)
        code_gen.emit("GET_ITER")

        code_gen.nextBlock(start)
        code_gen.emit("FOR_ITER", anchor)
        code_gen.visit(node.target)
        code_gen.visit(node.body)
        code_gen.emit("JUMP_ABSOLUTE", start)
        code_gen.nextBlock(anchor)
        code_gen.pop_loop()

        if node.orelse:
            code_gen.visit(node.orelse)
        code_gen.nextBlock(after)

    def emit_unaryop(self, node: ast.UnaryOp, code_gen: Static38CodeGenerator) -> None:
        code_gen.defaultVisit(node)

    def emit_augassign(
        self, node: ast.AugAssign, code_gen: Static38CodeGenerator
    ) -> None:
        code_gen.defaultVisit(node)

    def emit_augname(
        self, node: AugName, code_gen: Static38CodeGenerator, mode: str
    ) -> None:
        code_gen.defaultVisit(node, mode)

    def bind_constant(self, node: ast.Constant, visitor: TypeBinder) -> None:
        raise visitor.syntax_error(f"cannot constant with {self.name}", node)

    def emit_constant(
        self, node: ast.Constant, code_gen: Static38CodeGenerator
    ) -> None:
        return code_gen.defaultVisit(node)

    def emit_name(self, node: ast.Name, code_gen: Static38CodeGenerator) -> None:
        return code_gen.defaultVisit(node)

    def emit_jumpif(
        self, test: AST, next: Block, is_if_true: bool, code_gen: Static38CodeGenerator
    ) -> None:
        CinderCodeGenerator.compileJumpIf(code_gen, test, next, is_if_true)

    def emit_jumpif_pop(
        self, test: AST, next: Block, is_if_true: bool, code_gen: Static38CodeGenerator
    ) -> None:
        CinderCodeGenerator.compileJumpIfPop(code_gen, test, next, is_if_true)

    def emit_box(self, node: expr, code_gen: Static38CodeGenerator) -> None:
        raise RuntimeError(f"Unsupported box type: {code_gen.get_type(node)}")

    def emit_unbox(self, node: expr, code_gen: Static38CodeGenerator) -> None:
        raise RuntimeError("Unsupported unbox type")

    def get_fast_len_type(self) -> Optional[int]:
        return None

    def emit_len(
        self, node: ast.Call, code_gen: Static38CodeGenerator, boxed: bool
    ) -> None:
        if not boxed:
            raise RuntimeError("Unsupported type for clen()")
        return self.emit_call(node, code_gen)

    def make_generic(
        self, new_type: Class, name: GenericTypeName, generic_types: GenericTypesDict
    ) -> Value:
        return self

    def emit_convert(self, to_type: Value, code_gen: Static38CodeGenerator) -> None:
        pass


class Object(Value, Generic[TClass]):
    """Represents an instance of a type at compile time"""

    klass: TClass

    @property
    def name(self) -> str:
        return self.klass.instance_name

    def as_oparg(self) -> int:
        return TYPED_OBJECT

    def bind_call(
        self, node: ast.Call, visitor: TypeBinder, type_ctx: Optional[Class]
    ) -> NarrowingEffect:
        visitor.set_type(node, DYNAMIC)
        for arg in node.args:
            visitor.visit(arg)

        for arg in node.keywords:
            visitor.visit(arg.value)
        self.check_args_for_primitives(node, visitor)
        return NO_EFFECT

    def bind_attr(
        self, node: ast.Attribute, visitor: TypeBinder, type_ctx: Optional[Class]
    ) -> None:
        for base in self.klass.mro:
            member = base.members.get(node.attr)
            if member is not None:
                member.bind_descr_get(node, self, self.klass, visitor, type_ctx)
                return

        visitor.visit(node.value)
        if node.attr == "__class__":
            visitor.set_type(node, self.klass)
        else:
            visitor.set_type(node, DYNAMIC)

    def emit_attr(
        self, node: Union[ast.Attribute, AugAttribute], code_gen: Static38CodeGenerator
    ) -> None:
        for base in self.klass.mro:
            member = base.members.get(node.attr)
            if member is not None and isinstance(member, Slot):
                type_descr = member.container_type.type_descr
                type_descr += (member.slot_name,)
                if isinstance(node.ctx, ast.Store):
                    code_gen.emit("STORE_FIELD", type_descr)
                elif isinstance(node.ctx, ast.Del):
                    code_gen.emit("DELETE_ATTR", node.attr)
                else:
                    code_gen.emit("LOAD_FIELD", type_descr)
                return

        super().emit_attr(node, code_gen)

    def bind_descr_get(
        self,
        node: ast.Attribute,
        inst: Optional[Object[TClass]],
        ctx: Class,
        visitor: TypeBinder,
        type_ctx: Optional[Class],
    ) -> None:
        visitor.set_type(node, DYNAMIC)

    def bind_subscr(
        self, node: ast.Subscript, type: Value, visitor: TypeBinder
    ) -> None:
        visitor.check_can_assign_from(DYNAMIC_TYPE, type.klass, node)
        visitor.set_type(node, DYNAMIC)

    def bind_compare(
        self,
        node: ast.Compare,
        left: expr,
        op: cmpop,
        right: expr,
        visitor: TypeBinder,
        type_ctx: Optional[Class],
    ) -> bool:
        visitor.set_type(op, DYNAMIC)
        visitor.set_type(node, DYNAMIC)
        return False

    def bind_reverse_compare(
        self,
        node: ast.Compare,
        left: expr,
        op: cmpop,
        right: expr,
        visitor: TypeBinder,
        type_ctx: Optional[Class],
    ) -> bool:
        visitor.set_type(op, DYNAMIC)
        visitor.set_type(node, DYNAMIC)
        return False

    def bind_binop(
        self, node: ast.BinOp, visitor: TypeBinder, type_ctx: Optional[Class]
    ) -> bool:
        return False

    def bind_reverse_binop(
        self, node: ast.BinOp, visitor: TypeBinder, type_ctx: Optional[Class]
    ) -> bool:
        # we'll set the type in case we're the only one called
        visitor.set_type(node, DYNAMIC)
        return False

    def bind_unaryop(
        self, node: ast.UnaryOp, visitor: TypeBinder, type_ctx: Optional[Class]
    ) -> None:
        if isinstance(node.op, ast.Not):
            visitor.set_type(node, BOOL_TYPE.instance)
        else:
            visitor.set_type(node, DYNAMIC)

    def bind_constant(self, node: ast.Constant, visitor: TypeBinder) -> None:
        node_type = CONSTANT_TYPES[type(node.value)]
        visitor.set_type(node, node_type)
        visitor.check_can_assign_from(self.klass, node_type.klass, node)

    def get_iter_type(self, node: ast.expr, visitor: TypeBinder) -> Value:
        """returns the type that is produced when iterating over this value"""
        return DYNAMIC

    def __repr__(self) -> str:
        return f"<{self.name}>"


class Class(Object["Class"]):
    """Represents a type object at compile time"""

    suppress_exact = False

    def __init__(
        self,
        type_name: TypeName,
        bases: Optional[List[Class]] = None,
        instance: Optional[Value] = None,
        klass: Optional[Class] = None,
        members: Optional[Dict[str, Value]] = None,
        is_exact: bool = False,
        pytype: Optional[Type[object]] = None,
    ) -> None:
        super().__init__(klass or TYPE_TYPE)
        assert isinstance(bases, (type(None), list))
        self.type_name = type_name
        self.instance: Value = instance or Object(self)
        self.bases: List[Class] = bases or []
        self._mro: Optional[List[Class]] = None
        self._mro_type_descrs: Optional[Set[TypeDescr]] = None
        self.members: Dict[str, Value] = members or {}
        self.is_exact = is_exact
        self.is_final = False
        self.allow_weakrefs = False
        self.donotcompile = False
        if pytype:
            self.members.update(make_type_dict(self, pytype))
        # store attempted slot redefinitions during type declaration, for resolution in finish_bind
        self._slot_redefs: Dict[str, List[TypeRef]] = {}

    @property
    def name(self) -> str:
        return f"Type[{self.instance_name}]"

    @property
    def instance_name(self) -> str:
        name = self.qualname
        if self.is_exact and not self.suppress_exact:
            name = f"Exact[{name}]"
        return name

    @property
    def qualname(self) -> str:
        return self.type_name.friendly_name

    @property
    def is_generic_parameter(self) -> bool:
        """Returns True if this Class represents a generic parameter"""
        return False

    @property
    def contains_generic_parameters(self) -> bool:
        """Returns True if this class contains any generic parameters"""
        return False

    @property
    def is_generic_type(self) -> bool:
        """Returns True if this class is a generic type"""
        return False

    @property
    def is_generic_type_definition(self) -> bool:
        """Returns True if this class is a generic type definition.
        It'll be a generic type which still has unbound generic type
        parameters"""
        return False

    @property
    def generic_type_def(self) -> Optional[Class]:
        """Gets the generic type definition that defined this class"""
        return None

    def make_generic_type(
        self,
        index: Tuple[Class, ...],
        generic_types: GenericTypesDict,
    ) -> Optional[Class]:
        """Binds the generic type parameters to a generic type definition"""
        return None

    def bind_attr(
        self, node: ast.Attribute, visitor: TypeBinder, type_ctx: Optional[Class]
    ) -> None:
        for base in self.mro:
            member = base.members.get(node.attr)
            if member is not None:
                member.bind_descr_get(node, None, self, visitor, type_ctx)
                return

        super().bind_attr(node, visitor, type_ctx)

    def bind_binop(
        self, node: ast.BinOp, visitor: TypeBinder, type_ctx: Optional[Class]
    ) -> bool:
        if isinstance(node.op, ast.BitOr):
            rtype = visitor.get_type(node.right)
            if rtype is NONE_TYPE.instance:
                rtype = NONE_TYPE
            if rtype is DYNAMIC:
                rtype = DYNAMIC_TYPE
            if not isinstance(rtype, Class):
                raise visitor.syntax_error(
                    f"unsupported operand type(s) for |: {self.name} and {rtype.name}",
                    node,
                )
            union = UNION_TYPE.make_generic_type(
                (self, rtype), visitor.symtable.generic_types
            )
            visitor.set_type(node, union)
            return True

        return super().bind_binop(node, visitor, type_ctx)

    @property
    def can_be_narrowed(self) -> bool:
        return True

    @property
    def type_descr(self) -> TypeDescr:
        return self.type_name.type_descr

    def bind_call(
        self, node: ast.Call, visitor: TypeBinder, type_ctx: Optional[Class]
    ) -> NarrowingEffect:
        visitor.set_type(node, self.instance)
        for arg in node.args:
            visitor.visit(arg)
        for arg in node.keywords:
            visitor.visit(arg.value)
        self.check_args_for_primitives(node, visitor)
        return NO_EFFECT

    def can_assign_from(self, src: Class) -> bool:
        """checks to see if the src value can be assigned to this value.  Currently
        you can assign a derived type to a base type.  You cannot assign a primitive
        type to an object type.

        At some point we may also support some form of interfaces via protocols if we
        implement a more efficient form of interface dispatch than doing the dictionary
        lookup for the member."""
        return src is self or (
            not self.is_exact and not isinstance(src, CType) and self.issubclass(src)
        )

    def __repr__(self) -> str:
        return f"<{self.name} class>"

    def isinstance(self, src: Value) -> bool:
        return self.issubclass(src.klass)

    def issubclass(self, src: Class) -> bool:
        return self.type_descr in src.mro_type_descrs

    def finish_bind(self, module: ModuleTable) -> None:
        for name, new_type_refs in self._slot_redefs.items():
            cur_slot = self.members[name]
            assert isinstance(cur_slot, Slot)
            cur_type = cur_slot.decl_type
            if any(tr.resolved() != cur_type for tr in new_type_refs):
                raise TypedSyntaxError(
                    f"conflicting type definitions for slot {name} in {self.name}"
                )
        self._slot_redefs = {}

        inherited = set()
        for name, my_value in self.members.items():
            for base in self.mro[1:]:
                value = base.members.get(name)
                if value is not None and type(my_value) != type(value):
                    # TODO: There's more checking we should be doing to ensure
                    # this is a compatible override
                    raise TypedSyntaxError(
                        f"class cannot hide inherited member: {value!r}"
                    )
                elif isinstance(value, Slot):
                    inherited.add(name)
                elif isinstance(value, (Function, StaticMethod)):
                    if value.is_final:
                        raise TypedSyntaxError(
                            f"Cannot assign to a Final attribute of {self.instance.name}:{name}"
                        )
            if (
                isinstance(my_value, Slot)
                and my_value.is_final
                and not my_value.assignment
            ):
                raise TypedSyntaxError(
                    f"Final attribute not initialized: {self.instance.name}:{name}"
                )

        for name in inherited:
            assert type(self.members[name]) is Slot
            del self.members[name]

    def define_slot(
        self,
        name: str,
        type_ref: Optional[TypeRef] = None,
        assignment: Optional[AST] = None,
    ) -> None:
        existing = self.members.get(name)
        if existing is None:
            self.members[name] = Slot(
                type_ref or ResolvedTypeRef(DYNAMIC_TYPE), name, self, assignment
            )
        elif isinstance(existing, Slot):
            if not existing.assignment:
                existing.assignment = assignment
            if type_ref is not None:
                self._slot_redefs.setdefault(name, []).append(type_ref)
        else:
            raise TypedSyntaxError(
                f"slot conflicts with other member {name} in {self.name}"
            )

    def define_function(
        self,
        name: str,
        func: Function | StaticMethod,
        visitor: DeclarationVisitor,
    ) -> None:
        if name in self.members:
            raise TypedSyntaxError(
                f"function conflicts with other member {name} in {self.name}"
            )

        func.set_container_type(self)

        self.members[name] = func

    @property
    def mro(self) -> Sequence[Class]:
        mro = self._mro
        if mro is None:
            if not all(self.bases):
                # TODO: We can't compile w/ unknown bases
                mro = []
            else:
                mro = _mro(self)
            self._mro = mro

        return mro

    @property
    def mro_type_descrs(self) -> Collection[TypeDescr]:
        cached = self._mro_type_descrs
        if cached is None:
            self._mro_type_descrs = cached = {b.type_descr for b in self.mro}
        return cached

    def bind_generics(
        self,
        name: GenericTypeName,
        generic_types: Dict[Class, Dict[Tuple[Class, ...], Class]],
    ) -> Class:
        return self

    def get_own_member(self, name: str) -> Optional[Value]:
        return self.members.get(name)

    def get_parent_member(self, name: str) -> Optional[Value]:
        # the first entry of mro is the class itself
        for b in self.mro[1:]:
            slot = b.members.get(name, None)
            if slot:
                return slot

    def get_member(self, name: str) -> Optional[Value]:
        member = self.get_own_member(name)
        if member:
            return member
        return self.get_parent_member(name)


class GenericClass(Class):
    type_name: GenericTypeName
    is_variadic = False

    def __init__(
        self,
        name: GenericTypeName,
        bases: Optional[List[Class]] = None,
        instance: Optional[Object[Class]] = None,
        klass: Optional[Class] = None,
        members: Optional[Dict[str, Value]] = None,
        type_def: Optional[GenericClass] = None,
        is_exact: bool = False,
        pytype: Optional[Type[object]] = None,
    ) -> None:
        super().__init__(name, bases, instance, klass, members, is_exact, pytype)
        self.gen_name = name
        self.type_def = type_def

    def bind_call(
        self, node: ast.Call, visitor: TypeBinder, type_ctx: Optional[Class]
    ) -> NarrowingEffect:
        if self.contains_generic_parameters:
            raise visitor.syntax_error(
                f"cannot create instances of a generic {self.name}", node
            )
        return super().bind_call(node, visitor, type_ctx)

    def bind_subscr(
        self, node: ast.Subscript, type: Value, visitor: TypeBinder
    ) -> None:
        slice = node.slice
        if not isinstance(slice, ast.Index):
            raise visitor.syntax_error("can't slice generic types", node)

        visitor.visit(node.slice)
        val = slice.value

        if isinstance(val, ast.Tuple):
            multiple: List[Class] = []
            for elt in val.elts:
                klass = visitor.cur_mod.resolve_annotation(elt)
                if klass is None:
                    visitor.set_type(node, DYNAMIC)
                    return
                multiple.append(klass)

            index = tuple(multiple)
            if (not self.is_variadic) and len(val.elts) != len(self.gen_name.args):
                raise visitor.syntax_error(
                    "incorrect number of generic arguments", node
                )
        else:
            if (not self.is_variadic) and len(self.gen_name.args) != 1:
                raise visitor.syntax_error(
                    "incorrect number of generic arguments", node
                )

            single = visitor.cur_mod.resolve_annotation(val)
            if single is None:
                visitor.set_type(node, DYNAMIC)
                return

            index = (single,)

        klass = self.make_generic_type(index, visitor.symtable.generic_types)
        visitor.set_type(node, klass)

    @property
    def type_args(self) -> Sequence[Class]:
        return self.type_name.args

    @property
    def contains_generic_parameters(self) -> bool:
        for arg in self.gen_name.args:
            if arg.is_generic_parameter:
                return True
        return False

    @property
    def is_generic_type(self) -> bool:
        return True

    @property
    def is_generic_type_definition(self) -> bool:
        return self.type_def is None

    @property
    def generic_type_def(self) -> Optional[Class]:
        """Gets the generic type definition that defined this class"""
        return self.type_def

    def make_generic_type(
        self,
        index: Tuple[Class, ...],
        generic_types: GenericTypesDict,
    ) -> Class:
        instantiations = generic_types.get(self)
        if instantiations is not None:
            instance = instantiations.get(index)
            if instance is not None:
                return instance
        else:
            generic_types[self] = instantiations = {}

        type_args = index
        type_name = GenericTypeName(
            self.type_name.module, self.type_name.name, type_args
        )
        generic_bases: List[Optional[Class]] = [
            (
                base.make_generic_type(index, generic_types)
                if base.contains_generic_parameters
                else base
            )
            for base in self.bases
        ]
        bases: List[Class] = [base for base in generic_bases if base is not None]
        InstanceType = type(self.instance)
        instance = InstanceType.__new__(InstanceType)
        instance.__dict__.update(self.instance.__dict__)
        concrete = type(self)(
            type_name,
            bases,
            instance,
            self.klass,
            {},
            is_exact=self.is_exact,
            type_def=self,
        )

        instance.klass = concrete

        instantiations[index] = concrete
        concrete.members.update(
            {
                k: v.make_generic(concrete, type_name, generic_types)
                for k, v in self.members.items()
            }
        )
        return concrete

    def bind_generics(
        self,
        name: GenericTypeName,
        generic_types: Dict[Class, Dict[Tuple[Class, ...], Class]],
    ) -> Class:
        if self.contains_generic_parameters:
            type_args = [
                arg for arg in self.type_name.args if isinstance(arg, GenericParameter)
            ]
            assert len(type_args) == len(self.type_name.args)
            # map the generic type parameters for the type to the parameters provided
            bind_args = tuple(name.args[arg.index] for arg in type_args)
            # We don't yet support generic methods, so all of the generic parameters are coming from the
            # type definition.

            return self.make_generic_type(bind_args, generic_types)

        return self


class GenericParameter(Class):
    def __init__(self, name: str, index: int) -> None:
        super().__init__(TypeName("", name), [], None, None, {})
        self.index = index

    @property
    def name(self) -> str:
        return self.type_name.name

    @property
    def is_generic_parameter(self) -> bool:
        return True

    def bind_generics(
        self,
        name: GenericTypeName,
        generic_types: Dict[Class, Dict[Tuple[Class, ...], Class]],
    ) -> Class:
        return name.args[self.index]


class CType(Class):
    """base class for primitives that aren't heap allocated"""

    suppress_exact = True

    def __init__(
        self,
        type_name: TypeName,
        bases: Optional[List[Class]] = None,
        instance: Optional[CInstance[Class]] = None,
        klass: Optional[Class] = None,
        members: Optional[Dict[str, Value]] = None,
        is_exact: bool = True,
        pytype: Optional[Type[object]] = None,
    ) -> None:
        super().__init__(type_name, bases, instance, klass, members, is_exact, pytype)

    @property
    def can_be_narrowed(self) -> bool:
        return False

    def bind_call(
        self, node: ast.Call, visitor: TypeBinder, type_ctx: Optional[Class]
    ) -> NarrowingEffect:
        """
        Almost the same as the base class method, but this allows args to be primitives
        so we can write something like (explicit conversions):
        x = int32(int8(5))
        """
        visitor.set_type(node, self.instance)
        for arg in node.args:
            visitor.visit(arg, self.instance)
        return NO_EFFECT


class DynamicClass(Class):
    instance: DynamicInstance

    def __init__(self) -> None:
        super().__init__(
            # any references to dynamic at runtime are object
            TypeName("builtins", "object"),
            bases=[OBJECT_TYPE],
            instance=DynamicInstance(self),
        )

    @property
    def qualname(self) -> str:
        return "dynamic"

    def can_assign_from(self, src: Class) -> bool:
        # No automatic boxing to the dynamic type
        return not isinstance(src, CType)


class DynamicInstance(Object[DynamicClass]):
    def __init__(self, klass: DynamicClass) -> None:
        super().__init__(klass)

    def bind_constant(self, node: ast.Constant, visitor: TypeBinder) -> None:
        n = node.value
        inst = CONSTANT_TYPES.get(type(n), DYNAMIC_TYPE.instance)
        visitor.set_type(node, inst)

    def emit_binop(self, node: ast.BinOp, code_gen: Static38CodeGenerator) -> None:
        if maybe_emit_sequence_repeat(node, code_gen):
            return
        code_gen.defaultVisit(node)


class NoneType(Class):
    suppress_exact = True

    def __init__(self) -> None:
        super().__init__(
            TypeName("builtins", "None"),
            [OBJECT_TYPE],
            NoneInstance(self),
            is_exact=True,
        )


UNARY_SYMBOLS: Mapping[Type[ast.unaryop], str] = {
    ast.UAdd: "+",
    ast.USub: "-",
    ast.Invert: "~",
}


class NoneInstance(Object[NoneType]):
    def bind_attr(
        self, node: ast.Attribute, visitor: TypeBinder, type_ctx: Optional[Class]
    ) -> None:
        raise visitor.syntax_error(
            f"'NoneType' object has no attribute '{node.attr}'", node
        )

    def bind_call(
        self, node: ast.Call, visitor: TypeBinder, type_ctx: Optional[Class]
    ) -> NarrowingEffect:
        raise visitor.syntax_error("'NoneType' object is not callable", node)

    def bind_subscr(
        self, node: ast.Subscript, type: Value, visitor: TypeBinder
    ) -> None:
        raise visitor.syntax_error("'NoneType' object is not subscriptable", node)

    def bind_unaryop(
        self, node: ast.UnaryOp, visitor: TypeBinder, type_ctx: Optional[Class]
    ) -> None:
        if not isinstance(node.op, ast.Not):
            raise visitor.syntax_error(
                f"bad operand type for unary {UNARY_SYMBOLS[type(node.op)]}: 'NoneType'",
                node,
            )
        visitor.set_type(node, BOOL_TYPE.instance)

    def bind_binop(
        self, node: ast.BinOp, visitor: TypeBinder, type_ctx: Optional[Class]
    ) -> bool:
        # support `None | int` as a union type; None is special in that it is
        # not a type but can be used synonymously with NoneType for typing.
        if isinstance(node.op, ast.BitOr):
            return self.klass.bind_binop(node, visitor, type_ctx)
        else:
            return super().bind_binop(node, visitor, type_ctx)

    def bind_compare(
        self,
        node: ast.Compare,
        left: expr,
        op: cmpop,
        right: expr,
        visitor: TypeBinder,
        type_ctx: Optional[Class],
    ) -> bool:
        if isinstance(op, (ast.Eq, ast.NotEq, ast.Is, ast.IsNot)):
            return super().bind_compare(node, left, op, right, visitor, type_ctx)
        ltype = visitor.get_type(left)
        rtype = visitor.get_type(right)
        raise visitor.syntax_error(
            f"'{CMPOP_SIGILS[type(op)]}' not supported between '{ltype.name}' and '{rtype.name}'",
            node,
        )

    def bind_reverse_compare(
        self,
        node: ast.Compare,
        left: expr,
        op: cmpop,
        right: expr,
        visitor: TypeBinder,
        type_ctx: Optional[Class],
    ) -> bool:
        if isinstance(op, (ast.Eq, ast.NotEq, ast.Is, ast.IsNot)):
            return super().bind_reverse_compare(
                node, left, op, right, visitor, type_ctx
            )
        ltype = visitor.get_type(left)
        rtype = visitor.get_type(right)
        raise visitor.syntax_error(
            f"'{CMPOP_SIGILS[type(op)]}' not supported between '{ltype.name}' and '{rtype.name}'",
            node,
        )


# https://www.python.org/download/releases/2.3/mro/
def _merge(seqs: Iterable[List[Class]]) -> List[Class]:
    res = []
    i = 0
    while True:
        nonemptyseqs = [seq for seq in seqs if seq]
        if not nonemptyseqs:
            return res
        i += 1
        cand = None
        for seq in nonemptyseqs:  # find merge candidates among seq heads
            cand = seq[0]
            nothead = [s for s in nonemptyseqs if cand in s[1:]]
            if nothead:
                cand = None  # reject candidate
            else:
                break
        if not cand:
            types = {seq[0]: None for seq in nonemptyseqs}
            raise SyntaxError(
                "Cannot create a consistent method resolution order (MRO) for bases: "
                + ", ".join(t.name for t in types)
            )
        res.append(cand)
        for seq in nonemptyseqs:  # remove cand
            if seq[0] == cand:
                del seq[0]


def _mro(C: Class) -> List[Class]:
    "Compute the class precedence list (mro) according to C3"
    return _merge([[C]] + list(map(_mro, C.bases)) + [list(C.bases)])


class Parameter:
    def __init__(
        self,
        name: str,
        idx: int,
        type_ref: TypeRef,
        has_default: bool,
        default_val: object,
        is_kwonly: bool,
    ) -> None:
        self.name = name
        self.type_ref = type_ref
        self.index = idx
        self.has_default = has_default
        self.default_val = default_val
        self.is_kwonly = is_kwonly

    def __repr__(self) -> str:
        return (
            f"<Parameter name={self.name}, ref={self.type_ref}, "
            f"index={self.index}, has_default={self.has_default}>"
        )

    def bind_generics(
        self,
        name: GenericTypeName,
        generic_types: Dict[Class, Dict[Tuple[Class, ...], Class]],
    ) -> Parameter:
        klass = self.type_ref.resolved().bind_generics(name, generic_types)
        if klass is not self.type_ref.resolved():
            return Parameter(
                self.name,
                self.index,
                ResolvedTypeRef(klass),
                self.has_default,
                self.default_val,
                self.is_kwonly,
            )

        return self


def is_subsequence(a: Iterable[object], b: Iterable[object]) -> bool:
    # for loops go brrrr :)
    # https://ericlippert.com/2020/03/27/new-grad-vs-senior-dev/
    itr = iter(a)
    for each in b:
        if each not in itr:
            return False
    return True


class ArgMapping:
    def __init__(
        self,
        callable: Callable[TClass],
        call: ast.Call,
        self_arg: Optional[ast.expr],
    ) -> None:
        self.callable = callable
        self.call = call
        pos_args: List[ast.expr] = []
        if self_arg is not None:
            pos_args.append(self_arg)
        pos_args.extend(call.args)
        self.args: List[ast.expr] = pos_args

        self.kwargs: List[Tuple[Optional[str], ast.expr]] = [
            (kwarg.arg, kwarg.value) for kwarg in call.keywords
        ]
        self.self_arg = self_arg
        self.emitters: List[ArgEmitter] = []
        self.nvariadic = 0
        self.nseen = 0
        self.spills: Dict[int, SpillArg] = {}

    def bind_args(self, visitor: TypeBinder) -> None:
        # TODO: handle duplicate args and other weird stuff a-la
        # https://fburl.com/diffusion/q6tpinw8

        # Process provided position arguments to expected parameters
        for idx, (param, arg) in enumerate(zip(self.callable.args, self.args)):
            if param.is_kwonly:
                raise visitor.syntax_error(
                    f"{self.callable.qualname} takes {idx} positional args but "
                    f"{len(self.args)} {'was' if len(self.args) == 1 else 'were'} given",
                    self.call,
                )
            elif isinstance(arg, Starred):
                # Skip type verification here, f(a, b, *something)
                # TODO: add support for this by implementing type constrained tuples
                self.nvariadic += 1
                star_params = self.callable.args[idx:]
                self.emitters.append(StarredArg(arg.value, star_params))
                self.nseen = len(self.callable.args)
                for arg in self.args[idx:]:
                    visitor.visit(arg)
                break

            resolved_type = self.visit_arg(visitor, param, arg, "positional")
            self.emitters.append(PositionArg(arg, resolved_type))
            self.nseen += 1

        self.bind_kwargs(visitor)

        for argname, argvalue in self.kwargs:
            if argname is None:
                visitor.visit(argvalue)
                continue

            if argname not in self.callable.args_by_name:
                raise visitor.syntax_error(
                    f"Given argument {argname} "
                    f"does not exist in the definition of {self.callable.qualname}",
                    self.call,
                )

        # nseen must equal number of defined args if no variadic args are used
        if self.nvariadic == 0 and (self.nseen != len(self.callable.args)):
            raise visitor.syntax_error(
                f"Mismatched number of args for {self.callable.name}. "
                f"Expected {len(self.callable.args)}, got {self.nseen}",
                self.call,
            )

    def bind_kwargs(self, visitor: TypeBinder) -> None:
        spill_start = len(self.emitters)
        seen_variadic = False
        # Process unhandled arguments which can be populated via defaults,
        # keyword arguments, or **mapping.
        cur_kw_arg = 0
        for idx in range(self.nseen, len(self.callable.args)):
            param = self.callable.args[idx]
            name = param.name
            if (
                cur_kw_arg is not None
                and cur_kw_arg < len(self.kwargs)
                and self.kwargs[cur_kw_arg][0] == name
            ):
                # keyword arg hit, with the keyword arguments still in order...
                arg = self.kwargs[cur_kw_arg][1]
                resolved_type = self.visit_arg(visitor, param, arg, "keyword")
                cur_kw_arg += 1

                self.emitters.append(KeywordArg(arg, resolved_type))
                self.nseen += 1
                continue

            variadic_idx = None
            for candidate_kw in range(len(self.kwargs)):
                if name == self.kwargs[candidate_kw][0]:
                    arg = self.kwargs[candidate_kw][1]

                    tmp_name = f"{_TMP_VAR_PREFIX}{name}"
                    self.spills[candidate_kw] = SpillArg(arg, tmp_name)

                    if cur_kw_arg is not None:
                        cur_kw_arg = None
                        spill_start = len(self.emitters)

                    resolved_type = self.visit_arg(visitor, param, arg, "keyword")
                    self.emitters.append(SpilledKeywordArg(tmp_name, resolved_type))
                    break
                elif self.kwargs[candidate_kw][0] == None:
                    variadic_idx = candidate_kw
            else:
                if variadic_idx is not None:
                    # We have a f(**something), if the arg is unavailable, we
                    # load it from the mapping
                    if variadic_idx not in self.spills:
                        self.spills[variadic_idx] = SpillArg(
                            self.kwargs[variadic_idx][1], f"{_TMP_VAR_PREFIX}**"
                        )

                        if cur_kw_arg is not None:
                            cur_kw_arg = None
                            spill_start = len(self.emitters)

                    self.emitters.append(
                        KeywordMappingArg(param, f"{_TMP_VAR_PREFIX}**")
                    )
                elif param.has_default:
                    self.emitters.append(DefaultArg(param.default_val))
                else:
                    # It's an error if this arg did not have a default value in the definition
                    raise visitor.syntax_error(
                        f"Function {self.callable.qualname} expects a value for "
                        f"argument {param.name}",
                        self.call,
                    )

            self.nseen += 1

        if self.spills:
            self.emitters[spill_start:spill_start] = [
                x[1] for x in sorted(self.spills.items())
            ]

    def visit_arg(
        self, visitor: TypeBinder, param: Parameter, arg: expr, arg_style: str
    ) -> Class:
        resolved_type = param.type_ref.resolved()
        exc = None
        try:
            visitor.visit(arg, resolved_type.instance if resolved_type else None)
        except TypedSyntaxError as e:
            # We may report a better error message below...
            exc = e
        visitor.check_can_assign_from(
            resolved_type,
            visitor.get_type(arg).klass,
            arg,
            f"{arg_style} argument type mismatch",
        )
        if exc is not None:
            raise exc
        return resolved_type


class ArgEmitter:
    def __init__(self, argument: expr, type: Class) -> None:
        self.argument = argument

        self.type = type

    def emit(self, node: Call, code_gen: Static38CodeGenerator) -> None:
        pass


class PositionArg(ArgEmitter):
    def emit(self, node: Call, code_gen: Static38CodeGenerator) -> None:
        arg_type = code_gen.get_type(self.argument)
        code_gen.visit(self.argument)

        code_gen.emit_type_check(
            self.type,
            arg_type.klass,
            node,
        )

    def __repr__(self) -> str:
        return f"PositionArg({to_expr(self.argument)}, {self.type})"


class StarredArg(ArgEmitter):
    def __init__(self, argument: expr, params: List[Parameter]) -> None:

        self.argument = argument
        self.params = params

    def emit(self, node: Call, code_gen: Static38CodeGenerator) -> None:
        code_gen.visit(self.argument)
        for idx, param in enumerate(self.params):
            code_gen.emit("LOAD_ITERABLE_ARG", idx)

            if (
                param.type_ref.resolved() is not None
                and param.type_ref.resolved() is not DYNAMIC
            ):
                code_gen.emit("ROT_TWO")
                code_gen.emit("CAST", param.type_ref.resolved().type_descr)
                code_gen.emit("ROT_TWO")

        # Remove the tuple from TOS
        code_gen.emit("POP_TOP")


class SpillArg(ArgEmitter):
    def __init__(self, argument: expr, temporary: str) -> None:
        self.argument = argument
        self.temporary = temporary

    def emit(self, node: Call, code_gen: Static38CodeGenerator) -> None:
        code_gen.visit(self.argument)
        code_gen.emit("STORE_FAST", self.temporary)

    def __repr__(self) -> str:
        return f"SpillArg(..., {self.temporary})"


class SpilledKeywordArg(ArgEmitter):
    def __init__(self, temporary: str, type: Class) -> None:
        self.temporary = temporary
        self.type = type

    def emit(self, node: Call, code_gen: Static38CodeGenerator) -> None:
        code_gen.emit("LOAD_FAST", self.temporary)
        code_gen.emit_type_check(
            self.type,
            DYNAMIC_TYPE,
            node,
        )

    def __repr__(self) -> str:
        return f"SpilledKeywordArg({self.temporary})"


class KeywordArg(ArgEmitter):
    def __init__(self, argument: expr, type: Class) -> None:
        self.argument = argument
        self.type = type

    def emit(self, node: Call, code_gen: Static38CodeGenerator) -> None:
        code_gen.visit(self.argument)
        code_gen.emit_type_check(
            self.type,
            code_gen.get_type(self.argument).klass,
            node,
        )


class KeywordMappingArg(ArgEmitter):
    def __init__(self, param: Parameter, variadic: str) -> None:
        self.param = param

        self.variadic = variadic

    def emit(self, node: Call, code_gen: Static38CodeGenerator) -> None:
        if self.param.has_default:
            code_gen.emit("LOAD_CONST", self.param.default_val)
        code_gen.emit("LOAD_FAST", self.variadic)
        code_gen.emit("LOAD_CONST", self.param.name)
        if self.param.has_default:
            code_gen.emit("LOAD_MAPPING_ARG", 3)
        else:
            code_gen.emit("LOAD_MAPPING_ARG", 2)
        code_gen.emit_type_check(
            self.param.type_ref.resolved() or DYNAMIC_TYPE, DYNAMIC_TYPE, node
        )


class DefaultArg(ArgEmitter):
    def __init__(self, value: object) -> None:
        self.value = value

    def emit(self, node: Call, code_gen: Static38CodeGenerator) -> None:
        code_gen.emit("LOAD_CONST", self.value)


class Callable(Object[TClass]):
    def __init__(
        self,
        klass: Class,
        func_name: str,
        module_name: str,
        args: List[Parameter],
        args_by_name: Dict[str, Parameter],
        num_required_args: int,
        vararg: Optional[Parameter],
        kwarg: Optional[Parameter],
        return_type: TypeRef,
    ) -> None:
        super().__init__(klass)
        self.func_name = func_name
        self.module_name = module_name
        self.container_type: Optional[Class] = None
        self.args = args
        self.args_by_name = args_by_name
        self.num_required_args = num_required_args
        self.has_vararg: bool = vararg is not None
        self.has_kwarg: bool = kwarg is not None
        self.return_type = return_type
        self.is_final = False

    @property
    def qualname(self) -> str:
        cont = self.container_type
        if cont:
            return f"{cont.qualname}.{self.func_name}"
        return f"{self.module_name}.{self.func_name}"

    @property
    def type_descr(self) -> TypeDescr:
        cont = self.container_type
        if cont:
            return cont.type_descr + (self.func_name,)
        return (self.module_name, self.func_name)

    def set_container_type(self, klass: Optional[Class]) -> None:
        self.container_type = klass

    def bind_call(
        self, node: ast.Call, visitor: TypeBinder, type_ctx: Optional[Class]
    ) -> NarrowingEffect:
        # Careful adding logic here, MethodType.bind_call() will bypass it
        return self.bind_call_self(node, visitor, type_ctx)

    def bind_call_self(
        self,
        node: ast.Call,
        visitor: TypeBinder,
        type_ctx: Optional[Class],
        self_expr: Optional[ast.expr] = None,
    ) -> NarrowingEffect:
        if self.has_vararg or self.has_kwarg:
            return super().bind_call(node, visitor, type_ctx)

        if type_ctx is not None:
            visitor.check_can_assign_from(
                type_ctx.klass,
                self.return_type.resolved(),
                node,
                "is an invalid return type, expected",
            )

        arg_mapping = ArgMapping(self, node, self_expr)
        arg_mapping.bind_args(visitor)

        visitor.set_type(node, self.return_type.resolved().instance)
        visitor.set_node_data(node, ArgMapping, arg_mapping)
        return NO_EFFECT

    def _emit_kwarg_temps(
        self, keywords: List[ast.keyword], code_gen: Static38CodeGenerator
    ) -> Dict[str, str]:
        temporaries = {}
        for each in keywords:
            name = each.arg
            if name is not None:
                code_gen.visit(each.value)
                temp_var_name = f"{_TMP_VAR_PREFIX}{name}"
                code_gen.emit("STORE_FAST", temp_var_name)
                temporaries[name] = temp_var_name
        return temporaries

    def _find_provided_kwargs(
        self, node: ast.Call
    ) -> Tuple[Dict[int, int], Optional[int]]:
        # This is a mapping of indices from index in the function definition --> node.keywords
        provided_kwargs: Dict[int, int] = {}
        # Index of `**something` in the call
        variadic_idx: Optional[int] = None
        for idx, argument in enumerate(node.keywords):
            name = argument.arg
            if name is not None:
                provided_kwargs[self.args_by_name[name].index] = idx
            else:
                # Because of the constraints above, we will only ever reach here once
                variadic_idx = idx
        return provided_kwargs, variadic_idx

    def can_call_self(self, node: ast.Call, has_self: bool) -> bool:
        if self.has_vararg or self.has_kwarg:
            return False

        has_default_args = self.num_required_args < len(self.args)
        has_star_args = False
        for a in node.args:
            if isinstance(a, ast.Starred):
                if has_star_args:
                    # We don't support f(*a, *b)
                    return False
                has_star_args = True
            elif has_star_args:
                # We don't support f(*a, b)
                return False

        num_star_args = [isinstance(a, ast.Starred) for a in node.args].count(True)
        num_dstar_args = [(a.arg is None) for a in node.keywords].count(True)
        num_kwonly = len([arg for arg in self.args if arg.is_kwonly])

        start = 1 if has_self else 0
        for arg in self.args[start + len(node.args) :]:
            if arg.has_default and isinstance(arg.default_val, ast.expr):
                for kw_arg in node.keywords:
                    if kw_arg.arg == arg.name:
                        break
                else:
                    return False
        if (
            # We don't support f(**a, **b)
            num_dstar_args > 1
            # We don't support f(1, 2, *a) if f has any default arg values
            or (has_default_args and has_star_args)
            or num_kwonly
        ):
            return False

        return True

    def emit_call_self(self, node: ast.Call, code_gen: Static38CodeGenerator) -> None:
        arg_mapping: ArgMapping = code_gen.get_node_data(node, ArgMapping)
        for emitter in arg_mapping.emitters:
            emitter.emit(node, code_gen)
        self_expr = arg_mapping.self_arg
        if self_expr is not None and not code_gen.get_type(self_expr).klass.is_exact:
            code_gen.emit_invoke_method(self.type_descr, len(self.args) - 1)
        else:
            code_gen.emit("EXTENDED_ARG", 0)
            code_gen.emit("INVOKE_FUNCTION", (self.type_descr, len(self.args)))
        return


class ContainerTypeRef(TypeRef):
    def __init__(self, func: Function) -> None:
        self.func = func

    def resolved(self, is_declaration: bool = False) -> Class:
        res = self.func.container_type
        if res is None:
            return DYNAMIC_TYPE
        return res


class InlineRewriter(ASTRewriter):
    def __init__(self, replacements: Dict[str, ast.expr]) -> None:
        super().__init__()
        self.replacements = replacements

    def visit(
        self, node: Union[TAst, Sequence[AST]], *args: object
    ) -> Union[AST, Sequence[AST]]:
        res = super().visit(node, *args)
        if res is node:
            if isinstance(node, AST):
                return self.clone_node(node)

            return list(node)

        return res

    def visitName(self, node: ast.Name) -> AST:
        res = self.replacements.get(node.id)
        if res is None:
            return self.clone_node(node)

        return res


class InlinedCall:
    def __init__(
        self,
        expr: ast.expr,
        replacements: Dict[ast.expr, ast.expr],
        spills: Dict[str, ast.expr],
    ) -> None:
        self.expr = expr
        self.replacements = replacements
        self.spills = spills


class Function(Callable[Class]):
    def __init__(
        self,
        node: Union[AsyncFunctionDef, FunctionDef],
        module: ModuleTable,
        ret_type: TypeRef,
    ) -> None:
        super().__init__(
            FUNCTION_TYPE,
            node.name,
            module.name,
            [],
            {},
            0,
            None,
            None,
            ret_type,
        )
        self.node = node
        self.module = module
        self.process_args(module)
        self.inline = False
        self.donotcompile = False

    @property
    def name(self) -> str:
        return f"function {self.qualname}"

    def bind_call(
        self, node: ast.Call, visitor: TypeBinder, type_ctx: Optional[Class]
    ) -> NarrowingEffect:
        res = super().bind_call(node, visitor, type_ctx)
        if self.inline and visitor.optimize == 2:
            assert isinstance(self.node.body[0], ast.Return)

            return self.bind_inline_call(node, visitor, type_ctx) or res

        return res

    def emit_call(self, node: ast.Call, code_gen: Static38CodeGenerator) -> None:
        if not self.can_call_self(node, False):
            return super().emit_call(node, code_gen)

        if self.inline and code_gen.optimization_lvl == 2:
            return self.emit_inline_call(node, code_gen)

        return self.emit_call_self(node, code_gen)

    def bind_inline_call(
        self, node: ast.Call, visitor: TypeBinder, type_ctx: Optional[Class]
    ) -> Optional[NarrowingEffect]:
        args = visitor.get_node_data(node, ArgMapping)

        arg_replacements = {}
        spills = {}

        if visitor.inline_depth > 20:
            visitor.set_node_data(node, Optional[InlinedCall], None)
            return None

        visitor.inline_depth += 1
        for idx, arg in enumerate(args.emitters):
            name = self.node.args.args[idx].arg

            if isinstance(arg, DefaultArg):
                arg_replacements[name] = ast.Constant(arg.value)
                continue
            elif not isinstance(arg, (PositionArg, KeywordArg)):
                # We don't support complicated calls to inline functions
                visitor.set_node_data(node, Optional[InlinedCall], None)
                return None

            if (
                isinstance(arg.argument, ast.Constant)
                or visitor.get_final_literal(arg.argument) is not None
            ):
                arg_replacements[name] = arg.argument
                continue

            # store to a temporary...
            tmp_name = f"{_TMP_VAR_PREFIX}{visitor.inline_depth}{name}"
            cur_scope = visitor.symbols.scopes[visitor.scope]
            cur_scope.add_def(tmp_name)
            spills[tmp_name] = arg.argument
            replacement = ast.Name(tmp_name, ast.Load())
            visitor.assign_value(replacement, visitor.get_type(arg.argument))

            arg_replacements[name] = replacement

        # re-write node body with replacements...
        return_stmt = self.node.body[0]
        assert isinstance(return_stmt, Return)
        ret_value = return_stmt.value
        if ret_value is not None:
            new_node = InlineRewriter(arg_replacements).visit(ret_value)
        else:
            new_node = ast.Constant(None)
        new_node = AstOptimizer().visit(new_node)

        inlined_call = InlinedCall(new_node, arg_replacements, spills)
        visitor.visit(new_node)
        visitor.set_node_data(node, Optional[InlinedCall], inlined_call)

        visitor.inline_depth -= 1

    def emit_inline_call(self, node: ast.Call, code_gen: Static38CodeGenerator) -> None:
        assert isinstance(self.node.body[0], ast.Return)
        inlined_call = code_gen.get_node_data(node, Optional[InlinedCall])
        if inlined_call is None:
            return self.emit_call_self(node, code_gen)

        for name, arg in inlined_call.spills.items():
            code_gen.visit(arg)
            code_gen.emit("STORE_FAST", name)

        code_gen.visit(inlined_call.expr)

    def bind_descr_get(
        self,
        node: ast.Attribute,
        inst: Optional[Object[TClassInv]],
        ctx: TClassInv,
        visitor: TypeBinder,
        type_ctx: Optional[Class],
    ) -> None:
        if inst is None:
            visitor.set_type(node, self)
        else:
            visitor.set_type(node, MethodType(ctx.type_name, self.node, node, self))

    def register_arg(
        self,
        name: str,
        idx: int,
        ref: TypeRef,
        has_default: bool,
        default_val: object,
        is_kwonly: bool,
    ) -> None:
        parameter = Parameter(name, idx, ref, has_default, default_val, is_kwonly)
        self.args.append(parameter)
        self.args_by_name[name] = parameter
        if not has_default:
            self.num_required_args += 1

    def process_args(
        self: Function,
        module: ModuleTable,
    ) -> None:
        """
        Register type-refs for each function argument, assume DYNAMIC if annotation is missing.
        """
        arguments = self.node.args
        nrequired = len(arguments.args) - len(arguments.defaults)
        no_defaults = cast(List[Optional[ast.expr]], [None] * nrequired)
        defaults = no_defaults + cast(List[Optional[ast.expr]], arguments.defaults)
        idx = 0
        for idx, (argument, default) in enumerate(zip(arguments.args, defaults)):
            annotation = argument.annotation
            default_val = None
            has_default = False
            if default is not None:
                has_default = True
                default_val = get_default_value(default)

            if annotation:
                ref = TypeRef(module, annotation)
            elif idx == 0:
                ref = ContainerTypeRef(self)
            else:
                ref = ResolvedTypeRef(DYNAMIC_TYPE)
            self.register_arg(argument.arg, idx, ref, has_default, default_val, False)

        base_idx = idx

        vararg = arguments.vararg
        if vararg:
            base_idx += 1
            self.has_vararg = True

        for argument, default in zip(arguments.kwonlyargs, arguments.kw_defaults):
            annotation = argument.annotation
            default_val = None
            has_default = default is not None
            if default is not None:
                default_val = get_default_value(default)
            if annotation:
                ref = TypeRef(module, annotation)
            else:
                ref = ResolvedTypeRef(DYNAMIC_TYPE)
            base_idx += 1
            self.register_arg(
                argument.arg, base_idx, ref, has_default, default_val, True
            )

        kwarg = arguments.kwarg
        if kwarg:
            self.has_kwarg = True

    def __repr__(self) -> str:
        return f"<{self.name} '{self.name}' instance, args={self.args}>"


class MethodType(Object[Class]):
    def __init__(
        self,
        bound_type_name: TypeName,
        node: Union[AsyncFunctionDef, FunctionDef],
        target: ast.Attribute,
        function: Function,
    ) -> None:
        super().__init__(METHOD_TYPE)
        # TODO currently this type (the type the bound method was accessed
        # from) is unused, and we just end up deferring to the type where the
        # function was defined. This is fine until we want to fully support a
        # method defined in one class being also referenced as a method in
        # another class.
        self.bound_type_name = bound_type_name
        self.node = node
        self.target = target
        self.function = function

    @property
    def name(self) -> str:
        return "method " + self.function.qualname

    def bind_call(
        self, node: ast.Call, visitor: TypeBinder, type_ctx: Optional[Class]
    ) -> NarrowingEffect:
        result = self.function.bind_call_self(
            node, visitor, type_ctx, self.target.value
        )
        self.check_args_for_primitives(node, visitor)
        return result

    def emit_call(self, node: ast.Call, code_gen: Static38CodeGenerator) -> None:
        if not self.function.can_call_self(node, True):
            return super().emit_call(node, code_gen)

        code_gen.update_lineno(node)

        self.function.emit_call_self(node, code_gen)


class StaticMethod(Object[Class]):
    def __init__(
        self,
        function: Function,
    ) -> None:
        super().__init__(STATIC_METHOD_TYPE)
        self.function = function

    @property
    def name(self) -> str:
        return "staticmethod " + self.function.qualname

    @property
    def func_name(self) -> str:
        return self.function.func_name

    @property
    def is_final(self) -> bool:
        return self.function.is_final

    def set_container_type(self, container_type: Optional[Class]) -> None:
        self.function.set_container_type(container_type)

    def bind_descr_get(
        self,
        node: ast.Attribute,
        inst: Optional[Object[TClassInv]],
        ctx: TClassInv,
        visitor: TypeBinder,
        type_ctx: Optional[Class],
    ) -> None:
        visitor.set_type(node, self.function)


class TypingFinalDecorator(Class):
    def bind_decorate_function(
        self, visitor: DeclarationVisitor, fn: Function | StaticMethod
    ) -> Value:
        if isinstance(fn, StaticMethod):
            fn.function.is_final = True
        else:
            fn.is_final = True
        return fn

    def bind_decorate_class(self, klass: Class) -> Class:
        klass.is_final = True
        return klass


class AllowWeakrefsDecorator(Class):
    def bind_decorate_class(self, klass: Class) -> Class:
        klass.allow_weakrefs = True
        return klass


class DynamicReturnDecorator(Class):
    def bind_decorate_function(
        self, visitor: DeclarationVisitor, fn: Function | StaticMethod
    ) -> Value:
        real_fn = fn.function if isinstance(fn, StaticMethod) else fn
        real_fn.return_type = ResolvedTypeRef(DYNAMIC_TYPE)
        real_fn.module.dynamic_returns.add(real_fn.node.args)
        return fn


class StaticMethodDecorator(Class):
    def bind_decorate_function(
        self, visitor: DeclarationVisitor, fn: Function | StaticMethod
    ) -> Value:
        if isinstance(fn, StaticMethod):
            # no-op
            return fn
        return StaticMethod(fn)


class InlineFunctionDecorator(Class):
    def bind_decorate_function(
        self, visitor: DeclarationVisitor, fn: Function | StaticMethod
    ) -> Value:
        real_fn = fn.function if isinstance(fn, StaticMethod) else fn
        if not isinstance(real_fn.node.body[0], ast.Return):
            raise visitor.syntax_error(
                "@inline only supported on functions with simple return", real_fn.node
            )

        real_fn.inline = True
        return fn


class DoNotCompileDecorator(Class):
    def bind_decorate_function(
        self, visitor: DeclarationVisitor, fn: Function | StaticMethod
    ) -> Optional[Value]:
        real_fn = fn.function if isinstance(fn, StaticMethod) else fn
        real_fn.donotcompile = True
        return fn

    def bind_decorate_class(self, klass: Class) -> Class:
        klass.donotcompile = True
        return klass


class BuiltinFunction(Callable[Class]):
    def __init__(
        self,
        func_name: str,
        module: str,
        args: Optional[Tuple[Parameter, ...]] = None,
        return_type: Optional[TypeRef] = None,
    ) -> None:
        assert isinstance(return_type, (TypeRef, type(None)))
        super().__init__(
            BUILTIN_METHOD_DESC_TYPE,
            func_name,
            module,
            args,
            {},
            0,
            None,
            None,
            return_type or ResolvedTypeRef(DYNAMIC_TYPE),
        )

    def emit_call(self, node: ast.Call, code_gen: Static38CodeGenerator) -> None:
        if node.keywords or (
            self.args is not None and not self.can_call_self(node, True)
        ):
            return super().emit_call(node, code_gen)

        code_gen.update_lineno(node)
        self.emit_call_self(node, code_gen)


class BuiltinMethodDescriptor(Callable[Class]):
    def __init__(
        self,
        func_name: str,
        container_type: Class,
        args: Optional[Tuple[Parameter, ...]] = None,
        return_type: Optional[TypeRef] = None,
    ) -> None:
        assert isinstance(return_type, (TypeRef, type(None)))
        super().__init__(
            BUILTIN_METHOD_DESC_TYPE,
            func_name,
            container_type.type_name.module,
            args,
            {},
            0,
            None,
            None,
            return_type or ResolvedTypeRef(DYNAMIC_TYPE),
        )
        self.set_container_type(container_type)

    def bind_call_self(
        self,
        node: ast.Call,
        visitor: TypeBinder,
        type_ctx: Optional[Class],
        self_expr: Optional[expr] = None,
    ) -> NarrowingEffect:
        if self.args is not None:
            return super().bind_call_self(node, visitor, type_ctx, self_expr)
        elif node.keywords:
            return super().bind_call(node, visitor, type_ctx)

        visitor.set_type(node, DYNAMIC)
        for arg in node.args:
            visitor.visit(arg)

        return NO_EFFECT

    def bind_descr_get(
        self,
        node: ast.Attribute,
        inst: Optional[Object[TClassInv]],
        ctx: TClassInv,
        visitor: TypeBinder,
        type_ctx: Optional[Class],
    ) -> None:
        if inst is None:
            visitor.set_type(node, self)
        else:
            visitor.set_type(node, BuiltinMethod(self, node))

    def make_generic(
        self, new_type: Class, name: GenericTypeName, generic_types: GenericTypesDict
    ) -> Value:
        cur_args = self.args
        cur_ret_type = self.return_type
        if cur_args is not None and cur_ret_type is not None:
            new_args = tuple(arg.bind_generics(name, generic_types) for arg in cur_args)
            new_ret_type = cur_ret_type.resolved().bind_generics(name, generic_types)
            return BuiltinMethodDescriptor(
                self.func_name,
                new_type,
                new_args,
                ResolvedTypeRef(new_ret_type),
            )
        else:
            return BuiltinMethodDescriptor(self.func_name, new_type)


class BuiltinMethod(Callable[Class]):
    def __init__(self, desc: BuiltinMethodDescriptor, target: ast.Attribute) -> None:
        super().__init__(
            BUILTIN_METHOD_TYPE,
            desc.func_name,
            desc.module_name,
            desc.args,
            {},
            0,
            None,
            None,
            desc.return_type,
        )
        self.desc = desc
        self.target = target
        self.set_container_type(desc.container_type)

    @property
    def name(self) -> str:
        return self.qualname

    def bind_call(
        self, node: ast.Call, visitor: TypeBinder, type_ctx: Optional[Class]
    ) -> NarrowingEffect:
        if self.args:
            return super().bind_call_self(node, visitor, type_ctx, self.target.value)
        if node.keywords:
            return Object.bind_call(self, node, visitor, type_ctx)

        visitor.set_type(node, self.return_type.resolved().instance)
        visitor.visit(self.target.value)
        for arg in node.args:
            visitor.visit(arg)
        self.check_args_for_primitives(node, visitor)
        return NO_EFFECT

    def emit_call(self, node: ast.Call, code_gen: Static38CodeGenerator) -> None:
        if node.keywords or (
            self.args is not None and not self.desc.can_call_self(node, True)
        ):
            return super().emit_call(node, code_gen)

        code_gen.update_lineno(node)

        if self.args is not None:
            self.desc.emit_call_self(node, code_gen)
        else:
            # Untyped method, we can still do an INVOKE_METHOD

            code_gen.visit(self.target.value)

            code_gen.update_lineno(node)
            for arg in node.args:
                code_gen.visit(arg)

            if code_gen.get_type(self.target.value).klass.is_exact:
                code_gen.emit("INVOKE_FUNCTION", (self.type_descr, len(node.args) + 1))
            else:
                code_gen.emit_invoke_method(self.type_descr, len(node.args))


class StrictBuiltins(Object[Class]):
    def __init__(self, builtins: Dict[str, Value]) -> None:
        super().__init__(DICT_TYPE)
        self.builtins = builtins

    def bind_subscr(
        self, node: ast.Subscript, type: Value, visitor: TypeBinder
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


def get_default_value(default: expr) -> object:
    if not isinstance(default, (Constant, Str, Num, Bytes, NameConstant, ast.Ellipsis)):

        default = AstOptimizer().visit(default)

    if isinstance(default, Str):
        return default.s
    elif isinstance(default, Num):
        return default.n
    elif isinstance(default, Bytes):
        return default.s
    elif isinstance(default, ast.Ellipsis):
        return ...
    elif isinstance(default, (ast.Constant, ast.NameConstant)):
        return default.value
    else:
        return default


# Bringing up the type system is a little special as we have dependencies
# amongst type and object
TYPE_TYPE = Class.__new__(Class)
TYPE_TYPE.type_name = TypeName("builtins", "type")
TYPE_TYPE.klass = TYPE_TYPE
TYPE_TYPE.instance = TYPE_TYPE
TYPE_TYPE.members = {}
TYPE_TYPE.is_exact = False
TYPE_TYPE.is_final = False
TYPE_TYPE._mro = None
TYPE_TYPE._mro_type_descrs = None


class Slot(Object[TClassInv]):
    def __init__(
        self,
        type_ref: TypeRef,
        name: str,
        container_type: Class,
        assignment: Optional[AST] = None,
    ) -> None:
        super().__init__(MEMBER_TYPE)
        self.container_type = container_type
        self.slot_name = name
        self._type_ref = type_ref
        self.assignment = assignment

    def bind_descr_get(
        self,
        node: ast.Attribute,
        inst: Optional[Object[TClassInv]],
        ctx: TClassInv,
        visitor: TypeBinder,
        type_ctx: Optional[Class],
    ) -> None:
        if inst is None:
            visitor.set_type(node, self)
            return

        visitor.set_type(node, self.decl_type.instance)

    @property
    def decl_type(self) -> Class:
        type = self._type_ref.resolved(is_declaration=True)
        if isinstance(type, FinalClass):
            return type.inner_type()
        return type

    @property
    def is_final(self) -> bool:
        return isinstance(self._type_ref.resolved(is_declaration=True), FinalClass)

    @property
    def type_descr(self) -> TypeDescr:
        return self.decl_type.type_descr


# TODO (aniketpanse): move these to a better place
OBJECT_TYPE = Class(TypeName("builtins", "object"))
OBJECT = OBJECT_TYPE.instance

DYNAMIC_TYPE = DynamicClass()
DYNAMIC = DYNAMIC_TYPE.instance


class BoxFunction(Object[Class]):
    def bind_call(
        self, node: ast.Call, visitor: TypeBinder, type_ctx: Optional[Class]
    ) -> NarrowingEffect:
        if len(node.args) != 1:
            raise visitor.syntax_error("box only accepts a single argument", node)

        arg = node.args[0]
        visitor.visit(arg)
        arg_type = visitor.get_type(arg)

        if isinstance(arg_type, CIntInstance):
            typ = BOOL_TYPE if arg_type.constant == TYPED_BOOL else INT_EXACT_TYPE
            visitor.set_type(node, typ.instance)
        elif isinstance(arg_type, CDoubleInstance):
            visitor.set_type(node, FLOAT_EXACT_TYPE.instance)
        else:
            raise visitor.syntax_error(
                f"can't box non-primitive: {arg_type.name}", node
            )
        return NO_EFFECT

    def emit_call(self, node: ast.Call, code_gen: Static38CodeGenerator) -> None:
        code_gen.get_type(node.args[0]).emit_box(node.args[0], code_gen)


class UnboxFunction(Object[Class]):
    def bind_call(
        self, node: ast.Call, visitor: TypeBinder, type_ctx: Optional[Class]
    ) -> NarrowingEffect:
        if len(node.args) != 1:
            raise visitor.syntax_error("unbox only accepts a single argument", node)

        for arg in node.args:
            visitor.visit(arg, DYNAMIC)
        self.check_args_for_primitives(node, visitor)
        visitor.set_type(node, type_ctx or INT64_VALUE)
        return NO_EFFECT

    def emit_call(self, node: ast.Call, code_gen: Static38CodeGenerator) -> None:
        code_gen.get_type(node).emit_unbox(node.args[0], code_gen)


class LenFunction(Object[Class]):
    def __init__(self, klass: Class, boxed: bool) -> None:
        super().__init__(klass)
        self.boxed = boxed

    @property
    def name(self) -> str:
        return f"{'' if self.boxed else 'c'}len function"

    def bind_call(
        self, node: ast.Call, visitor: TypeBinder, type_ctx: Optional[Class]
    ) -> NarrowingEffect:
        if len(node.args) != 1:
            visitor.syntax_error(
                f"len() does not accept more than one arguments ({len(node.args)} given)",
                node,
            )
        arg = node.args[0]
        visitor.visit(arg)
        arg_type = visitor.get_type(arg)
        if not self.boxed and arg_type.get_fast_len_type() is None:
            raise visitor.syntax_error(
                f"bad argument type '{arg_type.name}' for clen()", arg
            )
        self.check_args_for_primitives(node, visitor)
        output_type = INT_EXACT_TYPE.instance if self.boxed else INT64_TYPE.instance
        visitor.set_type(node, output_type)
        return NO_EFFECT

    def emit_call(self, node: ast.Call, code_gen: Static38CodeGenerator) -> None:
        code_gen.get_type(node.args[0]).emit_len(node, code_gen, boxed=self.boxed)


class SortedFunction(Object[Class]):
    @property
    def name(self) -> str:
        return "sorted function"

    def bind_call(
        self, node: ast.Call, visitor: TypeBinder, type_ctx: Optional[Class]
    ) -> NarrowingEffect:
        if len(node.args) != 1:
            visitor.syntax_error(
                f"sorted() accepts one positional argument ({len(node.args)} given)",
                node,
            )
        visitor.visit(node.args[0])
        for kw in node.keywords:
            visitor.visit(kw.value)
        self.check_args_for_primitives(node, visitor)
        visitor.set_type(node, LIST_EXACT_TYPE.instance)
        return NO_EFFECT

    def emit_call(self, node: ast.Call, code_gen: Static38CodeGenerator) -> None:
        super().emit_call(node, code_gen)
        code_gen.emit("REFINE_TYPE", LIST_EXACT_TYPE.type_descr)


class ExtremumFunction(Object[Class]):
    def __init__(self, klass: Class, is_min: bool) -> None:
        super().__init__(klass)
        self.is_min = is_min

    @property
    def _extremum(self) -> str:
        return "min" if self.is_min else "max"

    @property
    def name(self) -> str:
        return f"{self._extremum} function"

    def emit_call(self, node: ast.Call, code_gen: Static38CodeGenerator) -> None:
        if (
            # We only specialize for two args
            len(node.args) != 2
            # We don't support specialization if any kwargs are present
            or len(node.keywords) > 0
            # If we have any *args, we skip specialization
            or any(isinstance(a, ast.Starred) for a in node.args)
        ):
            return super().emit_call(node, code_gen)

        # Compile `min(a, b)` to a ternary expression, `a if a <= b else b`.
        # Similar for `max(a, b).
        endblock = code_gen.newBlock(f"{self._extremum}_end")
        elseblock = code_gen.newBlock(f"{self._extremum}_else")

        for a in node.args:
            code_gen.visit(a)

        if self.is_min:
            op = "<="
        else:
            op = ">="

        code_gen.emit("DUP_TOP_TWO")
        code_gen.emit("COMPARE_OP", op)
        code_gen.emit("POP_JUMP_IF_FALSE", elseblock)
        # Remove `b` from stack, `a` was the minimum
        code_gen.emit("POP_TOP")
        code_gen.emit("JUMP_FORWARD", endblock)
        code_gen.nextBlock(elseblock)
        # Remove `a` from the stack, `b` was the minimum
        code_gen.emit("ROT_TWO")
        code_gen.emit("POP_TOP")
        code_gen.nextBlock(endblock)


class IsInstanceFunction(Object[Class]):
    def __init__(self) -> None:
        super().__init__(FUNCTION_TYPE)

    @property
    def name(self) -> str:
        return "isinstance function"

    def bind_call(
        self, node: ast.Call, visitor: TypeBinder, type_ctx: Optional[Class]
    ) -> NarrowingEffect:
        if node.keywords:
            visitor.syntax_error("isinstance() does not accept keyword arguments", node)
        for arg in node.args:
            visitor.visit(arg)
        self.check_args_for_primitives(node, visitor)
        visitor.set_type(node, BOOL_TYPE.instance)
        if len(node.args) == 2:
            arg0 = node.args[0]
            if not isinstance(arg0, ast.Name):
                return NO_EFFECT

            arg1 = node.args[1]
            klass_type = None
            if isinstance(arg1, ast.Tuple):
                types = tuple(visitor.get_type(el) for el in arg1.elts)
                if all(isinstance(t, Class) for t in types):
                    klass_type = UNION_TYPE.make_generic_type(
                        types, visitor.symtable.generic_types
                    )
            else:
                arg1_type = visitor.get_type(node.args[1])
                if isinstance(arg1_type, Class):
                    klass_type = inexact(arg1_type)

            if klass_type is not None:
                return IsInstanceEffect(
                    arg0.id,
                    visitor.get_type(arg0),
                    inexact(klass_type.instance),
                    visitor,
                )

        return NO_EFFECT


class IsSubclassFunction(Object[Class]):
    def __init__(self) -> None:
        super().__init__(FUNCTION_TYPE)

    @property
    def name(self) -> str:
        return "issubclass function"

    def bind_call(
        self, node: ast.Call, visitor: TypeBinder, type_ctx: Optional[Class]
    ) -> NarrowingEffect:
        if node.keywords:
            raise visitor.syntax_error(
                "issubclass() does not accept keyword arguments", node
            )
        for arg in node.args:
            visitor.visit(arg)
        visitor.set_type(node, BOOL_TYPE.instance)
        self.check_args_for_primitives(node, visitor)
        return NO_EFFECT


class RevealTypeFunction(Object[Class]):
    def __init__(self) -> None:
        super().__init__(FUNCTION_TYPE)

    @property
    def name(self) -> str:
        return "reveal_type function"

    def bind_call(
        self, node: ast.Call, visitor: TypeBinder, type_ctx: Optional[Class]
    ) -> NarrowingEffect:
        if node.keywords:
            raise visitor.syntax_error(
                "reveal_type() does not accept keyword arguments", node
            )
        if len(node.args) != 1:
            raise visitor.syntax_error(
                "reveal_type() accepts exactly one argument", node
            )
        arg = node.args[0]
        visitor.visit(arg)
        arg_type = visitor.get_type(arg)
        msg = f"reveal_type({to_expr(arg)}): '{arg_type.name}'"
        if isinstance(arg, ast.Name) and arg.id in visitor.decl_types:
            decl_type = visitor.decl_types[arg.id].type
            local_type = visitor.local_types[arg.id]
            msg += f", '{arg.id}' has declared type '{decl_type.name}' and local type '{local_type.name}'"
        raise visitor.syntax_error(msg, node)
        return NO_EFFECT


class NumClass(Class):
    def __init__(
        self,
        name: TypeName,
        pytype: Optional[Type[object]] = None,
        is_exact: bool = False,
        literal_value: Optional[int] = None,
    ) -> None:
        bases: List[Class] = [OBJECT_TYPE]
        if literal_value is not None:
            is_exact = True
            bases = [INT_EXACT_TYPE]
        instance = NumExactInstance(self) if is_exact else NumInstance(self)
        super().__init__(
            name,
            bases,
            instance,
            pytype=pytype,
            is_exact=is_exact,
        )
        self.literal_value = literal_value

    def can_assign_from(self, src: Class) -> bool:
        if isinstance(src, NumClass):
            if self.literal_value is not None:
                return src.literal_value == self.literal_value
            if self.is_exact and src.is_exact and self.type_descr == src.type_descr:
                return True
        return super().can_assign_from(src)


class NumInstance(Object[NumClass]):
    def bind_unaryop(
        self, node: ast.UnaryOp, visitor: TypeBinder, type_ctx: Optional[Class]
    ) -> None:
        if isinstance(node.op, (ast.USub, ast.Invert, ast.UAdd)):
            visitor.set_type(node, self)
        else:
            assert isinstance(node.op, ast.Not)
            visitor.set_type(node, BOOL_TYPE.instance)

    def bind_constant(self, node: ast.Constant, visitor: TypeBinder) -> None:
        self._bind_constant(node.value, node, visitor)

    def _bind_constant(
        self, value: object, node: ast.expr, visitor: TypeBinder
    ) -> None:
        value_inst = CONSTANT_TYPES.get(type(value), self)
        visitor.set_type(node, value_inst)
        visitor.check_can_assign_from(self.klass, value_inst.klass, node)


class NumExactInstance(NumInstance):
    @property
    def name(self) -> str:
        if self.klass.literal_value is not None:
            return f"Literal[{self.klass.literal_value}]"
        return super().name

    def bind_binop(
        self, node: ast.BinOp, visitor: TypeBinder, type_ctx: Optional[Class]
    ) -> bool:
        ltype = visitor.get_type(node.left)
        rtype = visitor.get_type(node.right)
        if INT_EXACT_TYPE.can_assign_from(
            ltype.klass
        ) and INT_EXACT_TYPE.can_assign_from(rtype.klass):
            if isinstance(node.op, ast.Div):
                visitor.set_type(node, FLOAT_EXACT_TYPE.instance)
            else:
                visitor.set_type(node, INT_EXACT_TYPE.instance)
            return True
        return False


def parse_param(info: Dict[str, object], idx: int) -> Parameter:
    name = info.get("name", "")
    assert isinstance(name, str)

    return Parameter(
        name,
        idx,
        ResolvedTypeRef(parse_type(info)),
        "default" in info,
        info.get("default"),
        False,
    )


def parse_typed_signature(
    sig: Dict[str, object], klass: Optional[Class] = None
) -> Tuple[Tuple[Parameter, ...], Class]:
    args = sig["args"]
    assert isinstance(args, list)
    if klass is not None:
        signature = [Parameter("self", 0, ResolvedTypeRef(klass), False, None, False)]
    else:
        signature = []

    for idx, arg in enumerate(args):
        signature.append(parse_param(arg, idx + 1))
    return_info = sig["return"]
    assert isinstance(return_info, dict)
    return_type = parse_type(return_info)
    return tuple(signature), return_type


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


def reflect_method_desc(
    obj: MethodDescriptorType, klass: Class
) -> BuiltinMethodDescriptor:
    sig = getattr(obj, "__typed_signature__", None)
    if sig is not None:
        signature, return_type = parse_typed_signature(sig, klass)

        method = BuiltinMethodDescriptor(
            obj.__name__,
            klass,
            signature,
            ResolvedTypeRef(return_type),
        )
    else:
        method = BuiltinMethodDescriptor(obj.__name__, klass)
    return method


def make_type_dict(klass: Class, t: Type[object]) -> Dict[str, Value]:
    ret: Dict[str, Value] = {}
    for k in t.__dict__.keys():
        obj = getattr(t, k)
        if isinstance(obj, MethodDescriptorType):
            ret[k] = reflect_method_desc(obj, klass)

    return ret


def common_sequence_emit_len(
    node: ast.Call, code_gen: Static38CodeGenerator, oparg: int, boxed: bool
) -> None:
    if len(node.args) != 1:
        raise code_gen.syntax_error(
            f"Can only pass a single argument when checking sequence length", node
        )
    code_gen.visit(node.args[0])
    code_gen.emit("FAST_LEN", oparg)
    if boxed:
        signed = True
        code_gen.emit("PRIMITIVE_BOX", int(signed))


def common_sequence_emit_jumpif(
    test: AST,
    next: Block,
    is_if_true: bool,
    code_gen: Static38CodeGenerator,
    oparg: int,
) -> None:
    code_gen.visit(test)
    code_gen.emit("FAST_LEN", oparg)
    code_gen.emit("POP_JUMP_IF_NONZERO" if is_if_true else "POP_JUMP_IF_ZERO", next)


def common_sequence_emit_forloop(
    node: ast.For, code_gen: Static38CodeGenerator, oparg: int
) -> None:
    descr = ("__static__", "int64")
    start = code_gen.newBlock(f"seq_forloop_start")
    anchor = code_gen.newBlock(f"seq_forloop_anchor")
    after = code_gen.newBlock(f"seq_forloop_after")
    with code_gen.new_loopidx() as loop_idx:
        code_gen.set_lineno(node)
        code_gen.push_loop(FOR_LOOP, start, after)
        code_gen.visit(node.iter)

        code_gen.emit("PRIMITIVE_LOAD_CONST", (0, TYPED_INT64))
        code_gen.emit("STORE_LOCAL", (loop_idx, descr))
        code_gen.nextBlock(start)
        code_gen.emit("DUP_TOP")  # used for SEQUENCE_GET
        code_gen.emit("DUP_TOP")  # used for FAST_LEN
        code_gen.emit("FAST_LEN", oparg)
        code_gen.emit("LOAD_LOCAL", (loop_idx, descr))
        code_gen.emit("INT_COMPARE_OP", PRIM_OP_GT_INT)
        code_gen.emit("POP_JUMP_IF_ZERO", anchor)
        code_gen.emit("LOAD_LOCAL", (loop_idx, descr))
        if oparg == FAST_LEN_LIST:
            code_gen.emit("SEQUENCE_GET", SEQ_LIST | SEQ_SUBSCR_UNCHECKED)
        else:
            # todo - we need to implement TUPLE_GET which supports primitive index
            code_gen.emit("PRIMITIVE_BOX", 1)  # 1 is for signed
            code_gen.emit("BINARY_SUBSCR", 2)
        code_gen.emit("LOAD_LOCAL", (loop_idx, descr))
        code_gen.emit("PRIMITIVE_LOAD_CONST", (1, TYPED_INT64))
        code_gen.emit("PRIMITIVE_BINARY_OP", PRIM_OP_ADD_INT)
        code_gen.emit("STORE_LOCAL", (loop_idx, descr))
        code_gen.visit(node.target)
        code_gen.visit(node.body)
        code_gen.emit("JUMP_ABSOLUTE", start)
        code_gen.nextBlock(anchor)
        code_gen.emit("POP_TOP")  # Pop loop index
        code_gen.emit("POP_TOP")  # Pop list
        code_gen.pop_loop()

        if node.orelse:
            code_gen.visit(node.orelse)
        code_gen.nextBlock(after)


class TupleClass(Class):
    def __init__(self, is_exact: bool = False) -> None:
        instance = TupleExactInstance(self) if is_exact else TupleInstance(self)
        super().__init__(
            type_name=TypeName("builtins", "tuple"),
            bases=[OBJECT_TYPE],
            instance=instance,
            is_exact=is_exact,
            pytype=tuple,
        )


class TupleInstance(Object[TupleClass]):
    def get_fast_len_type(self) -> int:
        return FAST_LEN_TUPLE | ((not self.klass.is_exact) << 4)

    def emit_len(
        self, node: ast.Call, code_gen: Static38CodeGenerator, boxed: bool
    ) -> None:
        return common_sequence_emit_len(
            node, code_gen, self.get_fast_len_type(), boxed=boxed
        )

    def emit_jumpif(
        self, test: AST, next: Block, is_if_true: bool, code_gen: Static38CodeGenerator
    ) -> None:
        return common_sequence_emit_jumpif(
            test, next, is_if_true, code_gen, self.get_fast_len_type()
        )

    def emit_binop(self, node: ast.BinOp, code_gen: Static38CodeGenerator) -> None:
        if maybe_emit_sequence_repeat(node, code_gen):
            return
        code_gen.defaultVisit(node)


class TupleExactInstance(TupleInstance):
    def bind_binop(
        self, node: ast.BinOp, visitor: TypeBinder, type_ctx: Optional[Class]
    ) -> bool:
        rtype = visitor.get_type(node.right).klass
        if isinstance(node.op, ast.Mult) and (
            INT_TYPE.can_assign_from(rtype) or rtype in SIGNED_CINT_TYPES
        ):
            visitor.set_type(node, TUPLE_EXACT_TYPE.instance)
            return True
        return super().bind_binop(node, visitor, type_ctx)

    def bind_reverse_binop(
        self, node: ast.BinOp, visitor: TypeBinder, type_ctx: Optional[Class]
    ) -> bool:
        ltype = visitor.get_type(node.left).klass
        if isinstance(node.op, ast.Mult) and (
            INT_TYPE.can_assign_from(ltype) or ltype in SIGNED_CINT_TYPES
        ):
            visitor.set_type(node, TUPLE_EXACT_TYPE.instance)
            return True
        return super().bind_reverse_binop(node, visitor, type_ctx)

    def emit_forloop(self, node: ast.For, code_gen: Static38CodeGenerator) -> None:
        if not isinstance(node.target, ast.Name):
            # We don't yet support `for a, b in my_tuple: ...`
            return super().emit_forloop(node, code_gen)

        return common_sequence_emit_forloop(node, code_gen, FAST_LEN_TUPLE)


class SetClass(Class):
    def __init__(self, is_exact: bool = False) -> None:
        super().__init__(
            type_name=TypeName("builtins", "set"),
            bases=[OBJECT_TYPE],
            instance=SetInstance(self),
            is_exact=is_exact,
            pytype=tuple,
        )


class SetInstance(Object[SetClass]):
    def get_fast_len_type(self) -> int:
        return FAST_LEN_SET | ((not self.klass.is_exact) << 4)

    def emit_len(
        self, node: ast.Call, code_gen: Static38CodeGenerator, boxed: bool
    ) -> None:
        if len(node.args) != 1:
            raise code_gen.syntax_error(
                "Can only pass a single argument when checking set length", node
            )
        code_gen.visit(node.args[0])
        code_gen.emit("FAST_LEN", self.get_fast_len_type())
        if boxed:
            signed = True
            code_gen.emit("PRIMITIVE_BOX", int(signed))

    def emit_jumpif(
        self, test: AST, next: Block, is_if_true: bool, code_gen: Static38CodeGenerator
    ) -> None:
        code_gen.visit(test)
        code_gen.emit("FAST_LEN", self.get_fast_len_type())
        code_gen.emit("POP_JUMP_IF_NONZERO" if is_if_true else "POP_JUMP_IF_ZERO", next)


def maybe_emit_sequence_repeat(
    node: ast.BinOp, code_gen: Static38CodeGenerator
) -> bool:
    if not isinstance(node.op, ast.Mult):
        return False
    for seq, num, rev in [
        (node.left, node.right, 0),
        (node.right, node.left, SEQ_REPEAT_REVERSED),
    ]:
        seq_type = code_gen.get_type(seq).klass
        num_type = code_gen.get_type(num).klass
        oparg = None
        if TUPLE_TYPE.can_assign_from(seq_type):
            oparg = SEQ_TUPLE
        elif LIST_TYPE.can_assign_from(seq_type):
            oparg = SEQ_LIST
        if oparg is None:
            continue
        if num_type in SIGNED_CINT_TYPES:
            oparg |= SEQ_REPEAT_PRIMITIVE_NUM
        elif not INT_TYPE.can_assign_from(num_type):
            continue
        if not seq_type.is_exact:
            oparg |= SEQ_REPEAT_INEXACT_SEQ
        if not num_type.is_exact:
            oparg |= SEQ_REPEAT_INEXACT_NUM
        oparg |= rev
        code_gen.visit(seq)
        code_gen.visit(num)
        code_gen.emit("SEQUENCE_REPEAT", oparg)
        return True
    return False


class ListAppendMethod(BuiltinMethodDescriptor):
    def bind_descr_get(
        self,
        node: ast.Attribute,
        inst: Optional[Object[TClassInv]],
        ctx: TClassInv,
        visitor: TypeBinder,
        type_ctx: Optional[Class],
    ) -> None:
        if inst is None:
            visitor.set_type(node, self)
        else:
            visitor.set_type(node, ListAppendBuiltinMethod(self, node))


class ListAppendBuiltinMethod(BuiltinMethod):
    def emit_call(self, node: ast.Call, code_gen: Static38CodeGenerator) -> None:
        if len(node.args) == 1 and not node.keywords:
            code_gen.visit(self.target.value)
            code_gen.visit(node.args[0])
            code_gen.emit("LIST_APPEND", 1)
            return

        return super().emit_call(node, code_gen)


class ListClass(Class):
    def __init__(self, is_exact: bool = False) -> None:
        instance = ListExactInstance(self) if is_exact else ListInstance(self)
        super().__init__(
            type_name=TypeName("builtins", "list"),
            bases=[OBJECT_TYPE],
            instance=instance,
            is_exact=is_exact,
            pytype=list,
        )
        if is_exact:
            self.members["append"] = ListAppendMethod("append", self)


class ListInstance(Object[ListClass]):
    def get_fast_len_type(self) -> int:
        return FAST_LEN_LIST | ((not self.klass.is_exact) << 4)

    def get_subscr_type(self) -> int:
        return SEQ_LIST_INEXACT

    def emit_len(
        self, node: ast.Call, code_gen: Static38CodeGenerator, boxed: bool
    ) -> None:
        return common_sequence_emit_len(
            node, code_gen, self.get_fast_len_type(), boxed=boxed
        )

    def emit_jumpif(
        self, test: AST, next: Block, is_if_true: bool, code_gen: Static38CodeGenerator
    ) -> None:
        return common_sequence_emit_jumpif(
            test, next, is_if_true, code_gen, self.get_fast_len_type()
        )

    def bind_subscr(
        self, node: ast.Subscript, type: Value, visitor: TypeBinder
    ) -> None:
        if type.klass not in SIGNED_CINT_TYPES:
            super().bind_subscr(node, type, visitor)
        visitor.set_type(node, DYNAMIC)

    def emit_subscr(
        self, node: ast.Subscript, aug_flag: bool, code_gen: Static38CodeGenerator
    ) -> None:
        index_type = code_gen.get_type(node.slice)
        if index_type.klass not in SIGNED_CINT_TYPES:
            return super().emit_subscr(node, aug_flag, code_gen)

        code_gen.update_lineno(node)
        code_gen.visit(node.value)
        code_gen.visit(node.slice)
        if isinstance(node.ctx, ast.Load):
            code_gen.emit("SEQUENCE_GET", self.get_subscr_type())
        elif isinstance(node.ctx, ast.Store):
            code_gen.emit("SEQUENCE_SET", self.get_subscr_type())
        elif isinstance(node.ctx, ast.Del):
            code_gen.emit("LIST_DEL")

    def emit_binop(self, node: ast.BinOp, code_gen: Static38CodeGenerator) -> None:
        if maybe_emit_sequence_repeat(node, code_gen):
            return
        code_gen.defaultVisit(node)


class ListExactInstance(ListInstance):
    def get_subscr_type(self) -> int:
        return SEQ_LIST

    def bind_binop(
        self, node: ast.BinOp, visitor: TypeBinder, type_ctx: Optional[Class]
    ) -> bool:
        rtype = visitor.get_type(node.right).klass
        if isinstance(node.op, ast.Mult) and (
            INT_TYPE.can_assign_from(rtype) or rtype in SIGNED_CINT_TYPES
        ):
            visitor.set_type(node, LIST_EXACT_TYPE.instance)
            return True
        return super().bind_binop(node, visitor, type_ctx)

    def bind_reverse_binop(
        self, node: ast.BinOp, visitor: TypeBinder, type_ctx: Optional[Class]
    ) -> bool:
        ltype = visitor.get_type(node.left).klass
        if isinstance(node.op, ast.Mult) and (
            INT_TYPE.can_assign_from(ltype) or ltype in SIGNED_CINT_TYPES
        ):
            visitor.set_type(node, LIST_EXACT_TYPE.instance)
            return True
        return super().bind_reverse_binop(node, visitor, type_ctx)

    def emit_forloop(self, node: ast.For, code_gen: Static38CodeGenerator) -> None:
        if not isinstance(node.target, ast.Name):
            # We don't yet support `for a, b in my_list: ...`
            return super().emit_forloop(node, code_gen)

        return common_sequence_emit_forloop(node, code_gen, FAST_LEN_LIST)


class StrClass(Class):
    def __init__(self, is_exact: bool = False) -> None:
        super().__init__(
            type_name=TypeName("builtins", "str"),
            bases=[OBJECT_TYPE],
            instance=StrInstance(self),
            is_exact=is_exact,
            pytype=str,
        )


class StrInstance(Object[StrClass]):
    def get_fast_len_type(self) -> int:
        return FAST_LEN_STR | ((not self.klass.is_exact) << 4)

    def emit_len(
        self, node: ast.Call, code_gen: Static38CodeGenerator, boxed: bool
    ) -> None:
        return common_sequence_emit_len(
            node, code_gen, self.get_fast_len_type(), boxed=boxed
        )

    def emit_jumpif(
        self, test: AST, next: Block, is_if_true: bool, code_gen: Static38CodeGenerator
    ) -> None:
        return common_sequence_emit_jumpif(
            test, next, is_if_true, code_gen, self.get_fast_len_type()
        )


class DictClass(Class):
    def __init__(self, is_exact: bool = False) -> None:
        super().__init__(
            type_name=TypeName("builtins", "dict"),
            bases=[OBJECT_TYPE],
            instance=DictInstance(self),
            is_exact=is_exact,
            pytype=dict,
        )


class DictInstance(Object[DictClass]):
    def get_fast_len_type(self) -> int:
        return FAST_LEN_DICT | ((not self.klass.is_exact) << 4)

    def emit_len(
        self, node: ast.Call, code_gen: Static38CodeGenerator, boxed: bool
    ) -> None:
        if len(node.args) != 1:
            raise code_gen.syntax_error(
                "Can only pass a single argument when checking dict length", node
            )
        code_gen.visit(node.args[0])
        code_gen.emit("FAST_LEN", self.get_fast_len_type())
        if boxed:
            signed = True
            code_gen.emit("PRIMITIVE_BOX", int(signed))

    def emit_jumpif(
        self, test: AST, next: Block, is_if_true: bool, code_gen: Static38CodeGenerator
    ) -> None:
        code_gen.visit(test)
        code_gen.emit("FAST_LEN", self.get_fast_len_type())
        code_gen.emit("POP_JUMP_IF_NONZERO" if is_if_true else "POP_JUMP_IF_ZERO", next)


FUNCTION_TYPE = Class(TypeName("types", "FunctionType"))
METHOD_TYPE = Class(TypeName("types", "MethodType"))
MEMBER_TYPE = Class(TypeName("types", "MemberDescriptorType"))
BUILTIN_METHOD_DESC_TYPE = Class(TypeName("types", "MethodDescriptorType"))
BUILTIN_METHOD_TYPE = Class(TypeName("types", "BuiltinMethodType"))
ARG_TYPE = Class(TypeName("builtins", "arg"))
SLICE_TYPE = Class(TypeName("builtins", "slice"))

# builtin types
NONE_TYPE = NoneType()
STR_TYPE = StrClass()
STR_EXACT_TYPE = StrClass(is_exact=True)
INT_TYPE = NumClass(TypeName("builtins", "int"), pytype=int)
INT_EXACT_TYPE = NumClass(TypeName("builtins", "int"), pytype=int, is_exact=True)
FLOAT_TYPE = NumClass(TypeName("builtins", "float"), pytype=float)
FLOAT_EXACT_TYPE = NumClass(TypeName("builtins", "float"), pytype=float, is_exact=True)
COMPLEX_TYPE = NumClass(TypeName("builtins", "complex"), pytype=complex)
COMPLEX_EXACT_TYPE = NumClass(
    TypeName("builtins", "complex"), pytype=complex, is_exact=True
)
BYTES_TYPE = Class(TypeName("builtins", "bytes"), [OBJECT_TYPE], pytype=bytes)
BOOL_TYPE = Class(TypeName("builtins", "bool"), [OBJECT_TYPE], pytype=bool)
ELLIPSIS_TYPE = Class(TypeName("builtins", "ellipsis"), [OBJECT_TYPE], pytype=type(...))
DICT_TYPE = DictClass(is_exact=False)
DICT_EXACT_TYPE = DictClass(is_exact=True)
TUPLE_TYPE = TupleClass()
TUPLE_EXACT_TYPE = TupleClass(is_exact=True)
SET_TYPE = SetClass()
SET_EXACT_TYPE = SetClass(is_exact=True)
LIST_TYPE = ListClass()
LIST_EXACT_TYPE = ListClass(is_exact=True)

BASE_EXCEPTION_TYPE = Class(TypeName("builtins", "BaseException"), pytype=BaseException)
EXCEPTION_TYPE = Class(
    TypeName("builtins", "Exception"),
    bases=[BASE_EXCEPTION_TYPE],
    pytype=Exception,
)
STATIC_METHOD_TYPE = StaticMethodDecorator(
    TypeName("builtins", "staticmethod"),
    bases=[OBJECT_TYPE],
    pytype=staticmethod,
)
FINAL_METHOD_TYPE = TypingFinalDecorator(TypeName("typing", "final"))
ALLOW_WEAKREFS_TYPE = AllowWeakrefsDecorator(TypeName("__static__", "allow_weakrefs"))
DYNAMIC_RETURN_TYPE = DynamicReturnDecorator(TypeName("__static__", "dynamic_return"))
INLINE_TYPE = InlineFunctionDecorator(TypeName("__static__", "inline"))
DONOTCOMPILE_TYPE = DoNotCompileDecorator(TypeName("__static__", "_donotcompile"))

RESOLVED_INT_TYPE = ResolvedTypeRef(INT_TYPE)
RESOLVED_STR_TYPE = ResolvedTypeRef(STR_TYPE)
RESOLVED_NONE_TYPE = ResolvedTypeRef(NONE_TYPE)

TYPE_TYPE.bases = [OBJECT_TYPE]

CONSTANT_TYPES: Mapping[Type[object], Value] = {
    str: STR_EXACT_TYPE.instance,
    int: INT_EXACT_TYPE.instance,
    float: FLOAT_EXACT_TYPE.instance,
    complex: COMPLEX_EXACT_TYPE.instance,
    bytes: BYTES_TYPE.instance,
    bool: BOOL_TYPE.instance,
    type(None): NONE_TYPE.instance,
    tuple: TUPLE_EXACT_TYPE.instance,
    type(...): ELLIPSIS_TYPE.instance,
}

NAMED_TUPLE_TYPE = Class(TypeName("typing", "NamedTuple"))


class FinalClass(GenericClass):

    is_variadic = True

    def make_generic_type(
        self,
        index: Tuple[Class, ...],
        generic_types: GenericTypesDict,
    ) -> Class:
        if len(index) > 1:
            raise TypedSyntaxError(
                f"Final types can only have a single type arg. Given: {str(index)}"
            )
        return super(FinalClass, self).make_generic_type(index, generic_types)

    def inner_type(self) -> Class:
        if self.type_args:
            return self.type_args[0]
        else:
            return DYNAMIC_TYPE


class UnionTypeName(GenericTypeName):
    @property
    def opt_type(self) -> Optional[Class]:
        """If we're an Optional (i.e. Union[T, None]), return T, otherwise None."""
        # Assumes well-formed union (no duplicate elements, >1 element)
        opt_type = None
        if len(self.args) == 2:
            if self.args[0] is NONE_TYPE:
                opt_type = self.args[1]
            elif self.args[1] is NONE_TYPE:
                opt_type = self.args[0]
        return opt_type

    @property
    def type_descr(self) -> TypeDescr:
        opt_type = self.opt_type
        if opt_type is not None:
            return opt_type.type_descr + ("?",)
        # the runtime does not support unions beyond optional, so just fall back
        # to dynamic for runtime purposes
        return DYNAMIC_TYPE.type_descr

    @property
    def friendly_name(self) -> str:
        opt_type = self.opt_type
        if opt_type is not None:
            return f"Optional[{opt_type.instance.name}]"
        return super().friendly_name


class UnionType(GenericClass):
    type_name: UnionTypeName
    # Union is a variadic generic, so we don't give the unbound Union any
    # GenericParameters, and we allow it to accept any number of type args.
    is_variadic = True

    def __init__(
        self,
        type_name: Optional[UnionTypeName] = None,
        type_def: Optional[GenericClass] = None,
        instance_type: Optional[Type[Object[Class]]] = None,
        generic_types: Optional[GenericTypesDict] = None,
    ) -> None:
        instance_type = instance_type or UnionInstance
        super().__init__(
            type_name or UnionTypeName("typing", "Union", ()),
            bases=[],
            instance=instance_type(self),
            type_def=type_def,
        )
        self.generic_types = generic_types

    @property
    def opt_type(self) -> Optional[Class]:
        return self.type_name.opt_type

    def issubclass(self, src: Class) -> bool:
        if isinstance(src, UnionType):
            return all(self.issubclass(t) for t in src.type_args)
        return any(t.issubclass(src) for t in self.type_args)

    def make_generic_type(
        self,
        index: Tuple[Class, ...],
        generic_types: GenericTypesDict,
    ) -> Class:
        instantiations = generic_types.get(self)
        if instantiations is not None:
            instance = instantiations.get(index)
            if instance is not None:
                return instance
        else:
            generic_types[self] = instantiations = {}

        type_args = self._simplify_args(index)
        if len(type_args) == 1 and not type_args[0].is_generic_parameter:
            return type_args[0]
        type_name = UnionTypeName(self.type_name.module, self.type_name.name, type_args)
        if any(isinstance(a, CType) for a in type_args):
            raise TypedSyntaxError(
                f"invalid union type {type_name.friendly_name}; unions cannot include primitive types"
            )
        ThisUnionType = type(self)
        if type_name.opt_type is not None:
            ThisUnionType = OptionalType
        instantiations[index] = concrete = ThisUnionType(
            type_name,
            type_def=self,
            generic_types=generic_types,
        )
        return concrete

    def _simplify_args(self, args: Sequence[Class]) -> Tuple[Class, ...]:
        args = self._flatten_args(args)
        remove = set()
        for i, arg1 in enumerate(args):
            if i in remove:
                continue
            for j, arg2 in enumerate(args):
                # TODO this should be is_subtype_of once we split that from can_assign_from
                if i != j and arg1.can_assign_from(arg2):
                    remove.add(j)
        return tuple(arg for i, arg in enumerate(args) if i not in remove)

    def _flatten_args(self, args: Sequence[Class]) -> Sequence[Class]:
        new_args = []
        for arg in args:
            if isinstance(arg, UnionType):
                new_args.extend(self._flatten_args(arg.type_args))
            else:
                new_args.append(arg)
        return new_args


class UnionInstance(Object[UnionType]):
    def _generic_bind(
        self,
        node: ast.AST,
        callback: typingCallable[[Class], object],
        description: str,
        visitor: TypeBinder,
    ) -> List[object]:
        if self.klass.is_generic_type_definition:
            raise visitor.syntax_error(f"cannot {description} unbound Union", node)
        result_types: List[Class] = []
        ret_types: List[object] = []
        try:
            for el in self.klass.type_args:
                ret_types.append(callback(el))
                result_types.append(visitor.get_type(node).klass)
        except TypedSyntaxError as e:
            raise visitor.syntax_error(f"{self.name}: {e.msg}", node)

        union = UNION_TYPE.make_generic_type(
            tuple(result_types), visitor.symtable.generic_types
        )
        visitor.set_type(node, union.instance)
        return ret_types

    def bind_attr(
        self, node: ast.Attribute, visitor: TypeBinder, type_ctx: Optional[Class]
    ) -> None:
        def cb(el: Class) -> None:
            return el.instance.bind_attr(node, visitor, type_ctx)

        self._generic_bind(
            node,
            cb,
            "access attribute from",
            visitor,
        )

    def bind_call(
        self, node: ast.Call, visitor: TypeBinder, type_ctx: Optional[Class]
    ) -> NarrowingEffect:
        def cb(el: Class) -> NarrowingEffect:
            return el.instance.bind_call(node, visitor, type_ctx)

        self._generic_bind(node, cb, "call", visitor)
        return NO_EFFECT

    def bind_subscr(
        self, node: ast.Subscript, type: Value, visitor: TypeBinder
    ) -> None:
        def cb(el: Class) -> None:
            return el.instance.bind_subscr(node, type, visitor)

        self._generic_bind(node, cb, "subscript", visitor)

    def bind_unaryop(
        self, node: ast.UnaryOp, visitor: TypeBinder, type_ctx: Optional[Class]
    ) -> None:
        def cb(el: Class) -> None:
            return el.instance.bind_unaryop(node, visitor, type_ctx)

        self._generic_bind(
            node,
            cb,
            "unary op",
            visitor,
        )

    def bind_compare(
        self,
        node: ast.Compare,
        left: expr,
        op: cmpop,
        right: expr,
        visitor: TypeBinder,
        type_ctx: Optional[Class],
    ) -> bool:
        def cb(el: Class) -> bool:
            return el.instance.bind_compare(node, left, op, right, visitor, type_ctx)

        rets = self._generic_bind(node, cb, "compare", visitor)
        return all(rets)

    def bind_reverse_compare(
        self,
        node: ast.Compare,
        left: expr,
        op: cmpop,
        right: expr,
        visitor: TypeBinder,
        type_ctx: Optional[Class],
    ) -> bool:
        def cb(el: Class) -> bool:
            return el.instance.bind_reverse_compare(
                node, left, op, right, visitor, type_ctx
            )

        rets = self._generic_bind(node, cb, "compare", visitor)
        return all(rets)


class OptionalType(UnionType):
    """UnionType for instantiations with [T, None], and to support Optional[T] special form."""

    is_variadic = False

    def __init__(
        self,
        type_name: Optional[UnionTypeName] = None,
        type_def: Optional[GenericClass] = None,
        generic_types: Optional[GenericTypesDict] = None,
    ) -> None:
        super().__init__(
            type_name
            or UnionTypeName("typing", "Optional", (GenericParameter("T", 0),)),
            type_def=type_def,
            instance_type=OptionalInstance,
            generic_types=generic_types,
        )

    @property
    def opt_type(self) -> Class:
        opt_type = self.type_name.opt_type
        if opt_type is None:
            params = ", ".join(t.name for t in self.type_args)
            raise TypeError(f"OptionalType has invalid type parameters {params}")
        return opt_type

    def make_generic_type(
        self, index: Tuple[Class, ...], generic_types: GenericTypesDict
    ) -> Class:
        assert len(index) == 1
        if not index[0].is_generic_parameter:
            # Optional[T] is syntactic sugar for Union[T, None]
            index = index + (NONE_TYPE,)
        return super().make_generic_type(index, generic_types)


class OptionalInstance(UnionInstance):
    """Only exists for typing purposes (so we know .klass is OptionalType)."""

    klass: OptionalType


class ArrayInstance(Object["ArrayClass"]):
    def _seq_type(self) -> int:
        idx = self.klass.index
        if not isinstance(idx, CIntType):
            # should never happen
            raise SyntaxError(f"Invalid Array type: {idx}")
        size = idx.size
        if size == 0:
            return SEQ_ARRAY_INT8 if idx.signed else SEQ_ARRAY_UINT8
        elif size == 1:
            return SEQ_ARRAY_INT16 if idx.signed else SEQ_ARRAY_UINT16
        elif size == 2:
            return SEQ_ARRAY_INT32 if idx.signed else SEQ_ARRAY_UINT32
        elif size == 3:
            return SEQ_ARRAY_INT64 if idx.signed else SEQ_ARRAY_UINT64
        else:
            raise SyntaxError(f"Invalid Array size: {size}")

    def bind_subscr(
        self, node: ast.Subscript, type: Value, visitor: TypeBinder
    ) -> None:
        if type == SLICE_TYPE.instance:
            # Slicing preserves type
            return visitor.set_type(node, self)

        visitor.set_type(node, self.klass.index.instance)

    def emit_subscr(
        self, node: ast.Subscript, aug_flag: bool, code_gen: Static38CodeGenerator
    ) -> None:
        index_type = code_gen.get_type(node.slice)
        is_del = isinstance(node.ctx, ast.Del)
        index_is_python_int = INT_TYPE.can_assign_from(index_type.klass)
        index_is_primitive_int = isinstance(index_type.klass, CIntType)

        # ARRAY_{GET,SET} support only integer indices and don't support del;
        # otherwise defer to the usual bytecode
        if is_del or not (index_is_python_int or index_is_primitive_int):
            return super().emit_subscr(node, aug_flag, code_gen)

        code_gen.update_lineno(node)
        code_gen.visit(node.value)
        code_gen.visit(node.slice)

        if index_is_python_int:
            # If the index is not a primitive, unbox its value to an int64, our implementation of
            # SEQUENCE_{GET/SET} expects the index to be a primitive int.
            code_gen.emit("INT_UNBOX", INT64_TYPE.instance.as_oparg())

        if isinstance(node.ctx, ast.Store) and not aug_flag:
            code_gen.emit("SEQUENCE_SET", self._seq_type())
        elif isinstance(node.ctx, ast.Load) or aug_flag:
            if aug_flag:
                code_gen.emit("DUP_TOP_TWO")
            code_gen.emit("SEQUENCE_GET", self._seq_type())

    def emit_store_subscr(
        self, node: ast.Subscript, code_gen: Static38CodeGenerator
    ) -> None:
        code_gen.emit("ROT_THREE")
        code_gen.emit("SEQUENCE_SET", self._seq_type())

    def __repr__(self) -> str:
        return f"{self.klass.type_name.name}[{self.klass.index.name!r}]"

    def get_fast_len_type(self) -> int:
        return FAST_LEN_ARRAY | ((not self.klass.is_exact) << 4)

    def emit_len(
        self, node: ast.Call, code_gen: Static38CodeGenerator, boxed: bool
    ) -> None:
        if len(node.args) != 1:
            raise code_gen.syntax_error(
                "Can only pass a single argument when checking array length", node
            )
        code_gen.visit(node.args[0])
        code_gen.emit("FAST_LEN", self.get_fast_len_type())
        if boxed:
            signed = True
            code_gen.emit("PRIMITIVE_BOX", int(signed))


class ArrayClass(GenericClass):
    def __init__(
        self,
        name: GenericTypeName,
        bases: Optional[List[Class]] = None,
        instance: Optional[Object[Class]] = None,
        klass: Optional[Class] = None,
        members: Optional[Dict[str, Value]] = None,
        type_def: Optional[GenericClass] = None,
        is_exact: bool = False,
        pytype: Optional[Type[object]] = None,
    ) -> None:
        default_bases: List[Class] = [OBJECT_TYPE]
        default_instance: Object[Class] = ArrayInstance(self)
        super().__init__(
            name,
            bases or default_bases,
            instance or default_instance,
            klass,
            members,
            type_def,
            is_exact,
            pytype,
        )

    @property
    def index(self) -> Class:
        return self.type_args[0]

    def make_generic_type(
        self, index: Tuple[Class, ...], generic_types: GenericTypesDict
    ) -> Class:
        for tp in index:
            if tp not in ALLOWED_ARRAY_TYPES:
                raise TypedSyntaxError(
                    f"Invalid {self.gen_name.name} element type: {tp.instance.name}"
                )
        return super().make_generic_type(index, generic_types)


class VectorClass(ArrayClass):
    def __init__(
        self,
        name: GenericTypeName,
        bases: Optional[List[Class]] = None,
        instance: Optional[Object[Class]] = None,
        klass: Optional[Class] = None,
        members: Optional[Dict[str, Value]] = None,
        type_def: Optional[GenericClass] = None,
        is_exact: bool = False,
        pytype: Optional[Type[object]] = None,
    ) -> None:
        super().__init__(
            name,
            bases,
            instance,
            klass,
            members,
            type_def,
            is_exact,
            pytype,
        )
        self.members["append"] = BuiltinMethodDescriptor(
            "append",
            self,
            (
                Parameter("self", 0, ResolvedTypeRef(self), False, None, False),
                Parameter(
                    "v",
                    0,
                    ResolvedTypeRef(VECTOR_TYPE_PARAM),
                    False,
                    None,
                    False,
                ),
            ),
        )


BUILTIN_GENERICS: Dict[Class, Dict[GenericTypeIndex, Class]] = {}
UNION_TYPE = UnionType()
OPTIONAL_TYPE = OptionalType()
FINAL_TYPE = FinalClass(GenericTypeName("typing", "Final", ()))
CHECKED_DICT_TYPE_NAME = GenericTypeName(
    "__static__", "chkdict", (GenericParameter("K", 0), GenericParameter("V", 1))
)


class CheckedDict(GenericClass):
    def __init__(
        self,
        name: GenericTypeName,
        bases: Optional[List[Class]] = None,
        instance: Optional[Object[Class]] = None,
        klass: Optional[Class] = None,
        members: Optional[Dict[str, Value]] = None,
        type_def: Optional[GenericClass] = None,
        is_exact: bool = False,
        pytype: Optional[Type[object]] = None,
    ) -> None:
        if instance is None:
            instance = CheckedDictInstance(self)
        super().__init__(
            name,
            bases,
            instance,
            klass,
            members,
            type_def,
            is_exact,
            pytype,
        )


class CheckedDictInstance(Object[CheckedDict]):
    def bind_subscr(
        self, node: ast.Subscript, type: Value, visitor: TypeBinder
    ) -> None:
        visitor.visit(node.slice, self.klass.gen_name.args[0].instance)
        visitor.set_type(node, self.klass.gen_name.args[1].instance)

    def emit_subscr(
        self, node: ast.Subscript, aug_flag: bool, code_gen: Static38CodeGenerator
    ) -> None:
        if isinstance(node.ctx, ast.Load):
            code_gen.visit(node.value)
            code_gen.visit(node.slice)
            dict_descr = self.klass.type_descr
            update_descr = dict_descr + ("__getitem__",)
            code_gen.emit_invoke_method(update_descr, 1)
        elif isinstance(node.ctx, ast.Store):
            code_gen.visit(node.value)
            code_gen.emit("ROT_TWO")
            code_gen.visit(node.slice)
            code_gen.emit("ROT_TWO")
            dict_descr = self.klass.type_descr
            setitem_descr = dict_descr + ("__setitem__",)
            code_gen.emit_invoke_method(setitem_descr, 2)
            code_gen.emit("POP_TOP")
        else:
            code_gen.defaultVisit(node, aug_flag)

    def get_fast_len_type(self) -> int:
        return FAST_LEN_DICT | ((not self.klass.is_exact) << 4)

    def emit_len(
        self, node: ast.Call, code_gen: Static38CodeGenerator, boxed: bool
    ) -> None:
        if len(node.args) != 1:
            raise code_gen.syntax_error(
                "Can only pass a single argument when checking dict length", node
            )
        code_gen.visit(node.args[0])
        code_gen.emit("FAST_LEN", self.get_fast_len_type())
        if boxed:
            signed = True
            code_gen.emit("PRIMITIVE_BOX", int(signed))

    def emit_jumpif(
        self, test: AST, next: Block, is_if_true: bool, code_gen: Static38CodeGenerator
    ) -> None:
        code_gen.visit(test)
        code_gen.emit("FAST_LEN", self.get_fast_len_type())
        code_gen.emit("POP_JUMP_IF_NONZERO" if is_if_true else "POP_JUMP_IF_ZERO", next)


class CastFunction(Object[Class]):
    def bind_call(
        self, node: ast.Call, visitor: TypeBinder, type_ctx: Optional[Class]
    ) -> NarrowingEffect:
        if len(node.args) != 2:
            raise visitor.syntax_error(
                "cast requires two parameters: type and value", node
            )

        for arg in node.args:
            visitor.visit(arg)
        self.check_args_for_primitives(node, visitor)

        cast_type = visitor.cur_mod.resolve_annotation(node.args[0])
        if cast_type is None:
            raise visitor.syntax_error("cast to unknown type", node)

        visitor.set_type(node, cast_type.instance)
        return NO_EFFECT

    def emit_call(self, node: ast.Call, code_gen: Static38CodeGenerator) -> None:
        code_gen.visit(node.args[1])
        code_gen.emit("CAST", code_gen.get_type(node).klass.type_descr)


prim_name_to_type: Mapping[str, int] = {
    "int8": TYPED_INT8,
    "int16": TYPED_INT16,
    "int32": TYPED_INT32,
    "int64": TYPED_INT64,
    "uint8": TYPED_UINT8,
    "uint16": TYPED_UINT16,
    "uint32": TYPED_UINT32,
    "uint64": TYPED_UINT64,
}


class CInstance(Value, Generic[TClass]):

    _op_name: Dict[Type[ast.operator], str] = {
        ast.Add: "add",
        ast.Sub: "subtract",
        ast.Mult: "multiply",
        ast.FloorDiv: "divide",
        ast.Div: "divide",
        ast.Mod: "modulus",
        ast.LShift: "left shift",
        ast.RShift: "right shift",
        ast.BitOr: "bitwise or",
        ast.BitXor: "xor",
        ast.BitAnd: "bitwise and",
    }

    @property
    def name(self) -> str:
        return self.klass.instance_name

    def binop_error(self, left: Value, right: Value, op: ast.operator) -> str:
        return f"cannot {self._op_name[type(op)]} {left.name} and {right.name}"

    def bind_reverse_binop(
        self, node: ast.BinOp, visitor: TypeBinder, type_ctx: Optional[Class]
    ) -> bool:
        try:
            visitor.visit(node.left, self)
        except TypedSyntaxError:
            raise visitor.syntax_error(
                self.binop_error(visitor.get_type(node.left), self, node.op), node
            )
        visitor.set_type(node, self)
        return True

    def get_op_id(self, op: AST) -> int:
        raise NotImplementedError("Must be implemented in the subclass")

    def emit_binop(self, node: ast.BinOp, code_gen: Static38CodeGenerator) -> None:
        code_gen.update_lineno(node)
        common_type = code_gen.get_type(node)
        code_gen.visit(node.left)
        ltype = code_gen.get_type(node.left)
        if ltype != common_type:
            common_type.emit_convert(ltype, code_gen)
        code_gen.visit(node.right)
        rtype = code_gen.get_type(node.right)
        if rtype != common_type:
            common_type.emit_convert(rtype, code_gen)
        op = self.get_op_id(node.op)
        code_gen.emit("PRIMITIVE_BINARY_OP", op)

    def emit_augassign(
        self, node: ast.AugAssign, code_gen: Static38CodeGenerator
    ) -> None:
        code_gen.set_lineno(node)
        aug_node = wrap_aug(node.target)
        code_gen.visit(aug_node, "load")
        code_gen.visit(node.value)
        code_gen.emit("PRIMITIVE_BINARY_OP", self.get_op_id(node.op))
        code_gen.visit(aug_node, "store")


class CIntInstance(CInstance["CIntType"]):
    def __init__(self, klass: CIntType, constant: int, size: int, signed: bool) -> None:
        super().__init__(klass)
        self.constant = constant
        self.size = size
        self.signed = signed

    def as_oparg(self) -> int:
        return self.constant

    _int_binary_opcode_signed: Mapping[Type[ast.AST], int] = {
        ast.Lt: PRIM_OP_LT_INT,
        ast.Gt: PRIM_OP_GT_INT,
        ast.Eq: PRIM_OP_EQ_INT,
        ast.NotEq: PRIM_OP_NE_INT,
        ast.LtE: PRIM_OP_LE_INT,
        ast.GtE: PRIM_OP_GE_INT,
        ast.Add: PRIM_OP_ADD_INT,
        ast.Sub: PRIM_OP_SUB_INT,
        ast.Mult: PRIM_OP_MUL_INT,
        ast.FloorDiv: PRIM_OP_DIV_INT,
        ast.Div: PRIM_OP_DIV_INT,
        ast.Mod: PRIM_OP_MOD_INT,
        ast.LShift: PRIM_OP_LSHIFT_INT,
        ast.RShift: PRIM_OP_RSHIFT_INT,
        ast.BitOr: PRIM_OP_OR_INT,
        ast.BitXor: PRIM_OP_XOR_INT,
        ast.BitAnd: PRIM_OP_AND_INT,
    }

    _int_binary_opcode_unsigned: Mapping[Type[ast.AST], int] = {
        ast.Lt: PRIM_OP_LT_UN_INT,
        ast.Gt: PRIM_OP_GT_UN_INT,
        ast.Eq: PRIM_OP_EQ_INT,
        ast.NotEq: PRIM_OP_NE_INT,
        ast.LtE: PRIM_OP_LE_UN_INT,
        ast.GtE: PRIM_OP_GE_UN_INT,
        ast.Add: PRIM_OP_ADD_INT,
        ast.Sub: PRIM_OP_SUB_INT,
        ast.Mult: PRIM_OP_MUL_INT,
        ast.FloorDiv: PRIM_OP_DIV_UN_INT,
        ast.Div: PRIM_OP_DIV_UN_INT,
        ast.Mod: PRIM_OP_MOD_UN_INT,
        ast.LShift: PRIM_OP_LSHIFT_INT,
        ast.RShift: PRIM_OP_RSHIFT_INT,
        ast.RShift: PRIM_OP_RSHIFT_UN_INT,
        ast.BitOr: PRIM_OP_OR_INT,
        ast.BitXor: PRIM_OP_XOR_INT,
        ast.BitAnd: PRIM_OP_AND_INT,
    }

    def get_op_id(self, op: AST) -> int:
        return (
            self._int_binary_opcode_signed[type(op)]
            if self.signed
            else (self._int_binary_opcode_unsigned[type(op)])
        )

    def validate_mixed_math(self, other: Value) -> Optional[Value]:
        if self.constant == TYPED_BOOL:
            return None
        if other is self:
            return self
        elif isinstance(other, CIntInstance):
            if other.constant == TYPED_BOOL:
                return None
            if self.signed == other.signed:
                # signs match, we can just treat this as a comparison of the larger type
                if self.size > other.size:
                    return self
                else:
                    return other
            else:
                new_size = max(
                    self.size if self.signed else self.size + 1,
                    other.size if other.signed else other.size + 1,
                )

                if new_size <= TYPED_INT_64BIT:
                    # signs don't match, but we can promote to the next highest data type
                    return SIGNED_CINT_TYPES[new_size].instance

        return None

    def bind_compare(
        self,
        node: ast.Compare,
        left: expr,
        op: cmpop,
        right: expr,
        visitor: TypeBinder,
        type_ctx: Optional[Class],
    ) -> bool:
        if visitor.get_type(right) != self:
            try:
                visitor.visit(right, type_ctx or INT64_VALUE)
            except TypedSyntaxError:
                # Report a better error message than the generic can't be used
                raise visitor.syntax_error(
                    f"can't compare {self.name} to {visitor.get_type(right).name}",
                    node,
                )

        compare_type = self.validate_mixed_math(visitor.get_type(right))
        if compare_type is None:
            raise visitor.syntax_error(
                f"can't compare {self.name} to {visitor.get_type(right).name}", node
            )

        visitor.set_type(op, compare_type)
        visitor.set_type(node, CBOOL_TYPE.instance)
        return True

    def bind_reverse_compare(
        self,
        node: ast.Compare,
        left: expr,
        op: cmpop,
        right: expr,
        visitor: TypeBinder,
        type_ctx: Optional[Class],
    ) -> bool:
        if not isinstance(visitor.get_type(left), CIntInstance):
            try:
                visitor.visit(left, type_ctx or INT64_VALUE)
            except TypedSyntaxError:
                # Report a better error message than the generic can't be used
                raise visitor.syntax_error(
                    f"can't compare {self.name} to {visitor.get_type(right).name}", node
                )

            compare_type = self.validate_mixed_math(visitor.get_type(left))
            if compare_type is None:
                raise visitor.syntax_error(
                    f"can't compare {visitor.get_type(left).name} to {self.name}", node
                )

            visitor.set_type(op, compare_type)
            visitor.set_type(node, CBOOL_TYPE.instance)
            return True

        return False

    def emit_compare(self, op: cmpop, code_gen: Static38CodeGenerator) -> None:
        code_gen.emit("INT_COMPARE_OP", self.get_op_id(op))

    def emit_augname(
        self, node: AugName, code_gen: Static38CodeGenerator, mode: str
    ) -> None:
        if mode == "load":
            code_gen.emit("LOAD_LOCAL", (node.id, self.klass.type_descr))
        elif mode == "store":
            code_gen.emit("STORE_LOCAL", (node.id, self.klass.type_descr))

    def validate_int(self, val: object, node: ast.AST, visitor: TypeBinder) -> None:
        if not isinstance(val, int):
            raise visitor.syntax_error(
                f"{type(val).__name__} cannot be used in a context where an int is expected",
                node,
            )

        bits = 8 << self.size
        if self.signed:
            low = -(1 << (bits - 1))
            high = (1 << (bits - 1)) - 1
        else:
            low = 0
            high = (1 << bits) - 1

        if not low <= val <= high:
            # We set a type here so that when call handles the syntax error and tries to
            # improve the error message to "positional argument type mismatch" it can
            # successfully get the type
            visitor.set_type(node, INT_TYPE.instance)
            raise visitor.syntax_error(
                f"constant {val} is outside of the range {low} to {high} for {self.name}",
                node,
            )

    def bind_constant(self, node: ast.Constant, visitor: TypeBinder) -> None:
        self.validate_int(node.value, node, visitor)
        visitor.set_type(node, self)

    def emit_constant(
        self, node: ast.Constant, code_gen: Static38CodeGenerator
    ) -> None:
        val = node.value
        if self.constant == TYPED_BOOL:
            val = bool(val)
        code_gen.emit("PRIMITIVE_LOAD_CONST", (val, self.as_oparg()))

    def emit_name(self, node: ast.Name, code_gen: Static38CodeGenerator) -> None:
        if isinstance(node.ctx, ast.Load):
            code_gen.emit("LOAD_LOCAL", (node.id, self.klass.type_descr))
        elif isinstance(node.ctx, ast.Store):
            code_gen.emit("STORE_LOCAL", (node.id, self.klass.type_descr))
        else:
            raise TypedSyntaxError("unsupported op")

    def emit_jumpif(
        self, test: AST, next: Block, is_if_true: bool, code_gen: Static38CodeGenerator
    ) -> None:
        code_gen.visit(test)
        code_gen.emit("POP_JUMP_IF_NONZERO" if is_if_true else "POP_JUMP_IF_ZERO", next)

    def emit_jumpif_pop(
        self, test: AST, next: Block, is_if_true: bool, code_gen: Static38CodeGenerator
    ) -> None:
        code_gen.visit(test)
        code_gen.emit(
            "JUMP_IF_NONZERO_OR_POP" if is_if_true else "JUMP_IF_ZERO_OR_POP", next
        )

    def bind_binop(
        self, node: ast.BinOp, visitor: TypeBinder, type_ctx: Optional[Class]
    ) -> bool:
        if self.constant == TYPED_BOOL:
            raise TypedSyntaxError(
                f"cbool is not a valid operand type for {self._op_name[type(node.op)]}"
            )
        rinst = visitor.get_type(node.right)
        if rinst != self:
            if rinst.klass == LIST_EXACT_TYPE:
                visitor.set_type(node, LIST_EXACT_TYPE.instance)
                return True
            if rinst.klass == TUPLE_EXACT_TYPE:
                visitor.set_type(node, TUPLE_EXACT_TYPE.instance)
                return True

            try:
                visitor.visit(node.right, type_ctx or INT64_VALUE)
            except TypedSyntaxError:
                # Report a better error message than the generic can't be used
                raise visitor.syntax_error(
                    self.binop_error(self, visitor.get_type(node.right), node.op),
                    node,
                )

        if type_ctx is None:
            type_ctx = self.validate_mixed_math(visitor.get_type(node.right))
            if type_ctx is None:
                raise visitor.syntax_error(
                    self.binop_error(self, visitor.get_type(node.right), node.op),
                    node,
                )

        visitor.set_type(node, type_ctx)
        return True

    def emit_box(self, node: expr, code_gen: Static38CodeGenerator) -> None:
        code_gen.visit(node)
        type = code_gen.get_type(node)
        if isinstance(type, CIntInstance):
            code_gen.emit("PRIMITIVE_BOX", self.as_oparg())
        else:
            raise RuntimeError("unsupported box type: " + type.name)

    def emit_unbox(self, node: expr, code_gen: Static38CodeGenerator) -> None:
        final_val = code_gen.get_final_literal(node)
        if final_val is not None:
            return self.emit_constant(final_val, code_gen)
        typ = code_gen.get_type(node).klass
        if isinstance(typ, NumClass) and typ.literal_value is not None:
            code_gen.emit("PRIMITIVE_LOAD_CONST", (typ.literal_value, self.as_oparg()))
            return
        code_gen.visit(node)
        code_gen.emit("INT_UNBOX", self.as_oparg())

    def bind_unaryop(
        self, node: ast.UnaryOp, visitor: TypeBinder, type_ctx: Optional[Class]
    ) -> None:
        if isinstance(node.op, (ast.USub, ast.Invert, ast.UAdd)):
            visitor.set_type(node, self)
        else:
            assert isinstance(node.op, ast.Not)
            visitor.set_type(node, BOOL_TYPE.instance)

    def emit_unaryop(self, node: ast.UnaryOp, code_gen: Static38CodeGenerator) -> None:
        code_gen.update_lineno(node)
        if isinstance(node.op, ast.USub):
            code_gen.visit(node.operand)
            code_gen.emit("PRIMITIVE_UNARY_OP", PRIM_OP_NEG_INT)
        elif isinstance(node.op, ast.Invert):
            code_gen.visit(node.operand)
            code_gen.emit("PRIMITIVE_UNARY_OP", PRIM_OP_INV_INT)
        elif isinstance(node.op, ast.UAdd):
            code_gen.visit(node.operand)
        elif isinstance(node.op, ast.Not):
            raise NotImplementedError()

    def emit_convert(self, to_type: Value, code_gen: Static38CodeGenerator) -> None:
        assert isinstance(to_type, CIntInstance)
        # Lower nibble is type-from, higher nibble is type-to.
        code_gen.emit("CONVERT_PRIMITIVE", (self.as_oparg() << 4) | to_type.as_oparg())


class CIntType(CType):
    instance: CIntInstance

    def __init__(self, constant: int, name_override: Optional[str] = None) -> None:
        self.constant = constant
        # See TYPED_SIZE macro
        self.size: int = (constant >> 1) & 3
        self.signed: bool = bool(constant & 1)
        if name_override is None:
            name = ("" if self.signed else "u") + "int" + str(8 << self.size)
        else:
            name = name_override
        super().__init__(
            TypeName("__static__", name),
            [],
            CIntInstance(self, self.constant, self.size, self.signed),
        )

    def can_assign_from(self, src: Class) -> bool:
        if isinstance(src, CIntType):
            if src.size <= self.size and src.signed == self.signed:
                # assignment to same or larger size, with same sign
                # is allowed
                return True
            if src.size < self.size and self.signed:
                # assignment to larger signed size from unsigned is
                # allowed
                return True

        return super().can_assign_from(src)

    def bind_call(
        self, node: ast.Call, visitor: TypeBinder, type_ctx: Optional[Class]
    ) -> NarrowingEffect:
        if len(node.args) != 1:
            raise visitor.syntax_error(
                f"{self.name} requires a single argument ({len(node.args)} given)", node
            )

        visitor.set_type(node, self.instance)
        arg = node.args[0]
        try:
            visitor.visit(arg, self.instance)
        except TypedSyntaxError:
            visitor.visit(arg)
            arg_type = visitor.get_type(arg)
            if (
                arg_type is not INT_TYPE.instance
                and arg_type is not DYNAMIC
                and arg_type is not OBJECT
            ):
                raise

        return NO_EFFECT

    def emit_call(self, node: ast.Call, code_gen: Static38CodeGenerator) -> None:
        if len(node.args) != 1:
            raise code_gen.syntax_error(
                f"{self.name} requires a single argument ({len(node.args)} given)", node
            )

        arg = node.args[0]
        arg_type = code_gen.get_type(arg)
        if isinstance(arg_type, CIntInstance):
            code_gen.visit(arg)
            if arg_type != self.instance:
                self.instance.emit_convert(arg_type, code_gen)
        else:
            self.instance.emit_unbox(arg, code_gen)


class CDoubleInstance(CInstance["CDoubleType"]):

    _double_binary_opcode_signed: Mapping[Type[ast.AST], int] = {
        ast.Add: PRIM_OP_ADD_DBL,
        ast.Sub: PRIM_OP_SUB_DBL,
        ast.Mult: PRIM_OP_MUL_DBL,
        ast.Div: PRIM_OP_DIV_DBL,
    }

    def get_op_id(self, op: AST) -> int:
        return self._double_binary_opcode_signed[type(op)]

    def as_oparg(self) -> int:
        return TYPED_DOUBLE

    def emit_name(self, node: ast.Name, code_gen: Static38CodeGenerator) -> None:
        if isinstance(node.ctx, ast.Load):
            code_gen.emit("LOAD_LOCAL", (node.id, self.klass.type_descr))
        elif isinstance(node.ctx, ast.Store):
            code_gen.emit("STORE_LOCAL", (node.id, self.klass.type_descr))
        else:
            raise TypedSyntaxError("unsupported op")

    def bind_binop(
        self, node: ast.BinOp, visitor: TypeBinder, type_ctx: Optional[Class]
    ) -> bool:
        rtype = visitor.get_type(node.right)
        if rtype != self or type(node.op) not in self._double_binary_opcode_signed:
            raise visitor.syntax_error(self.binop_error(self, rtype, node.op), node)

        visitor.set_type(node, self)
        return True

    def bind_constant(self, node: ast.Constant, visitor: TypeBinder) -> None:
        visitor.set_type(node, self)

    def emit_constant(
        self, node: ast.Constant, code_gen: Static38CodeGenerator
    ) -> None:
        code_gen.emit("PRIMITIVE_LOAD_CONST", (node.value, self.as_oparg()))

    def emit_box(self, node: expr, code_gen: Static38CodeGenerator) -> None:
        code_gen.visit(node)
        type = code_gen.get_type(node)
        if isinstance(type, CDoubleInstance):
            code_gen.emit("PRIMITIVE_BOX", self.as_oparg())
        else:
            raise RuntimeError("unsupported box type: " + type.name)


class CDoubleType(CType):
    def __init__(self) -> None:
        super().__init__(
            TypeName("__static__", "double"),
            [OBJECT_TYPE],
            CDoubleInstance(self),
        )


CBOOL_TYPE = CIntType(TYPED_BOOL, name_override="cbool")

INT8_TYPE = CIntType(TYPED_INT8)
INT16_TYPE = CIntType(TYPED_INT16)
INT32_TYPE = CIntType(TYPED_INT32)
INT64_TYPE = CIntType(TYPED_INT64)

UINT8_TYPE = CIntType(TYPED_UINT8)
UINT16_TYPE = CIntType(TYPED_UINT16)
UINT32_TYPE = CIntType(TYPED_UINT32)
UINT64_TYPE = CIntType(TYPED_UINT64)

INT64_VALUE = INT64_TYPE.instance

CHAR_TYPE = CIntType(TYPED_INT8, name_override="char")
DOUBLE_TYPE = CDoubleType()
ARRAY_TYPE = ArrayClass(
    GenericTypeName("__static__", "Array", (GenericParameter("T", 0),))
)
ARRAY_EXACT_TYPE = ArrayClass(
    GenericTypeName("__static__", "Array", (GenericParameter("T", 0),)), is_exact=True
)

# Vectors are just currently a special type of array that support
# methods that resize them.
VECTOR_TYPE_PARAM = GenericParameter("T", 0)
VECTOR_TYPE_NAME = GenericTypeName("__static__", "Vector", (VECTOR_TYPE_PARAM,))

VECTOR_TYPE = VectorClass(VECTOR_TYPE_NAME, is_exact=True)


ALLOWED_ARRAY_TYPES: List[Class] = [
    INT8_TYPE,
    INT16_TYPE,
    INT32_TYPE,
    INT64_TYPE,
    UINT8_TYPE,
    UINT16_TYPE,
    UINT32_TYPE,
    UINT64_TYPE,
    CHAR_TYPE,
    DOUBLE_TYPE,
    FLOAT_TYPE,
]

SIGNED_CINT_TYPES = [INT8_TYPE, INT16_TYPE, INT32_TYPE, INT64_TYPE]
UNSIGNED_CINT_TYPES: List[CIntType] = [
    UINT8_TYPE,
    UINT16_TYPE,
    UINT32_TYPE,
    UINT64_TYPE,
]
ALL_CINT_TYPES: Sequence[CIntType] = SIGNED_CINT_TYPES + UNSIGNED_CINT_TYPES

NAME_TO_TYPE: Mapping[object, Class] = {
    "NoneType": NONE_TYPE,
    "object": OBJECT_TYPE,
    "str": STR_TYPE,
    "__static__.int8": INT8_TYPE,
    "__static__.int16": INT16_TYPE,
    "__static__.int32": INT32_TYPE,
    "__static__.int64": INT64_TYPE,
    "__static__.uint8": UINT8_TYPE,
    "__static__.uint16": UINT16_TYPE,
    "__static__.uint32": UINT32_TYPE,
    "__static__.uint64": UINT64_TYPE,
}


def parse_type(info: Dict[str, object]) -> Class:
    optional = info.get("optional", False)
    type = info.get("type")
    if type:
        klass = NAME_TO_TYPE.get(type)
        if klass is None:
            raise NotImplementedError("unsupported type: " + str(type))
    else:
        type_param = info.get("type_param")
        assert isinstance(type_param, int)
        klass = GenericParameter("T" + str(type_param), type_param)

    if optional:
        return OPTIONAL_TYPE.make_generic_type((klass,), BUILTIN_GENERICS)

    return klass


CHECKED_DICT_TYPE = CheckedDict(CHECKED_DICT_TYPE_NAME, [OBJECT_TYPE], pytype=chkdict)

CHECKED_DICT_EXACT_TYPE = CheckedDict(
    CHECKED_DICT_TYPE_NAME, [OBJECT_TYPE], pytype=chkdict, is_exact=True
)

EXACT_TYPES: Mapping[Class, Class] = {
    ARRAY_TYPE: ARRAY_EXACT_TYPE,
    LIST_TYPE: LIST_EXACT_TYPE,
    TUPLE_TYPE: TUPLE_EXACT_TYPE,
    INT_TYPE: INT_EXACT_TYPE,
    FLOAT_TYPE: FLOAT_EXACT_TYPE,
    COMPLEX_TYPE: COMPLEX_EXACT_TYPE,
    DICT_TYPE: DICT_EXACT_TYPE,
    CHECKED_DICT_TYPE: CHECKED_DICT_EXACT_TYPE,
    SET_TYPE: SET_EXACT_TYPE,
    STR_TYPE: STR_EXACT_TYPE,
}

EXACT_INSTANCES: Mapping[Value, Value] = {
    k.instance: v.instance for k, v in EXACT_TYPES.items()
}

INEXACT_TYPES: Mapping[Class, Class] = {v: k for k, v in EXACT_TYPES.items()}

INEXACT_INSTANCES: Mapping[Value, Value] = {v: k for k, v in EXACT_INSTANCES.items()}


def exact(maybe_inexact: Value) -> Value:
    if isinstance(maybe_inexact, UnionInstance):
        return exact_type(maybe_inexact.klass).instance
    exact = EXACT_INSTANCES.get(maybe_inexact)
    return exact or maybe_inexact


def inexact(maybe_exact: Value) -> Value:
    if isinstance(maybe_exact, UnionInstance):
        return inexact_type(maybe_exact.klass).instance
    inexact = INEXACT_INSTANCES.get(maybe_exact)
    return inexact or maybe_exact


def exact_type(maybe_inexact: Class) -> Class:
    if isinstance(maybe_inexact, UnionType):
        generic_types = maybe_inexact.generic_types
        if generic_types is not None:
            return UNION_TYPE.make_generic_type(
                tuple(exact_type(a) for a in maybe_inexact.type_args), generic_types
            )
    exact = EXACT_TYPES.get(maybe_inexact)
    return exact or maybe_inexact


def inexact_type(maybe_exact: Class) -> Class:
    if isinstance(maybe_exact, UnionType):
        generic_types = maybe_exact.generic_types
        if generic_types is not None:
            return UNION_TYPE.make_generic_type(
                tuple(inexact_type(a) for a in maybe_exact.type_args), generic_types
            )
    inexact = INEXACT_TYPES.get(maybe_exact)
    return inexact or maybe_exact


if spamobj is not None:
    SPAM_OBJ = GenericClass(
        GenericTypeName("xxclassloader", "spamobj", (GenericParameter("T", 0),)),
        pytype=spamobj,
    )
    XXGENERIC_T = GenericParameter("T", 0)
    XXGENERIC_U = GenericParameter("U", 1)
    XXGENERIC_TYPE_NAME = GenericTypeName(
        "xxclassloader", "XXGeneric", (XXGENERIC_T, XXGENERIC_U)
    )

    class XXGeneric(GenericClass):
        def __init__(
            self,
            name: GenericTypeName,
            bases: Optional[List[Class]] = None,
            instance: Optional[Object[Class]] = None,
            klass: Optional[Class] = None,
            members: Optional[Dict[str, Value]] = None,
            type_def: Optional[GenericClass] = None,
            is_exact: bool = False,
            pytype: Optional[Type[object]] = None,
        ) -> None:
            super().__init__(
                name,
                bases,
                instance,
                klass,
                members,
                type_def,
                is_exact,
                pytype,
            )
            self.members["foo"] = BuiltinMethodDescriptor(
                "foo",
                self,
                (
                    Parameter("self", 0, ResolvedTypeRef(self), False, None, False),
                    Parameter(
                        "t",
                        0,
                        ResolvedTypeRef(XXGENERIC_T),
                        False,
                        None,
                        False,
                    ),
                    Parameter(
                        "u",
                        0,
                        ResolvedTypeRef(XXGENERIC_U),
                        False,
                        None,
                        False,
                    ),
                ),
            )

    XX_GENERIC_TYPE = XXGeneric(XXGENERIC_TYPE_NAME)
else:
    SPAM_OBJ: Optional[GenericClass] = None


class GenericVisitor(ASTVisitor):
    def __init__(self, module_name: str, filename: str) -> None:
        super().__init__()
        self.module_name = module_name
        self.filename = filename

    def visit(self, node: Union[AST, Sequence[AST]], *args: object) -> Optional[object]:
        # if we have a sequence of nodes, don't catch TypedSyntaxError here;
        # walk_list will call us back with each individual node in turn and we
        # can catch errors and add node info then.
        ctx = (
            error_context(self.filename, node)
            if isinstance(node, AST)
            else nullcontext()
        )
        with ctx:
            return super().visit(node, *args)

    def syntax_error(self, msg: str, node: AST) -> TypedSyntaxError:
        return syntax_error(msg, self.filename, node)


class InitVisitor(ASTVisitor):
    def __init__(
        self, module: ModuleTable, klass: Class, init_func: FunctionDef
    ) -> None:
        super().__init__()
        self.module = module
        self.klass = klass
        self.init_func = init_func

    def visitAnnAssign(self, node: AnnAssign) -> None:
        target = node.target
        if isinstance(target, Attribute):
            value = target.value
            if (
                isinstance(value, ast.Name)
                and value.id == self.init_func.args.args[0].arg
            ):
                attr = target.attr
                self.klass.define_slot(
                    attr,
                    TypeRef(self.module, node.annotation),
                    assignment=node,
                )

    def visitAssign(self, node: Assign) -> None:
        for target in node.targets:
            if not isinstance(target, Attribute):
                continue
            value = target.value
            if (
                isinstance(value, ast.Name)
                and value.id == self.init_func.args.args[0].arg
            ):
                attr = target.attr
                self.klass.define_slot(attr, assignment=node)


class DeclarationVisitor(GenericVisitor):
    def __init__(self, mod_name: str, filename: str, symbols: SymbolTable) -> None:
        super().__init__(mod_name, filename)
        self.symbols = symbols
        self.module = symbols[mod_name] = ModuleTable(mod_name, filename, symbols)

    def finish_bind(self) -> None:
        self.module.finish_bind()

    def visitAnnAssign(self, node: AnnAssign) -> None:
        self.module.decls.append((node, None))

    def visitClassDef(self, node: ClassDef) -> None:
        bases = [self.module.resolve_type(base) or DYNAMIC_TYPE for base in node.bases]
        if not bases:
            bases.append(OBJECT_TYPE)
        klass = Class(TypeName(self.module_name, node.name), bases)
        self.module.decls.append((node, klass))
        for item in node.body:
            with error_context(self.filename, item):
                if isinstance(item, (AsyncFunctionDef, FunctionDef)):
                    function = self._make_function(item)
                    if not function:
                        continue
                    klass.define_function(item.name, function, self)
                    if (
                        item.name != "__init__"
                        or not item.args.args
                        or not isinstance(item, FunctionDef)
                    ):
                        continue

                    InitVisitor(self.module, klass, item).visit(item.body)
                elif isinstance(item, AnnAssign):
                    # class C:
                    #    x: foo
                    target = item.target
                    if isinstance(target, ast.Name):
                        klass.define_slot(
                            target.id,
                            TypeRef(self.module, item.annotation),
                            # Note down whether the slot has been assigned a value.
                            assignment=item if item.value else None,
                        )

        for base in bases:
            if base is NAMED_TUPLE_TYPE:
                # In named tuples, the fields are actually elements
                # of the tuple, so we can't do any advanced binding against it.
                klass = DYNAMIC_TYPE
                break

            if base.is_final:
                raise self.syntax_error(
                    f"Class `{klass.instance.name}` cannot subclass a Final class: `{base.instance.name}`",
                    node,
                )

        for d in node.decorator_list:
            if klass is DYNAMIC_TYPE:
                break
            with error_context(self.filename, d):
                decorator = self.module.resolve_type(d) or DYNAMIC_TYPE
                klass = decorator.bind_decorate_class(klass)

        self.module.children[node.name] = klass

    def _visitFunc(self, node: Union[FunctionDef, AsyncFunctionDef]) -> None:
        function = self._make_function(node)
        if function:
            self.module.children[function.func_name] = function

    def _make_function(
        self, node: Union[FunctionDef, AsyncFunctionDef]
    ) -> Function | StaticMethod | None:
        func = Function(node, self.module, self.type_ref(node.returns))
        for decorator in node.decorator_list:
            decorator_type = self.module.resolve_type(decorator) or DYNAMIC_TYPE
            func = decorator_type.bind_decorate_function(self, func)
            if not isinstance(func, (Function, StaticMethod)):
                return None
        return func

    def visitFunctionDef(self, node: FunctionDef) -> None:
        self._visitFunc(node)

    def visitAsyncFunctionDef(self, node: AsyncFunctionDef) -> None:
        self._visitFunc(node)

    def type_ref(self, ann: Optional[expr]) -> TypeRef:
        if not ann:
            return ResolvedTypeRef(DYNAMIC_TYPE)
        return TypeRef(self.module, ann)

    def visitImport(self, node: Import) -> None:
        for name in node.names:
            self.symbols.import_module(name.name)

    def visitImportFrom(self, node: ImportFrom) -> None:
        mod_name = node.module
        if not mod_name or node.level:
            raise NotImplementedError("relative imports aren't supported")
        self.symbols.import_module(mod_name)
        mod = self.symbols.modules.get(mod_name)
        if mod is not None:
            for name in node.names:
                val = mod.children.get(name.name)
                if val is not None:
                    self.module.children[name.asname or name.name] = val

    # We don't pick up declarations in nested statements
    def visitFor(self, node: For) -> None:
        pass

    def visitAsyncFor(self, node: AsyncFor) -> None:
        pass

    def visitWhile(self, node: While) -> None:
        pass

    def visitIf(self, node: If) -> None:
        test = node.test
        if isinstance(test, Name) and test.id == "TYPE_CHECKING":
            self.visit(node.body)

    def visitWith(self, node: With) -> None:
        pass

    def visitAsyncWith(self, node: AsyncWith) -> None:
        pass

    def visitTry(self, node: Try) -> None:
        pass


class TypedSyntaxError(SyntaxError):
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
                    widest = self._widest_type(value, local_types[key])
                    local_types[key] = widest or self.scope.decl_types[key].type
                continue

        for key in local_types.keys():
            # If a value isn't definitely assigned we can safely turn it
            # back into the declared type
            if key not in entry_locals and key in self.scope.decl_types:
                local_types[key] = self.scope.decl_types[key].type

    def _widest_type(self, *types: Value) -> Optional[Value]:
        # TODO: this should be a join, rather than just reverting to decl_type
        # if neither type is greater than the other
        if len(types) == 1:
            return types[0]

        widest_type = None
        for src in types:
            if src == DYNAMIC:
                return DYNAMIC

            if widest_type is None or src.klass.can_assign_from(widest_type.klass):
                widest_type = src
            elif widest_type is not None and not widest_type.klass.can_assign_from(
                src.klass
            ):
                return None

        return widest_type


class TypeDeclaration:
    def __init__(self, typ: Value, is_final: bool = False) -> None:
        self.type = typ
        self.is_final = is_final


class BindingScope:
    def __init__(self, node: AST) -> None:
        self.node = node
        self.local_types: Dict[str, Value] = {}
        self.decl_types: Dict[str, TypeDeclaration] = {}

    def branch(self) -> LocalsBranch:
        return LocalsBranch(self)

    def declare(self, name: str, typ: Value, is_final: bool = False) -> TypeDeclaration:
        decl = TypeDeclaration(typ, is_final)
        self.decl_types[name] = decl
        self.local_types[name] = typ
        return decl


class ModuleBindingScope(BindingScope):
    def __init__(self, node: ast.Module, module: ModuleTable) -> None:
        super().__init__(node)
        self.module = module
        for name, typ in self.module.children.items():
            self.declare(name, typ)

    def declare(self, name: str, typ: Value, is_final: bool = False) -> TypeDeclaration:
        self.module.children[name] = typ
        return super().declare(name, typ, is_final)


class NarrowingEffect:
    """captures type narrowing effects on variables"""

    def and_(self, other: NarrowingEffect) -> NarrowingEffect:
        if other is NoEffect:
            return self

        return AndEffect(self, other)

    def or_(self, other: NarrowingEffect) -> NarrowingEffect:
        if other is NoEffect:
            return self

        return OrEffect(self, other)

    def not_(self) -> NarrowingEffect:
        return NegationEffect(self)

    def apply(self, local_types: Dict[str, Value]) -> None:
        """applies the given effect in the target scope"""
        pass

    def undo(self, local_types: Dict[str, Value]) -> None:
        """restores the type to its original value"""
        pass

    def reverse(self, local_types: Dict[str, Value]) -> None:
        """applies the reverse of the scope or reverts it if
        there is no reverse"""
        self.undo(local_types)


class AndEffect(NarrowingEffect):
    def __init__(self, *effects: NarrowingEffect) -> None:
        self.effects: Sequence[NarrowingEffect] = effects

    def and_(self, other: NarrowingEffect) -> NarrowingEffect:
        if other is NoEffect:
            return self
        elif isinstance(other, AndEffect):
            return AndEffect(*self.effects, *other.effects)

        return AndEffect(*self.effects, other)

    def apply(self, local_types: Dict[str, Value]) -> None:
        for effect in self.effects:
            effect.apply(local_types)

    def undo(self, local_types: Dict[str, Value]) -> None:
        """restores the type to its original value"""
        for effect in self.effects:
            effect.undo(local_types)


class OrEffect(NarrowingEffect):
    def __init__(self, *effects: NarrowingEffect) -> None:
        self.effects: Sequence[NarrowingEffect] = effects

    def and_(self, other: NarrowingEffect) -> NarrowingEffect:
        if other is NoEffect:
            return self
        elif isinstance(other, OrEffect):
            return OrEffect(*self.effects, *other.effects)

        return OrEffect(*self.effects, other)

    def reverse(self, local_types: Dict[str, Value]) -> None:
        for effect in self.effects:
            effect.reverse(local_types)

    def undo(self, local_types: Dict[str, Value]) -> None:
        """restores the type to its original value"""
        for effect in self.effects:
            effect.undo(local_types)


class NoEffect(NarrowingEffect):
    def union(self, other: NarrowingEffect) -> NarrowingEffect:
        return other


# Singleton instance for no effects
NO_EFFECT = NoEffect()


class NegationEffect(NarrowingEffect):
    def __init__(self, negated: NarrowingEffect) -> None:
        self.negated = negated

    def not_(self) -> NarrowingEffect:
        return self.negated

    def apply(self, local_types: Dict[str, Value]) -> None:
        self.negated.reverse(local_types)

    def undo(self, local_types: Dict[str, Value]) -> None:
        self.negated.undo(local_types)

    def reverse(self, local_types: Dict[str, Value]) -> None:
        self.negated.apply(local_types)


class IsInstanceEffect(NarrowingEffect):
    def __init__(self, var: str, prev: Value, inst: Value, visitor: TypeBinder) -> None:
        self.var = var
        self.prev = prev
        self.inst = inst
        reverse = prev
        if isinstance(prev, UnionInstance):
            type_args = tuple(
                ta for ta in prev.klass.type_args if not inst.klass.can_assign_from(ta)
            )
            reverse = UNION_TYPE.make_generic_type(
                type_args, visitor.symtable.generic_types
            ).instance
        self.rev: Value = reverse

    def apply(self, local_types: Dict[str, Value]) -> None:
        local_types[self.var] = self.inst

    def undo(self, local_types: Dict[str, Value]) -> None:
        local_types[self.var] = self.prev

    def reverse(self, local_types: Dict[str, Value]) -> None:
        local_types[self.var] = self.rev


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
        super().__init__(module_name, filename)
        self.symbols = symbols
        self.scopes: List[BindingScope] = []
        self.symtable = symtable
        self.cur_mod: ModuleTable = symtable[module_name]
        self.optimize = optimize
        self.terminals: Dict[AST, TerminalKind] = {}
        self.inline_depth = 0

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
        decl_type = self.decl_types[name].type
        if local_type is DYNAMIC or not decl_type.klass.can_be_narrowed:
            local_type = decl_type
        self.local_types[name] = local_type
        return local_type

    def maybe_get_current_class(self) -> Optional[Class]:
        scope = self.scope
        if isinstance(scope, ClassDef):
            klass = self.cur_mod.resolve_name(scope.name)
            assert isinstance(klass, Class)
            return klass

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
        self, target: ast.Name, typ: Value, is_final: bool = False
    ) -> None:
        if target.id in self.decl_types:
            raise self.syntax_error(
                f"Cannot redefine local variable {target.id}", target
            )
        if isinstance(typ, CInstance):
            self.check_primitive_scope(target)
        self.binding_scope.declare(target.id, typ, is_final)

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
                        elif name.name == "tinyframe":
                            self.cur_mod.tinyframe = True
                        elif name.name == "noframe":
                            self.cur_mod.noframe = True

    def visitModule(self, node: Module) -> None:
        self.scopes.append(ModuleBindingScope(node, self.cur_mod))

        self.check_static_import_flags(node)

        for stmt in node.body:
            self.visit(stmt)

        self.scopes.pop()

    def set_param(self, arg: ast.arg, arg_type: Class, scope: BindingScope) -> None:
        scope.declare(arg.arg, arg_type.instance)
        self.set_type(arg, arg_type.instance)

    def _visitFunc(self, node: Union[FunctionDef, AsyncFunctionDef]) -> None:
        scope = BindingScope(node)
        for decorator in node.decorator_list:
            self.visit(decorator)
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

        for arg in node.args.posonlyargs:
            ann = arg.annotation
            if ann:
                self.visit(ann)
                arg_type = self.cur_mod.resolve_annotation(ann) or DYNAMIC_TYPE
            elif arg.arg in scope.decl_types:
                # Already handled self
                continue
            else:
                arg_type = DYNAMIC_TYPE
            self.set_param(arg, arg_type, scope)

        for arg in node.args.args:
            ann = arg.annotation
            if ann:
                self.visit(ann)
                arg_type = self.cur_mod.resolve_annotation(ann) or DYNAMIC_TYPE
            elif arg.arg in scope.decl_types:
                # Already handled self
                continue
            else:
                arg_type = DYNAMIC_TYPE

            self.set_param(arg, arg_type, scope)

        if node.args.defaults:
            for default in node.args.defaults:
                self.visit(default)

        if node.args.kw_defaults:
            for default in node.args.kw_defaults:
                if default is not None:
                    self.visit(default)

        vararg = node.args.vararg
        if vararg:
            ann = vararg.annotation
            if ann:
                self.visit(ann)

            self.set_param(vararg, TUPLE_EXACT_TYPE, scope)

        for arg in node.args.kwonlyargs:
            ann = arg.annotation
            if ann:
                self.visit(ann)
                arg_type = self.cur_mod.resolve_annotation(ann) or DYNAMIC_TYPE
            else:
                arg_type = DYNAMIC_TYPE

            self.set_param(arg, arg_type, scope)

        kwarg = node.args.kwarg
        if kwarg:
            ann = kwarg.annotation
            if ann:
                self.visit(ann)
            self.set_param(kwarg, DICT_EXACT_TYPE, scope)

        returns = None if node.args in self.cur_mod.dynamic_returns else node.returns
        if returns:
            # We store the return type on the node for the function as we otherwise
            # don't need to store type information for it
            expected = self.cur_mod.resolve_annotation(returns) or DYNAMIC_TYPE
            self.set_type(node, expected.instance)
            self.visit(returns)
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
        for decorator in node.decorator_list:
            self.visit(decorator)

        for kwarg in node.keywords:
            self.visit(kwarg.value)

        for base in node.bases:
            self.visit(base)

        self.scopes.append(BindingScope(node))

        for stmt in node.body:
            self.visit(stmt)

        self.scopes.pop()

    def set_type(self, node: AST, type: Value) -> None:
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
            raise self.syntax_error(
                "cannot use primitives in global or closure scope", node
            )

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
            klass = self.cur_mod.resolve_name(scope.name)
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
            raise self.syntax_error(
                f"Cannot assign to a Final attribute of {klass.instance.name}:{member_name}",
                target,
            )

    def visitAnnAssign(self, node: AnnAssign) -> None:
        self.visit(node.annotation)

        target = node.target
        comp_type = (
            self.cur_mod.resolve_annotation(node.annotation, is_declaration=True)
            or DYNAMIC_TYPE
        )
        is_final = False
        if isinstance(comp_type, FinalClass):
            is_final = True
            comp_type = comp_type.inner_type()

        if isinstance(target, Name):
            self.declare_local(target, comp_type.instance, is_final)
            self.set_type(target, comp_type.instance)

        self.visit(target)

        value = node.value
        if value:
            self.visit(value, comp_type.instance)
            if isinstance(target, Name):
                # We could be narrowing the type after the assignment, so we update it here
                # even though we assigned it above (but we never narrow primtives)
                new_type = self.get_type(value)
                local_type = self.maybe_set_local_type(target.id, new_type)
                self.set_type(target, local_type)

            self.check_can_assign_from(comp_type, self.get_type(value).klass, node)
            self._check_final_attribute_reassigned(target, node)

    def visitAugAssign(self, node: AugAssign) -> None:
        self.visit(node.target)
        target_type = inexact(self.get_type(node.target))
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
                decl_type = self.decl_types.get(target.id)
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
        self, dest: Class, src: Class, node: AST, reason: str = "cannot be assigned to"
    ) -> None:
        if not dest.can_assign_from(src) and src is not DYNAMIC_TYPE:
            raise self.syntax_error(
                f"type mismatch: {src.instance.name} {reason} {dest.instance.name} ",
                node,
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
            for value in node.values:
                new_effect = self.visit(value) or NO_EFFECT
                effect = effect.or_(new_effect)
                final_type = self.widen(final_type, self.get_type(value))

                new_effect.reverse(self.local_types)

            effect.undo(self.local_types)
        else:
            for value in node.values:
                self.visit(value)
                final_type = self.widen(final_type, self.get_type(value))

        self.set_type(node, final_type or DYNAMIC)
        return effect

    def visitBinOp(
        self, node: BinOp, type_ctx: Optional[Class] = None
    ) -> NarrowingEffect:
        # In order to interpret numeric literals as primitives within a
        # primitive type context, we want to try to pass the type context down
        # to each side, but we can't require this, otherwise things like `List:
        # List * int` would fail.
        try:
            self.visit(node.left, type_ctx)
        except TypedSyntaxError:
            self.visit(node.left)
        try:
            self.visit(node.right, type_ctx)
        except TypedSyntaxError:
            self.visit(node.right)

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
        self.visit(node.body)
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
            raise self.syntax_error(
                f"if expression has incompatible types: {body_t.name} and {else_t.name}",
                node,
            )
        return NO_EFFECT

    def visitSlice(
        self, node: Slice, type_ctx: Optional[Class] = None
    ) -> NarrowingEffect:
        lower = node.lower
        if lower:
            self.visit(lower, type_ctx)
        upper = node.upper
        if upper:
            self.visit(upper, type_ctx)
        step = node.step
        if step:
            self.visit(step, type_ctx)
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
                self.visit(k)
                key_type = self.widen(key_type, self.get_type(k))
                self.visit(v)
                value_type = self.widen(value_type, self.get_type(v))
            else:
                self.visit(v, type_ctx)
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
        if key_type is None:
            key_type = OBJECT_TYPE.instance

        if value_type is None:
            value_type = OBJECT_TYPE.instance

        checked_dict_typ = CHECKED_DICT_EXACT_TYPE if is_exact else CHECKED_DICT_TYPE

        gen_type = checked_dict_typ.make_generic_type(
            (key_type.klass, value_type.klass), self.symtable.generic_types
        )

        if type_ctx is not None:
            type_class = type_ctx.klass
            if type_class.generic_type_def in (
                CHECKED_DICT_EXACT_TYPE,
                CHECKED_DICT_TYPE,
            ):
                assert isinstance(type_class, GenericClass)
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
            else:
                # Otherwise we allow something that would assign to dynamic, but not something
                # that would assign to an unrelated type (e.g. int)
                self.set_type(node, gen_type.instance)
                self.check_can_assign_from(type_class, gen_type, node)
        else:
            self.set_type(node, gen_type.instance)

        return gen_type.instance

    def visitSet(
        self, node: ast.Set, type_ctx: Optional[Class] = None
    ) -> NarrowingEffect:
        for elt in node.elts:
            self.visit(elt)
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

    def assign_value(
        self,
        target: expr,
        value: Value,
        src: Optional[expr] = None,
        assignment: Optional[AST] = None,
    ) -> None:
        if isinstance(target, Name):
            decl_type = self.decl_types.get(target.id)
            if decl_type is None:
                # This var is not declared in the current scope, but it might be a
                # global or nonlocal. In that case, we need to check whether it's a Final.
                scope_type = self.get_var_scope(target.id)
                if scope_type == SC_GLOBAL_EXPLICIT or scope_type == SC_GLOBAL_IMPLICIT:
                    declared_type = self.scopes[0].decl_types.get(target.id, None)
                    if declared_type is not None and declared_type.is_final:
                        raise self.syntax_error(
                            "Cannot assign to a Final variable", target
                        )

                # For an inferred exact type, we want to declare the inexact
                # type; the exact type is useful local inference information,
                # but we should still allow assignment of a subclass later.
                self.declare_local(target, inexact(value))
            else:
                if decl_type.is_final:
                    raise self.syntax_error("Cannot assign to a Final variable", target)
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

        scope = BindingScope(node)
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
            for if_ in node.generators[0].ifs:
                self.visit(if_)

        self.visit(node.key)
        self.visit(node.value)

        self.scopes.pop()

        key_type = self.get_type(node.key)
        value_type = self.get_type(node.value)
        self.set_dict_type(node, key_type, value_type, type_ctx, is_exact=True)

        return NO_EFFECT

    def visit_comprehension(
        self, node: ast.expr, generators: List[ast.comprehension], *elts: ast.expr
    ) -> None:
        self.visit(generators[0].iter)

        scope = BindingScope(node)
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
            for if_ in generators[0].ifs:
                self.visit(if_)

        for elt in elts:
            self.visit(elt)

        self.scopes.pop()

    def visitAwait(
        self, node: Await, type_ctx: Optional[Class] = None
    ) -> NarrowingEffect:
        self.visit(node.value)
        self.set_type(node, DYNAMIC)
        return NO_EFFECT

    def visitYield(
        self, node: Yield, type_ctx: Optional[Class] = None
    ) -> NarrowingEffect:
        value = node.value
        if value is not None:
            self.visit(value)
        self.set_type(node, DYNAMIC)
        return NO_EFFECT

    def visitYieldFrom(
        self, node: YieldFrom, type_ctx: Optional[Class] = None
    ) -> NarrowingEffect:
        self.visit(node.value)
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
                    effect = IsInstanceEffect(
                        other.id, var_type, NONE_TYPE.instance, self
                    )
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
        self.visit(node.value)
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
        val_type.bind_subscr(node, self.get_type(node.slice), self)
        return NO_EFFECT

    def visitStarred(
        self, node: Starred, type_ctx: Optional[Class] = None
    ) -> NarrowingEffect:
        self.visit(node.value)
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
            if type_ctx is not None:
                self.check_can_assign_from(type_ctx.klass, var_type.klass, node)
        else:
            self.set_type(node, self.cur_mod.resolve_name(node.id) or DYNAMIC)

        type = self.get_type(node)
        if (
            isinstance(type, UnionInstance)
            and not type.klass.is_generic_type_definition
        ):
            effect = IsInstanceEffect(node.id, type, NONE_TYPE.instance, self)
            return effect.not_()

        return NO_EFFECT

    def visitList(
        self, node: ast.List, type_ctx: Optional[Class] = None
    ) -> NarrowingEffect:
        for elt in node.elts:
            self.visit(elt, DYNAMIC)
        self.set_type(node, LIST_EXACT_TYPE.instance)
        return NO_EFFECT

    def visitTuple(
        self, node: ast.Tuple, type_ctx: Optional[Class] = None
    ) -> NarrowingEffect:
        for elt in node.elts:
            self.visit(elt, DYNAMIC)
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
                raise self.syntax_error(
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
                    raise self.syntax_error(
                        "from __static__ import * is disallowed", node
                    )
                elif name not in self.symtable.statics.children:
                    raise self.syntax_error(f"unsupported static import {name}", node)

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
                    raise self.syntax_error("Cannot assign to a Final variable", node)

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
        elif self.cur_mod.tinyframe:
            graph.setFlag(self.consts.CO_TINY_FRAME)
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

    def walkClassBody(self, node: ClassDef, gen: CodeGenerator) -> None:
        super().walkClassBody(node, gen)
        cur_mod = self.symtable.modules[self.modname]
        klass = cur_mod.resolve_name(node.name)
        if not isinstance(klass, Class) or klass is DYNAMIC_TYPE:
            return

        class_mems = [
            name for name, value in klass.members.items() if isinstance(value, Slot)
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
            if isinstance(dest, CType):
                # TODO raise this in type binding instead
                raise syntax_error(
                    f"Cannot assign a {src.instance.name} to {dest.instance.name}",
                    self.graph.filename,
                    node,
                )
            self.emit("CAST", dest.type_descr)
        elif not dest.can_assign_from(src):
            # TODO raise this in type binding instead
            raise syntax_error(
                f"Cannot assign a {src.instance.name} to {dest.instance.name}",
                self.graph.filename,
                node,
            )

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
            self.emitChainedCompareStep(op, optype, code, cleanup)
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
        self,
        op: cmpop,
        optype: Value,
        value: AST,
        cleanup: Block,
        jump: str = "JUMP_IF_ZERO_OR_POP",
    ) -> None:
        self.visit(value)
        rtype = self.get_type(value)
        if rtype != optype:
            optype.emit_convert(rtype, self)
        self.emit("DUP_TOP")
        self.emit("ROT_THREE")
        optype.emit_compare(op, self)
        self.emit(jump, cleanup)
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
        self, arg_name: str, non_cellvar_pos: int, cellvars: List[str]
    ) -> int:
        try:
            offset = cellvars.index(arg_name)
        except ValueError:
            return non_cellvar_pos
        else:
            # the negative sign indicates to the runtime/JIT that this is a cellvar
            return -(offset + 1)


StaticCodeGenerator = Static38CodeGenerator
