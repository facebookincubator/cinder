# Copyright (c) Meta Platforms, Inc. and affiliates. (http://www.meta.com)

from __future__ import annotations

import ast
import builtins
import sys
from ast import (
    Assign,
    AST,
    AsyncFunctionDef,
    Attribute,
    BinOp,
    BoolOp,
    Call,
    ClassDef,
    cmpop,
    Compare,
    Constant,
    DictComp,
    expr,
    FunctionDef,
    Module,
    Name,
    UnaryOp,
)
from contextlib import contextmanager
from types import CodeType
from typing import (
    Any,
    Callable as typingCallable,
    cast,
    Dict,
    Generator,
    List,
    Optional,
    Type,
)

from .. import consts, opcode_static
from ..opcodebase import Opcode
from ..pyassem import Block, IndexedSet, PyFlowGraph, PyFlowGraphCinder
from ..pycodegen import CodeGenerator, compile, CompNode, FuncOrLambda
from ..strict import FIXED_MODULES, StrictCodeGenerator
from ..symbols import ClassScope, ModuleScope, Scope, SymbolVisitor
from .compiler import Compiler
from .definite_assignment_checker import DefiniteAssignmentVisitor
from .effects import NarrowingEffect, TypeState
from .module_table import ModuleFlag, ModuleTable
from .type_binder import UsedRefinementField
from .types import (
    _TMP_VAR_PREFIX,
    ASYNC_CACHED_PROPERTY_IMPL_PREFIX,
    AsyncCachedPropertyMethod,
    AwaitableType,
    CACHED_PROPERTY_IMPL_PREFIX,
    CachedPropertyMethod,
    CInstance,
    Class,
    CType,
    Dataclass,
    DataclassDecorator,
    DataclassField,
    DecoratedMethod,
    Function,
    FunctionContainer,
    GenericClass,
    KnownBoolean,
    Slot,
    TType,
    TypeDescr,
    Value,
)

try:
    from cinder import _set_qualname
except ImportError:

    def _set_qualname(code: CodeType, qualname: str) -> None:
        pass


def exec_static(
    source: str,
    locals: Dict[str, object],
    globals: Dict[str, object],
    modname: str = "<module>",
) -> None:
    code = compile(
        source, "<module>", "exec", compiler=StaticCodeGenerator, modname=modname
    )
    if "<fixed-modules>" not in globals:
        globals["<fixed-modules>"] = FIXED_MODULES
    if "<builtins>" not in globals:
        globals["<builtins>"] = builtins.__dict__
    exec(code, locals, globals)


class PyFlowGraph38Static(PyFlowGraphCinder):
    opcode: Opcode = opcode_static.opcode


class InitSubClassGenerator:
    def __init__(self, flow_graph: PyFlowGraph, qualname: str) -> None:
        self.flow_graph = flow_graph
        self.qualname = qualname

    def getCode(self) -> CodeType:
        code = self.flow_graph.getCode()
        _set_qualname(code, self.qualname)
        return code


class Static38CodeGenerator(StrictCodeGenerator):
    flow_graph = PyFlowGraph38Static
    _default_cache: Dict[Type[ast.AST], typingCallable[[...], None]] = {}

    def __init__(
        self,
        parent: Optional[CodeGenerator],
        node: AST,
        symbols: SymbolVisitor,
        graph: PyFlowGraph,
        compiler: Compiler,
        modname: str,
        flags: int = 0,
        optimization_lvl: int = 0,
        enable_patching: bool = False,
        builtins: Dict[str, Any] = builtins.__dict__,
        future_flags: Optional[int] = None,
    ) -> None:
        super().__init__(
            parent,
            node,
            symbols,
            graph,
            flags=flags,
            optimization_lvl=optimization_lvl,
            builtins=builtins,
            future_flags=future_flags,
        )
        self.compiler = compiler
        self.modname = modname
        # Use this counter to allocate temporaries for loop indices
        self._tmpvar_loopidx_count = 0
        self.cur_mod: ModuleTable = self.compiler.modules[modname]
        self.enable_patching = enable_patching

    def _is_static_compiler_disabled(self, node: AST) -> bool:
        if not isinstance(node, (AsyncFunctionDef, FunctionDef, ClassDef)):
            # Static compilation can only be disabled for functions and classes.
            return False
        if node in self.cur_mod.compile_non_static:
            return True
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

        if isinstance(fn, (Function, DecoratedMethod)):
            return fn.donotcompile

        return False

    def make_child_codegen(
        self,
        tree: FuncOrLambda | CompNode | ast.ClassDef,
        graph: PyFlowGraph,
        codegen_type: Optional[Type[CodeGenerator]] = None,
    ) -> CodeGenerator:
        if self._is_static_compiler_disabled(tree):
            return super().make_child_codegen(
                tree, graph, codegen_type=StrictCodeGenerator
            )
        return StaticCodeGenerator(
            self,
            tree,
            self.symbols,
            graph,
            compiler=self.compiler,
            modname=self.modname,
            optimization_lvl=self.optimization_lvl,
            enable_patching=self.enable_patching,
        )

    def _get_arg_types(
        self,
        func: FuncOrLambda | CompNode,
        args: ast.arguments,
        graph: PyFlowGraph,
    ) -> tuple[object, ...]:
        arg_checks = []
        cellvars = graph.cellvars
        is_comprehension = not isinstance(
            func, (ast.AsyncFunctionDef, ast.FunctionDef, ast.Lambda)
        )

        for i, arg in enumerate(args.posonlyargs):
            t = self.get_type(arg)
            if (
                t is not self.compiler.type_env.DYNAMIC
                and t is not self.compiler.type_env.OBJECT
            ):
                arg_checks.append(self._calculate_idx(arg.arg, i, cellvars))
                arg_checks.append(t.klass.type_descr)

        for i, arg in enumerate(args.args):
            # Comprehension nodes don't have arguments when they're typed; make
            # up for that here.
            t = (
                self.compiler.type_env.DYNAMIC
                if is_comprehension
                else self.get_type(arg)
            )
            if (
                t is not self.compiler.type_env.DYNAMIC
                and t is not self.compiler.type_env.OBJECT
            ):
                arg_checks.append(
                    self._calculate_idx(arg.arg, i + len(args.posonlyargs), cellvars)
                )
                arg_checks.append(t.klass.type_descr)

        for i, arg in enumerate(args.kwonlyargs):
            t = self.get_type(arg)
            if (
                t is not self.compiler.type_env.DYNAMIC
                and t is not self.compiler.type_env.OBJECT
            ):
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

        return tuple(arg_checks)

    def get_type(self, node: AST) -> Value:
        return self.cur_mod.types[node]

    def get_node_data(self, key: AST, data_type: Type[TType]) -> TType:
        return self.cur_mod.get_node_data(key, data_type)

    def get_opt_node_data(self, key: AST, data_type: Type[TType]) -> TType | None:
        return self.cur_mod.get_opt_node_data(key, data_type)

    def set_node_data(self, key: AST, data_type: Type[TType], value: TType) -> None:
        self.cur_mod.set_node_data(key, data_type, value)

    @classmethod
    def make_code_gen(
        cls,
        module_name: str,
        tree: AST,
        filename: str,
        flags: int,
        optimize: int,
        ast_optimizer_enabled: bool = True,
        enable_patching: bool = False,
        builtins: Dict[str, Any] = builtins.__dict__,
    ) -> Static38CodeGenerator:
        assert ast_optimizer_enabled

        compiler = Compiler(cls)
        code_gen = compiler.code_gen(
            module_name, filename, tree, optimize, enable_patching, builtins
        )
        return code_gen

    def make_function_graph(
        self,
        func: FuncOrLambda | CompNode,
        func_args: ast.arguments,
        filename: str,
        scopes: Dict[AST, Scope],
        class_name: str,
        name: str,
        first_lineno: int,
    ) -> PyFlowGraph:
        graph = super().make_function_graph(
            func, func_args, filename, scopes, class_name, name, first_lineno
        )

        if self._is_static_compiler_disabled(func):
            return graph

        graph.setFlag(consts.CO_STATICALLY_COMPILED)
        arg_types = self._get_arg_types(func, func_args, graph)
        graph.emit("CHECK_ARGS", arg_types)
        ret_type = self._get_return_type(func)
        graph.extra_consts.append(ret_type.type_descr)
        return graph

    def _get_return_type(self, func: FuncOrLambda | CompNode) -> Class:
        if isinstance(func, (FunctionDef, AsyncFunctionDef)):
            function = self.get_func_container(func)
            klass = function.return_type.resolved()
        else:
            klass = self.get_type(func).klass
        if isinstance(klass, AwaitableType):
            klass = klass.type_args[0]
        return klass

    @contextmanager
    def new_loopidx(self) -> Generator[str, None, None]:
        self._tmpvar_loopidx_count += 1
        try:
            yield f"{_TMP_VAR_PREFIX}.{self._tmpvar_loopidx_count}"
        finally:
            self._tmpvar_loopidx_count -= 1

    def _resolve_class(self, node: ClassDef) -> Optional[Class]:
        cur_mod = self.compiler.modules[self.modname]
        klass = cur_mod.resolve_name(node.name)
        if not isinstance(klass, Class) or klass is self.compiler.type_env.dynamic:
            return
        return klass

    def emit_build_class(self, node: ast.ClassDef, class_body: CodeGenerator) -> None:
        klass = self._resolve_class(node)
        if not isinstance(self.scope, ModuleScope) or klass is None:
            # If a class isn't top-level then it's not going to be using static
            # features (we could relax this in the future for classes nested in classes).
            return super().emit_build_class(node, class_body)

        self._makeClosure(class_body, 0)
        self.emit("LOAD_CONST", node.name)

        if node.keywords:
            for keyword in node.keywords:
                self.emit("LOAD_CONST", keyword.arg)
                self.visit(keyword.value)

            self.emit("BUILD_MAP", len(node.keywords))
        else:
            self.emit("LOAD_CONST", None)

        if "__class__" in class_body.graph.cellvars:
            self.emit("LOAD_CONST", True)
        else:
            self.emit("LOAD_CONST", False)

        assert klass is not None
        final_methods: List[str] = []
        for class_or_subclass in klass.mro:
            final_methods.extend(class_or_subclass.get_own_final_method_names())
        self.emit("LOAD_CONST", tuple(sorted(final_methods)))

        if klass.is_final:
            self.emit("LOAD_CONST", True)
        else:
            self.emit("LOAD_CONST", False)

        count = 0
        for name, value in klass.members.items():
            if isinstance(value, CachedPropertyMethod):
                self.emit("LOAD_CONST", CACHED_PROPERTY_IMPL_PREFIX + name)
                count += 1
            elif isinstance(value, AsyncCachedPropertyMethod):
                self.emit("LOAD_CONST", ASYNC_CACHED_PROPERTY_IMPL_PREFIX + name)
                count += 1

        self.emit("BUILD_TUPLE", count)

        for base in node.bases:
            self.visit(base)

        self.emit(
            "INVOKE_FUNCTION",
            (("_static", "__build_cinder_class__"), 7 + len(node.bases)),
        )

    def processBody(
        self, node: AST, body: List[ast.stmt] | AST, gen: CodeGenerator
    ) -> None:
        if isinstance(node, (ast.FunctionDef | ast.AsyncFunctionDef)):
            # check for any unassigned primitive values and force them to be
            # assigned.
            visitor = DefiniteAssignmentVisitor(self.symbols.scopes[node])
            visitor.analyzeFunction(node)
            for unassigned in visitor.unassigned:
                node_type = self.get_type(unassigned)
                if isinstance(node_type, CInstance):
                    assert type(gen) is Static38CodeGenerator
                    node_type.emit_init(unassigned, gen)

        super().processBody(node, body, gen)

    def visitFunctionOrLambda(
        self, node: ast.FunctionDef | ast.AsyncFunctionDef | ast.Lambda
    ) -> None:
        if isinstance(node, ast.Lambda):
            return super().visitFunctionOrLambda(node)

        function = self.get_func_container(node)
        name = function.emit_function(node, self)
        self.storeName(name)

    def get_func_container(self, node: ast.AST) -> FunctionContainer:
        function = self.get_type(node)
        if not isinstance(function, FunctionContainer):
            raise RuntimeError("bad value for function")

        return function

    def walkClassBody(self, node: ClassDef, gen: CodeGenerator) -> None:
        klass = self._resolve_class(node)
        super().walkClassBody(node, gen)
        if not klass:
            return

        if not klass.has_init_subclass:
            # we define a custom override of __init_subclass__
            if "__class__" not in gen.scope.cells:
                # we need to call super(), which will need to have
                # __class__ available if it's not already...
                gen.graph.cellvars.get_index("__class__")
                gen.scope.cells["__class__"] = 1

            init_graph = self.flow_graph(
                "__init_subclass__",
                self.graph.filename,
                None,
                0,
                ("cls",),
                (),
                (),
                optimized=1,
                docstring=None,
            )
            init_graph.freevars.get_index("__class__")

            init_graph.nextBlock()

            init_graph.emit("LOAD_GLOBAL", "super")
            init_graph.emit("LOAD_DEREF", "__class__")
            init_graph.emit("LOAD_FAST", "cls")
            init_graph.emit("LOAD_METHOD_SUPER", ("__init_subclass__", True))
            init_graph.emit("CALL_METHOD", 0)
            init_graph.emit("POP_TOP")

            init_graph.emit("LOAD_FAST", "cls")
            init_graph.emit("INVOKE_FUNCTION", (("_static", "init_subclass"), 1))
            init_graph.emit("RETURN_VALUE")

            gen.emit("LOAD_CLOSURE", "__class__")
            gen.emit("BUILD_TUPLE", 1)
            gen.emit(
                "LOAD_CONST",
                InitSubClassGenerator(
                    init_graph, self.get_qual_prefix(gen) + ".__init_subclass__"
                ),
            )
            gen.emit("LOAD_CONST", self.get_qual_prefix(gen) + ".__init_subclass__")
            gen.emit("MAKE_FUNCTION", 8)
            gen.emit("STORE_NAME", "__init_subclass__")

        assert isinstance(gen, Static38CodeGenerator)
        klass.emit_extra_members(node, gen)

        class_mems_with_overrides = [
            name
            for name, value in klass.members.items()
            if (isinstance(value, Slot) and not value.is_classvar)
            or isinstance(value, CachedPropertyMethod)
            or isinstance(value, AsyncCachedPropertyMethod)
        ]

        class_mems = [
            name
            for name in class_mems_with_overrides
            # pyre-ignore[16]: `Value` has no attribute `override`.
            if not isinstance(mem := klass.members[name], Slot) or not mem.override
        ]

        if klass.allow_weakrefs:
            class_mems.append("__weakref__")

        # In the future we may want a compatibility mode where we add
        # __dict__ and __weakref__
        gen.emit("LOAD_CONST", tuple(class_mems))
        gen.emit("STORE_NAME", "__slots__")

        slots_with_default = [
            name
            for name in class_mems_with_overrides
            if name in klass.members
            and isinstance(klass.members[name], Slot)
            and cast(
                Slot[Class], klass.members[name]
            ).is_typed_descriptor_with_default_value()
        ]

        for name in slots_with_default:
            gen.emit("LOAD_CONST", name)
            gen.emit("LOAD_NAME", name)
            gen.emit("DELETE_NAME", name)

        gen.emit("BUILD_MAP", len(slots_with_default))
        gen.emit("STORE_NAME", "__slots_with_default__")

        count = 0
        for name, value in klass.members.items():
            if not isinstance(value, Slot):
                continue

            if value.is_classvar:
                continue

            value_type = value.decl_type
            if value.decl_type is self.compiler.type_env.dynamic:
                if value.is_typed_descriptor_with_default_value():
                    value_type = self.compiler.type_env.object
                else:
                    continue

            gen.emit("LOAD_CONST", name)
            gen.emit("LOAD_CONST", value_type.type_descr)
            count += 1

        if count:
            gen.emit("BUILD_MAP", count)
            gen.emit("STORE_NAME", "__slot_types__")

    def visit_decorator(self, decorator: AST, class_def: ClassDef) -> None:
        d = self.get_type(decorator)
        if (
            isinstance(d, DataclassDecorator)
            and self.get_type(class_def) is not self.compiler.type_env.dynamic
        ):
            return

        super().visit_decorator(decorator, class_def)

    def emit_decorator_call(self, decorator: AST, class_def: ClassDef) -> None:
        self.get_type(decorator).emit_decorator_call(class_def, self)

    def emit_load_builtin(self, name: str) -> None:
        is_checked_dict = (
            name == "dict" and ModuleFlag.CHECKED_DICTS in self.cur_mod.flags
        )
        is_checked_list = (
            name == "list" and ModuleFlag.CHECKED_LISTS in self.cur_mod.flags
        )
        if is_checked_dict or is_checked_list:
            chkname = f"chk{name}"
            self.emit("LOAD_CONST", 0)
            self.emit("LOAD_CONST", (chkname,))
            self.emit("IMPORT_NAME", "_static")
            self.emit("IMPORT_FROM", chkname)
            self.emit("ROT_TWO")
            self.emit("POP_TOP")
        else:
            super().emit_load_builtin(name)

    def visitModule(self, node: Module) -> None:
        if ModuleFlag.CHECKED_DICTS in self.cur_mod.flags:
            self.emit_restore_builtin("dict")

        if ModuleFlag.CHECKED_LISTS in self.cur_mod.flags:
            self.emit_restore_builtin("list")

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
        type_state = TypeState()
        effect_nodes: Dict[str, ast.AST] = {}
        effect.apply(type_state, effect_nodes)
        for key, value in type_state.local_types.items():
            if value.klass is not self.compiler.type_env.DYNAMIC:
                self.visit(effect_nodes[key])
                self.emit("CAST", value.klass.type_descr)
                self.emit("POP_TOP")
        for base, refinement_dict in type_state.refined_fields.items():
            for attr, (value, _, _) in refinement_dict.items():
                if value.klass is not self.compiler.type_env.DYNAMIC:
                    key = f"{base}.{attr}"
                    self.visit(effect_nodes[key])
                    self.emit("CAST", value.klass.type_descr)
                    self.emit("POP_TOP")

    def visitAttribute(self, node: Attribute) -> None:
        self.set_lineno(node)
        data = self.get_opt_node_data(node, UsedRefinementField)
        if data is not None and not data.is_source:
            self.emit("LOAD_FAST", data.name)
            return

        if isinstance(node.ctx, ast.Store) and data is not None and data.is_used:
            self.emit("DUP_TOP")
            self.emit("STORE_FAST", data.name)

        self.get_type(node.value).emit_attr(node, self)

        if (
            isinstance(node.ctx, ast.Load)
            and data is not None
            and data.is_source
            and data.is_used
        ):
            self.emit("DUP_TOP")
            self.emit("STORE_FAST", data.name)

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
            elt_type = self.get_type(elt).klass
            value_type = (
                self.compiler.type_env.dynamic
                if value is None
                else self.get_type(value).klass
            )
            elt_type.emit_type_check(value_type, self)
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
        self.strictPostVisitAssign(node)

    def visitAnnAssign(self, node: ast.AnnAssign) -> None:
        self.set_lineno(node)
        value = node.value
        if value:
            value_type = self.get_type(value)
            if (
                isinstance(value_type, DataclassField)
                and isinstance(self.tree, ast.ClassDef)
                and isinstance(self.get_type(self.tree), Dataclass)
            ):
                value_type.emit_field(node.target, self)
            else:
                self.visit(value)
                self.get_type(node.target).klass.emit_type_check(value_type.klass, self)
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

    def visitDefault(self, node: expr) -> None:
        if isinstance(self.get_type(node), CInstance):
            self.get_type(node).emit_box(node, self)
        else:
            self.visit(node)

    def get_final_literal(self, node: AST) -> Optional[ast.Constant]:
        return self.cur_mod.get_final_literal(node, self.scope)

    def visitName(self, node: Name) -> None:
        final_val = self.get_final_literal(node)
        if final_val is not None:
            # visit the constant directly
            return self.defaultVisit(final_val)
        self.get_type(node).emit_name(node, self)

    def emitAugAttribute(self, node: ast.AugAssign) -> None:
        target = node.target
        assert isinstance(target, ast.Attribute)
        self.visit(target.value)
        typ = self.get_type(target.value)
        self.emit("DUP_TOP")
        typ.emit_load_attr(target, self)
        self.emitAugRHS(node)
        self.emit("ROT_TWO")
        typ.emit_store_attr(target, self)

    def emitAugName(self, node: ast.AugAssign) -> None:
        target = node.target
        assert isinstance(target, ast.Name)
        typ = self.get_type(target)
        typ.emit_load_name(target, self)
        self.emitAugRHS(node)
        typ.emit_store_name(target, self)

    def emitAugSubscript(self, node: ast.AugAssign) -> None:
        target = node.target
        assert isinstance(target, ast.Subscript)
        self.visit(target.value)
        self.visit(target.slice)
        typ = self.get_type(target.value)
        self.emit("DUP_TOP_TWO")
        typ.emit_load_subscr(target, self)
        self.emitAugRHS(node)
        self.emit("ROT_THREE")
        typ.emit_store_subscr(target, self)

    def emitAugRHS(self, node: ast.AugAssign) -> None:
        self.get_type(node.target).emit_aug_rhs(node, self)

    def visitCompare(self, node: Compare) -> None:
        self.set_lineno(node)
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
        self.strictPreVisitCall(node)
        self.get_type(node.func).emit_call(node, self)

    def visitSubscript(self, node: ast.Subscript, aug_flag: bool = False) -> None:
        # aug_flag is unused in static compiler; we have our own
        # emitAugSubscript that doesn't call visitSubscript
        self.get_type(node.value).emit_subscr(node, self)

    def _visitReturnValue(self, value: ast.AST, expected: Class) -> None:
        self.visit(value)
        expected.emit_type_check(self.get_type(value).klass, self)

    def visitReturn(self, node: ast.Return) -> None:
        self.checkReturn(node)
        function = self.get_func_container(self.tree)

        expected = function.return_type.resolved()
        if isinstance(self.tree, AsyncFunctionDef):
            assert isinstance(expected, AwaitableType)
            expected = expected.type_args[0]
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
                opcode = "RETURN_PRIMITIVE"
                oparg = expected.instance.as_oparg()
        else:
            self.unwind_setup_entries(preserve_tos=False)
            self.emit("LOAD_CONST", None)

        self.emit(opcode, oparg)

    def visitDictComp(self, node: DictComp) -> None:
        dict_type = self.get_type(node)
        if dict_type in (
            self.compiler.type_env.dict.instance,
            self.compiler.type_env.dict.exact_type().instance,
        ):
            return super().visitDictComp(node)
        klass = dict_type.klass

        assert (
            isinstance(klass, GenericClass)
            and klass.type_def is self.compiler.type_env.checked_dict
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
        if dict_type in (
            self.compiler.type_env.dict.instance,
            self.compiler.type_env.dict.exact_type().instance,
        ):
            return super().visitDict(node)
        klass = dict_type.klass

        assert (
            isinstance(klass, GenericClass)
            and klass.type_def is self.compiler.type_env.checked_dict
        ), dict_type

        self.set_lineno(node)
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

    def compile_sub_checked_list(
        self, node: ast.List, begin: int, end: int, type_descr: TypeDescr
    ) -> None:
        n = end - begin
        for i in range(begin, end):
            elt = node.elts[i]
            assert not isinstance(elt, ast.Starred)
            self.visit(elt)

        self.emit("BUILD_CHECKED_LIST", (type_descr, n))

    def visitListComp(self, node: ast.ListComp) -> None:
        list_type = self.get_type(node)
        if list_type in (
            self.compiler.type_env.list.instance,
            self.compiler.type_env.list.exact_type().instance,
        ):
            return super().visitListComp(node)
        klass = list_type.klass

        assert (
            isinstance(klass, GenericClass)
            and klass.type_def is self.compiler.type_env.checked_list
        ), list_type
        self.compile_comprehension(
            node,
            sys.intern("<listcomp>"),
            node.elt,
            None,
            "BUILD_CHECKED_LIST",
            (list_type.klass.type_descr, 0),
        )

    def visitList(self, node: ast.List) -> None:
        list_type = self.get_type(node)
        if list_type in (
            self.compiler.type_env.list.instance,
            self.compiler.type_env.list.exact_type().instance,
        ):
            return super().visitList(node)
        klass = list_type.klass

        assert (
            isinstance(klass, GenericClass)
            and klass.type_def is self.compiler.type_env.checked_list
        ), list_type

        self.set_lineno(node)
        list_descr = list_type.klass.type_descr
        extend_descr = list_descr + ("extend",)
        built_final_list = False
        elements = 0

        for i, elt in enumerate(node.elts):
            if isinstance(elt, ast.Starred):
                if elements:
                    self.compile_sub_checked_list(node, i - elements, i, list_descr)
                    built_final_list = True
                    elements = 0
                if not built_final_list:
                    # We need to generate the empty list to extend in the case of [*foo, ...].
                    self.emit("BUILD_CHECKED_LIST", (list_descr, 0))
                    built_final_list = True
                self.emit("DUP_TOP")
                self.visit(elt.value)
                self.emit_invoke_method(extend_descr, 1)
                self.emit("POP_TOP")
            else:
                elements += 1

        if elements or not built_final_list:
            if built_final_list:
                self.emit("DUP_TOP")
            self.compile_sub_checked_list(
                node, len(node.elts) - elements, len(node.elts), list_descr
            )
            if built_final_list:
                self.emit_invoke_method(extend_descr, 1)
                self.emit("POP_TOP")

    def visitFor(self, node: ast.For) -> None:
        self.strictPreVisitFor(node)
        iter_type = self.get_type(node.iter)
        iter_type.emit_forloop(node, self)
        self.strictPostVisitFor(node)

    def emit_invoke_method(
        self, descr: TypeDescr, arg_count: int, is_classmethod: bool = False
    ) -> None:
        # Emit a zero EXTENDED_ARG before so that we can optimize and insert the
        # arg count
        self.emit("EXTENDED_ARG", 0)
        self.emit(
            "INVOKE_METHOD",
            (descr, arg_count, True) if is_classmethod else (descr, arg_count),
        )

    def defaultCall(self, node: object, name: str, *args: object) -> None:
        meth = getattr(super(Static38CodeGenerator, Static38CodeGenerator), name)
        return meth(self, node, *args)

    def defaultVisit(self, node: object, *args: object) -> None:
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

    def get_bool_const(self, node: AST) -> bool | None:
        if isinstance(node, ast.Constant):
            return bool(node.value)

        kb = self.get_opt_node_data(node, KnownBoolean)
        if kb is not None:
            return True if kb == KnownBoolean.TRUE else False

    def visitIf(self, node: ast.If) -> None:
        test_type = self.get_type(node.test)

        test_const = self.get_bool_const(node.test)

        end = self.newBlock("if_end")
        orelse = None
        if node.orelse:
            orelse = self.newBlock("if_else")

        self.compileJumpIf(node.test, orelse or end, False)
        if test_const is not False:
            self.visitStatements(node.body)

        if node.orelse:
            self.emit_noline("JUMP_FORWARD", end)
            self.nextBlock(orelse)
            if test_const is not True:
                self.visitStatements(node.orelse)

        self.nextBlock(end)

    def compileJumpIf(self, test: AST, next: Block, is_if_true: bool) -> None:
        if isinstance(test, ast.UnaryOp):
            if isinstance(test.op, ast.Not):
                # Compile to remove not operation
                return self.compileJumpIf(test.operand, next, not is_if_true)
        elif isinstance(test, ast.BoolOp):
            is_or = isinstance(test.op, ast.Or)
            skip_jump = next
            if is_if_true != is_or:
                skip_jump = self.newBlock()

            for node in test.values[:-1]:
                self.get_type(node).emit_jumpif(node, skip_jump, is_or, self)
                self.nextBlock()

            self.get_type(test.values[-1]).emit_jumpif(
                test.values[-1], next, is_if_true, self
            )
            self.nextBlock()

            if skip_jump is not next:
                self.nextBlock(skip_jump)
            return
        elif isinstance(test, ast.IfExp):
            end = self.newBlock("end")
            orelse = self.newBlock("orelse")
            # Jump directly to orelse if test matches
            self.get_type(test.test).emit_jumpif(test.test, orelse, False, self)
            # Jump directly to target if test is true and body is matches
            self.get_type(test.body).emit_jumpif(test.body, next, is_if_true, self)
            self.emit_noline("JUMP_FORWARD", end)
            # Jump directly to target if test is true and orelse matches
            self.nextBlock(orelse)
            self.get_type(test.orelse).emit_jumpif(test.orelse, next, is_if_true, self)
            self.nextBlock(end)
            return

        self.get_type(test).emit_jumpif(test, next, is_if_true, self)
        self.nextBlock()

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

    def perf_warning(self, msg: str, node: AST) -> None:
        return self.compiler.error_sink.perf_warning(msg, self.graph.filename, node)


StaticCodeGenerator = Static38CodeGenerator
