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
from .declaration_visitor import GenericVisitor, DeclarationVisitor
from .errors import ErrorSink
from .module_table import ModuleTable
from .symbol_table import SymbolTable
from .type_binder import BindingScope, TypeBinder
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
        symtable = SymbolTable(cls)
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
