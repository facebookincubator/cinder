# pyre-unsafe
from __future__ import annotations

import ast
import sys
from ast import (
    AST,
    AnnAssign,
    Assign,
    AsyncFunctionDef,
    Attribute,
    Call,
    ClassDef,
    Constant,
    Delete,
    DictComp,
    ExceptHandler,
    For,
    FunctionDef,
    GeneratorExp,
    Global,
    If,
    Import,
    ImportFrom,
    Lambda,
    ListComp,
    Module,
    Name,
    NodeVisitor,
    Raise,
    SetComp,
    Str,
    Try,
    alias,
    arg,
    copy_location,
    expr,
    stmt,
)
from symtable import SymbolTable
from types import CodeType, ModuleType
from typing import (
    Dict,
    Generic,
    Iterable,
    List,
    Mapping,
    MutableMapping,
    Optional,
    Sequence,
    Set,
    TypeVar,
    Union,
    cast,
    final,
)

# pyre-fixme[21]: Could not find module `strict_modules.abstract`.
from strict_modules.abstract import NodeAttributes

# pyre-fixme[21]: Could not find module `strict_modules.common`.
from strict_modules.common import (
    FIXED_MODULES,
    AstRewriter,
    ScopeStack,
    SymbolMap,
    SymbolScope,
    get_symbol_map,
    imported_name,
    mangle_priv_name,
)


# We want to transform a module which looks something like:
#
# from strict_modules import cached_property
#
# x = 1
# def f():
#     return min(x, 20)
#
# class C:
#     @cached_property
#     def f(self): return 42
#
# C.foo = 42
#
# def g():
#     class C: pass
#     return C
#
# Into something like:
#
# min = <builtins>.min  # we have a fix set of builtins we execute against
#
# # We want some helper methods to be available to us:
# <strict-modules> = <fixed-modules>["strict_modules"]
# <freeze-type> = <strict-modules>["freeze_type"]
#
# # To freeze types we collect all of the classes in a list, and freeze
# # them at the end of a scope to allow mutation after creation.
# <classes> = []
# def <register-immutable>(cls):
#     <classes>.append(cls)
#
# x = 1
# def f():
#     return min(x, 20)
#
# @<register-immutable>
# @<init-cached-properties>({'f': '_f_impl'})
# class C:
#     __slots__ = ('f', )

#     def _f_impl(self):
#         return 42
#
#
# C.foo = 42
#
# def g():
#     <classes> = []
#     def <register-immutable>(cls):
#         <classes>.append(cls)
#     try:
#         class C: pass
#         return C
#     finally:
#         for <cls> in <classes>:
#             <freeze-type>(cls)
#         <classes>.clear()
#
#
# for <cls> in <classes>:
#     <freeze-type>(cls)
# <classes>.clear()
#
# # Top-level assigned names
#
# Note: we use non-valid Python identifeirs for anything which we introduce like
# <strict-module> and use valid Python identifiers for variables which will be accessible
# from within the module.


def make_arg(name: str) -> arg:
    return lineinfo(ast.arg(name, None))


TAst = TypeVar("TAst", bound=AST)


def lineinfo(node: TAst) -> TAst:
    # TODO: What effect do these line offsets have?
    node.lineno = 0
    node.col_offset = 0
    return node


def make_assign(*a: object, **kw: object) -> Assign:
    node = Assign(*a, **kw)
    if sys.version_info >= (3, 8):
        node.type_comment = None
    return node


def copyline(from_node: AST, to_node: TAst) -> TAst:
    to_node.lineno = from_node.lineno
    to_node.col_offset = from_node.col_offset
    return to_node


_IMPLICIT_GLOBALS = [
    "__name__",
    "__loader__",
    "__package__",
    "__spec__",
    "__path__",
    "__file__",
    "__cached__",
]


def make_function(name: str, pos_args: List[arg]) -> FunctionDef:
    func = lineinfo(ast.FunctionDef())
    func.name = name
    args = ast.arguments()
    args.kwonlyargs = []
    args.kw_defaults = []
    args.defaults = []
    args.args = pos_args
    args.posonlyargs = []
    func.args = args
    args.kwarg = None
    args.vararg = None
    func.decorator_list = []
    func.returns = None
    func.type_comment = ""
    return func


def make_assign_empty_list(name: str) -> Assign:
    return lineinfo(
        make_assign(
            [lineinfo(ast.Name(name, ast.Store()))],
            lineinfo(ast.List([], ast.Load())),
        )
    )


def is_assigned(name: str) -> str:
    return f"<assigned:{name}>"


GLOBALS_HELPER_ALIAS = "<globals-helper>"


def load_builtin(name: str) -> expr:
    if name == "globals":
        return lineinfo(ast.Name(GLOBALS_HELPER_ALIAS, ast.Load()))

    return lineinfo(
        ast.Subscript(
            lineinfo(ast.Name("<builtins>", ast.Load())),
            lineinfo(ast.Index(lineinfo(Constant(name)))),
            ast.Load(),
        )
    )


def make_raise(type: str, msg: str, cause: Optional[expr] = None) -> AST:
    return lineinfo(
        ast.Raise(
            lineinfo(ast.Call(load_builtin(type), [lineinfo(Constant(msg))], [])), cause
        )
    )


@final
class StrictModuleRewriter:
    """rewrites a module body so that all global variables are transformed into
    local variables, and are closed over by the enclosing functions.  This will
    ultimately remove all LOAD_GLOBAL/STORE_GLOBAL opcodes, and therefore will
    also have the side effect of making the module read-only as globals will
    not be exposed."""

    def __init__(
        self,
        root: Module,
        table: SymbolTable,
        filename: str,
        modname: str,
        mode: str,
        optimize: int,
        builtins: ModuleType | Mapping[str, object] = __builtins__,
        # pyre-fixme[11]: Annotation `NodeAttributes` is not defined as a type.
        node_attrs: Optional[Mapping[AST, NodeAttributes]] = None,
        is_static: bool = False,
        track_import_call: bool = False,
    ) -> None:
        if not isinstance(builtins, dict):
            builtins = builtins.__dict__

        self.root = root
        self.table = table
        self.filename = filename
        self.modname = modname
        self.mode = mode
        self.optimize = optimize
        self.node_attrs: Mapping[AST, NodeAttributes] = node_attrs or {}
        self.builtins: Mapping[str, object] = builtins
        # pyre-fixme[11]: Annotation `SymbolMap` is not defined as a type.
        self.symbol_map: SymbolMap = get_symbol_map(root, table)
        # pyre-fixme[11]: Annotation `SymbolScope` is not defined as a type.
        scope: SymbolScope[None, None] = SymbolScope(table, None)
        self.visitor = ImmutableVisitor(ScopeStack(scope, symbol_map=self.symbol_map))
        # Top-level statements in the returned code object...
        self.code_stmts: List[stmt] = []
        self.is_static = is_static
        self.track_import_call = track_import_call

    def transform(self) -> ast.Module:
        self.visitor.visit(self.root)
        self.visitor.global_sets.update(_IMPLICIT_GLOBALS)

        for argname in _IMPLICIT_GLOBALS:
            self.visitor.globals.add(argname)

        mod = ast.Module(
            [
                *self.get_future_imports(),
                *self.load_helpers(),
                *self.init_globals(),
                *self.transform_body(),
            ]
        )
        mod.type_ignores = []
        return mod

    def rewrite(self) -> CodeType:
        mod = self.transform()
        return compile(
            mod, self.filename, self.mode, dont_inherit=True, optimize=self.optimize
        )

    def load_helpers(self) -> Iterable[stmt]:
        helpers = [
            lineinfo(
                make_assign(
                    [lineinfo(ast.Name("<strict-modules>", ast.Store()))],
                    lineinfo(
                        ast.Subscript(
                            lineinfo(ast.Name("<fixed-modules>", ast.Load())),
                            lineinfo(ast.Index(lineinfo(Constant("strict_modules")))),
                            ast.Load(),
                        )
                    ),
                )
            ),
            lineinfo(
                make_assign(
                    [lineinfo(ast.Name("<freeze-type>", ast.Store()))],
                    lineinfo(
                        ast.Subscript(
                            lineinfo(ast.Name("<strict-modules>", ast.Load())),
                            lineinfo(ast.Index(lineinfo(Constant("freeze_type")))),
                            ast.Load(),
                        )
                    ),
                )
            ),
        ]
        if self.track_import_call:
            helpers.append(
                lineinfo(
                    make_assign(
                        [lineinfo(ast.Name("<track-import-call>", ast.Store()))],
                        lineinfo(
                            ast.Subscript(
                                lineinfo(ast.Name("<strict-modules>", ast.Load())),
                                lineinfo(
                                    ast.Index(lineinfo(Constant("track_import_call")))
                                ),
                                ast.Load(),
                            )
                        ),
                    )
                ),
            )
        return helpers

    def get_future_imports(self) -> Iterable[stmt]:
        if self.visitor.future_imports:
            yield lineinfo(
                ImportFrom("__future__", list(self.visitor.future_imports), 0)
            )

    def del_global(self, name: str) -> stmt:
        return lineinfo(ast.Delete([lineinfo(ast.Name(name, ast.Del()))]))

    def store_global(self, name: str, value: expr) -> stmt:
        return lineinfo(make_assign([lineinfo(ast.Name(name, ast.Store()))], value))

    def load_global(self, name: str) -> expr:
        return lineinfo(ast.Name(name, ast.Load()))

    def create_annotations(self) -> stmt:
        return self.store_global("__annotations__", lineinfo(ast.Dict([], [])))

    def make_transformer(
        self,
        # pyre-fixme[11]: Annotation `ScopeStack` is not defined as a type.
        scopes: ScopeStack[None, ScopeData],
    ) -> ImmutableTransformer:
        return ImmutableTransformer(
            scopes,
            self.modname,
            self.builtins,
            self.visitor.globals,
            self.visitor.global_sets,
            self.visitor.global_dels,
            self.visitor.future_imports,
            self.node_attrs,
            self.is_static,
            self.track_import_call,
        )

    def transform_body(self) -> Iterable[stmt]:
        scopes = ScopeStack(
            SymbolScope(self.table, ScopeData()), symbol_map=self.symbol_map
        )
        transformer = self.make_transformer(scopes)
        body = transformer.visit(self.root).body

        transformer.post_process_classes(body)
        return body

    def init_globals(self) -> Iterable[stmt]:
        for name in self.visitor.globals:
            if name in self.builtins:
                yield lineinfo(
                    make_assign(
                        [lineinfo(ast.Name(is_assigned(name), ast.Store()))],
                        lineinfo(ast.Constant(name in _IMPLICIT_GLOBALS)),
                    )
                )

            if name == "globals":
                # we need to provide access to the globals, we just grab __dict__
                # from our strict module object, which produces a new fresh snapshot
                # of the globals when accessed.
                yield from self.make_globals_function()
            elif name in self.builtins and name not in _IMPLICIT_GLOBALS:
                yield self.init_shadowed_builtin(name)

    def init_shadowed_builtin(self, name: str) -> stmt:
        return self.store_global(name, load_builtin(name))

    def make_globals_function(self) -> Iterable[stmt]:
        """Produces our faked out globals() which just grabs __dict__ from our
        strict module object where the actual work of collecting the globals
        comes from."""

        globals = make_function("globals", [])
        globals.body = [
            lineinfo(
                ast.Return(
                    lineinfo(
                        ast.Attribute(
                            lineinfo(ast.Name("<strict_module>", ast.Load())),
                            "__dict__",
                            ast.Load(),
                        )
                    )
                )
            )
        ]
        yield globals

        # Also save globals under an alias to handle the case where they can
        # be deleted and we need to restore them
        yield lineinfo(
            make_assign(
                [lineinfo(ast.Name(GLOBALS_HELPER_ALIAS, ast.Store()))],
                lineinfo(ast.Name("globals", ast.Load())),
            )
        )


def rewrite(
    root: Module,
    table: SymbolTable,
    filename: str,
    modname: str,
    mode: str,
    optimize: int = -1,
    builtins: ModuleType | Mapping[str, object] = __builtins__,
    node_attrs: Optional[Mapping[AST, NodeAttributes]] = None,
    track_import_call: bool = False,
) -> CodeType:
    return StrictModuleRewriter(
        root,
        table,
        filename,
        modname,
        mode,
        optimize,
        builtins,
        node_attrs,
        track_import_call=track_import_call,
    ).rewrite()


TTransformedStmt = Union[Optional[AST], List[AST]]
TVar = TypeVar("TScope")
TScopeData = TypeVar("TData")


class SymbolVisitor(Generic[TVar, TScopeData], NodeVisitor):
    def __init__(self, scopes: ScopeStack[TVar, TScopeData]) -> None:
        self.scopes = scopes

    def is_global(self, name: str) -> bool:
        return self.scopes.is_global(name)

    def scope_for(self, name: str) -> SymbolScope[TVar, TScopeData]:
        return self.scopes.scope_for(name)

    def visit_Comp_Outer(
        self,
        node: ListComp | SetComp | GeneratorExp | DictComp,
        update: bool = False,
    ) -> None:
        iter = self.visit(node.generators[0].iter)
        if update:
            node.generators[0].iter = iter

    def visit_Try(self, node: Try) -> TTransformedStmt:
        # Need to match the order the symbol visitor constructs these in, which
        # walks orelse before handlers.
        for val in node.body:
            self.visit(val)

        for val in node.orelse:
            self.visit(val)

        for val in node.handlers:
            self.visit(val)

        for val in node.finalbody:
            self.visit(val)

        return node

    def visit_Comp_Inner(
        self,
        node: ListComp | SetComp | GeneratorExp | DictComp,
        update: bool = False,
        scope_node: ListComp | SetComp | GeneratorExp | DictComp | None = None,
    ) -> None:
        scope_node = scope_node or node
        with self.scopes.with_node_scope(scope_node):
            if isinstance(node, DictComp):
                key = self.visit(node.key)
                if update:
                    node.key = key
                value = self.visit(node.value)
                if update:
                    node.value = value
            else:
                elt = self.visit(node.elt)
                if update:
                    node.elt = elt

            self.walk_many(node.generators[0].ifs, update)

            target = self.visit(node.generators[0].target)
            if update:
                # pyre-fixme[16]: `DictComp` has no attribute `target`.
                # pyre-fixme[16]: `GeneratorExp` has no attribute `target`.
                # pyre-fixme[16]: `ListComp` has no attribute `target`.
                # pyre-fixme[16]: `SetComp` has no attribute `target`.
                node.target = target

            gens = node.generators[1:]
            self.walk_many(gens, update)
            if update:
                node.generators[1:] = gens

    def visit_Func_Outer(
        self, node: AsyncFunctionDef | FunctionDef | Lambda, update: bool = False
    ) -> None:
        args = self.visit(node.args)
        if update:
            node.args = args

        if isinstance(node, (AsyncFunctionDef, FunctionDef)):
            retnode = node.returns
            if retnode:
                returns = self.visit(retnode)
                if update:
                    node.returns = returns

            self.walk_many(node.decorator_list, update)

    def visit_Func_Inner(
        self,
        node: AsyncFunctionDef | FunctionDef | Lambda,
        update: bool = False,
        scope_node: AsyncFunctionDef | FunctionDef | Lambda | None = None,
    ) -> SymbolScope[TVar, TScopeData]:
        scope_node = scope_node or node
        with self.scopes.with_node_scope(scope_node) as next:
            assert isinstance(node, Lambda) or node.name == next.symbols.get_name()

            # visit body in function scope
            if isinstance(node, Lambda):
                new = self.visit(node.body)
                if update:
                    node.body = new
            else:
                self.walk_many(node.body, update)

            return next

    def visit_Class_Outer(self, node: ClassDef, update: bool = False) -> None:
        self.walk_many(node.bases, update)
        self.walk_many(node.keywords, update)
        self.walk_many(node.decorator_list, update)

    def visit_Class_Inner(
        self,
        node: ClassDef,
        update: bool = False,
        scope_node: Optional[ClassDef] = None,
    ) -> SymbolScope[TVar, TScopeData]:
        scope_node = scope_node or node
        with self.scopes.with_node_scope(scope_node) as next:
            assert node.name == next.symbols.get_name()

            self.walk_many(node.body, update)

            return next

    def walk_many(
        self,
        stmts: List[TAst],
        update: bool = False,
    ) -> None:
        new_stmts = []
        for statement in stmts:
            new = self.visit(statement)
            if isinstance(new, ast.AST):
                new_stmts.append(new)
            elif new is not None:
                new_stmts.extend(new)
        if update:
            stmts[:] = new_stmts

    def visit_Lambda(self, node: Lambda) -> TTransformedStmt:
        self.visit_Func_Outer(node)

        self.visit_Func_Inner(node)

    def visit_comp(
        self, node: ListComp | SetComp | DictComp | GeneratorExp
    ) -> Optional[expr]:
        self.visit_Comp_Outer(node)

        self.visit_Comp_Inner(node)

        return node

    def visit_ListComp(self, node: ListComp) -> Optional[expr]:
        return self.visit_comp(node)

    def visit_SetComp(self, node: SetComp) -> Optional[expr]:
        return self.visit_comp(node)

    def visit_GeneratorExp(self, node: GeneratorExp) -> Optional[expr]:
        return self.visit_comp(node)

    def visit_DictComp(self, node: DictComp) -> Optional[expr]:
        return self.visit_comp(node)


@final
class ImmutableVisitor(SymbolVisitor[None, None]):
    def __init__(self, scopes: ScopeStack[None, None]) -> None:
        super().__init__(scopes)
        self.globals: Set[str] = set()
        self.global_sets: Set[str] = set()
        self.global_dels: Set[str] = set()
        self.future_imports: Set[alias] = set()

    def load_name(self, name: str) -> None:
        if self.is_global(name):
            self.globals.add(name)

    def store_name(self, name: str) -> None:
        if self.is_global(name):
            self.globals.add(name)
            self.global_sets.add(name)

    def del_name(self, name: str) -> None:
        if self.is_global(name):
            self.globals.add(name)
            self.global_sets.add(name)
            self.global_dels.add(name)

    def visit_Global(self, node: Global) -> None:
        for name in node.names:
            self.globals.add(name)

    def visit_Name(self, node: Name) -> None:
        if isinstance(node.ctx, ast.Load):
            self.load_name(node.id)
        elif isinstance(node.ctx, ast.Store):
            self.store_name(node.id)
        elif isinstance(node.ctx, ast.Del):
            self.del_name(node.id)

    def visit_ImportFrom(self, node: ImportFrom) -> None:
        if node.module == "__future__":
            self.future_imports.update(node.names)

        for name in node.names:
            self.store_name(name.asname or name.name)

    def visit_Import(self, node: Import) -> None:
        for name in node.names:
            self.store_name(imported_name(name))

    def visit_Call(self, node: Call) -> None:
        func = node.func
        if isinstance(func, ast.Name):
            # We don't currently allow aliasing or shadowing exec/eval
            # so this check is currently sufficient.
            if (func.id == "exec" or func.id == "eval") and len(node.args) < 2:
                # We'll need access to our globals() helper when we transform
                # the ast
                self.globals.add("globals")
                self.globals.add("locals")
        self.generic_visit(node)

    def visit_Try(self, node: Try) -> None:
        super().visit_Try(node)

        for handler in node.handlers:
            name = handler.name
            if name is None:
                continue
            self.del_name(name)

    def visit_ClassDef(self, node: ClassDef) -> None:
        self.visit_Class_Outer(node)

        self.store_name(node.name)

        self.visit_Class_Inner(node)

    def visit_FunctionDef(self, node: FunctionDef) -> None:
        self.visit_Func_Outer(node)

        self.store_name(node.name)

        self.visit_Func_Inner(node)

    def visit_AsyncFunctionDef(self, node: AsyncFunctionDef) -> None:
        self.visit_Func_Outer(node)

        self.store_name(node.name)

        self.visit_Func_Inner(node)


class ScopeData:
    def __init__(self) -> None:
        self.has_classdefs = False

    def visit_Assign(self, node: Assign) -> None:
        pass

    def visit_AnnAssign(self, node: AnnAssign) -> None:
        pass


@final
class ClassScope(ScopeData):
    def __init__(self, node: ClassDef) -> None:
        super().__init__()
        self.node = node
        self.instance_fields: Set[str] = set()
        self.cached_props: Dict[str, object] = {}

    def visit_AnnAssign(self, node: AnnAssign) -> None:
        target = node.target
        if node.value is None and isinstance(target, Name):
            self.instance_fields.add(target.id)


@final
class FunctionScope(ScopeData):
    def __init__(self, node: AsyncFunctionDef | FunctionDef, parent: ScopeData) -> None:
        super().__init__()
        self.node = node
        self.parent = parent

    def visit_AnnAssign(self, node: AnnAssign) -> None:
        parent = self.parent
        target = node.target
        if (
            self.node.name == "__init__"
            and self.node.args.args
            and isinstance(parent, ClassScope)
            and isinstance(target, Attribute)
        ):
            self.add_attr_name(target, parent)

    def add_attr_name(self, target: Attribute, parent: ClassScope) -> None:
        """records self.name = ... when salf matches the 1st parameter"""
        value = target.value
        node = self.node
        if isinstance(value, Name) and value.id == node.args.args[0].arg:
            parent.instance_fields.add(target.attr)

    def assign_worker(self, targets: Sequence[AST], parent: ClassScope) -> None:
        for target in targets:
            if isinstance(target, Attribute):
                self.add_attr_name(target, parent)
            elif isinstance(target, (ast.Tuple, ast.List)):
                self.assign_worker(target.elts, parent)

    def visit_Assign(self, node: Assign) -> None:
        parent = self.parent
        if (
            self.node.name == "__init__"
            and isinstance(parent, ClassScope)
            and self.node.args.args
        ):
            self.assign_worker(node.targets, parent)


@final
# pyre-fixme[11]: Annotation `AstRewriter` is not defined as a type.
class ImmutableTransformer(SymbolVisitor[None, ScopeData], AstRewriter):
    def __init__(
        self,
        symbols: ScopeStack[None, ScopeData],
        modname: str,
        builtins: Mapping[str, object],
        globals: Set[str],
        global_sets: Set[str],
        global_dels: Set[str],
        future_imports: Set[alias],
        node_attrs: Mapping[AST, NodeAttributes],
        is_static: bool,
        track_import_call: bool,
    ) -> None:
        super().__init__(symbols)
        symbols.scope_factory = self.make_scope
        self.modname = modname
        self.builtins = builtins
        self.globals = globals
        self.global_sets = global_sets
        self.global_dels = global_dels
        self.future_imports = future_imports
        self.node_attrs = node_attrs
        self.is_static = is_static
        self.track_import_call = track_import_call

    def make_scope(
        self,
        symtable: SymbolTable,
        node: AST,
        vars: Optional[MutableMapping[str, None]] = None,
    ) -> SymbolScope[None, ScopeData]:
        if isinstance(node, (FunctionDef, AsyncFunctionDef)):
            data: ScopeData = FunctionScope(node, self.scopes.scopes[-1].scope_data)
        elif isinstance(node, ClassDef):
            data: ScopeData = ClassScope(node)
        else:
            data: ScopeData = ScopeData()
        return SymbolScope(symtable, data)

    def call_builtin(self, line_node: AST, name: str, args: List[arg]) -> expr:
        return copyline(
            line_node,
            ast.Call(copyline(line_node, ast.Name(name, ast.Load())), args, []),
        )

    def visit_Raise(self, node: Raise) -> Optional[AST]:
        return self.generic_visit(node)

    def visit_Call(self, node: Call) -> Optional[AST]:
        node = self.generic_visit(node)
        func = node.func
        if isinstance(func, ast.Name):
            # We don't currently allow aliasing or shadowing exec/eval
            # so this check is currently sufficient.
            if func.id == "exec" or func.id == "eval":
                # exec and eval don't take keyword args, so we only need to
                # inspect the normal arguments
                if len(node.args) < 2:
                    # pyre-fixme[16]: `ImmutableTransformer` has no attribute
                    #  `clone_node`.
                    node = self.clone_node(node)
                    # we're implicitly capturing globals, make it explicit so
                    # that we get our globals dictionary.  exec/eval won't be
                    # able to mutate them though.  We also need to explicitly
                    # call locals() here because the behavior of exec/eval is
                    # to use globals as locals if it is explicitly supplied.
                    node.args.append(self.call_builtin(node.args[0], "globals", []))
                    node.args.append(self.call_builtin(node.args[0], "locals", []))

        return node

    def get_extra_assigns(self, target: AST) -> Optional[List[stmt]]:
        if (
            isinstance(target, ast.Name)
            and target.id in self.builtins
            and target.id in self.global_dels
            and self.is_global(target.id)
        ):
            update = cast(stmt, self.update_del_state(target.id, True))
            return [update]
        elif isinstance(target, (ast.List, ast.Tuple)):
            extras = []
            for item in target.elts:
                assigns = self.get_extra_assigns(item)
                if assigns is not None:
                    extras.extend(assigns)
            return extras

        return None

    def visit_For(self, node: For) -> Optional[AST]:
        extras = self.get_extra_assigns(node.target)

        target = self.visit(node.target)
        iter = self.visit(node.iter)
        # pyre-fixme[16]: `ImmutableTransformer` has no attribute `walk_list`.
        body = self.walk_list(node.body)
        orelse = self.walk_list(node.orelse)
        if extras:
            body = body + extras

        # pyre-fixme[16]: `ImmutableTransformer` has no attribute `update_node`.
        return self.update_node(
            node, target=target, iter=iter, body=body, orelse=orelse
        )

    def gen_del_check(self, name: str) -> Optional[AST]:
        if name in self.global_dels:
            return lineinfo(
                ast.If(
                    lineinfo(
                        ast.UnaryOp(
                            ast.Not(), lineinfo(ast.Name(is_assigned(name), ast.Load()))
                        )
                    ),
                    [make_raise("NameError", f"name '{name}' is not defined")],
                    [],
                )
            )
        return None

    def update_del_state(self, name: str, assigned: bool = False) -> Assign:
        return lineinfo(
            make_assign(
                [lineinfo(ast.Name(is_assigned(name), ast.Store()))],
                lineinfo(ast.Constant(assigned)),
            )
        )

    def get_handler_tracker(self, handler: ExceptHandler) -> str:
        """gets a variable name to track exception state for aliased global"""
        handler_type = handler.type
        if isinstance(handler_type, Name):
            desc = handler_type.id
        elif handler.type is None:
            desc = "bare"
        else:
            desc = "complex"

        return f"<{desc} {handler.name} at {str(id(handler))}>"

    def visit_Try(self, node: Try) -> TTransformedStmt:
        """If an except block aliases a built-in's state based upon how the
        exception propgated.

        When we enter the except block the built-in will be aliased to the exception.
        When we leave the except block the built-in will be restored to the built-in value.
        If no exception is raised the built-in value is not modified.

        Because except blocks clear the name after leaving them this also registers
        it as a global delete."""
        # pyre-fixme[16]: `ImmutableTransformer` has no attribute `walk_list`.
        body = self.walk_list(node.body)
        orelse = self.walk_list(node.orelse)
        handlers = self.walk_list(node.handlers)
        finalbody = self.walk_list(node.finalbody)

        for i, (old_handler, handler) in enumerate(zip(node.handlers, handlers)):
            handler_name = handler.name
            if handler_name is None or not self.is_global(handler_name):
                continue

            if handler_name not in self.builtins:
                continue

            self.global_dels.add(handler_name)
            tracker = self.get_handler_tracker(handler)

            if body is node.body:
                body = list(body)

            # mark except as not taken before entering the try
            body.insert(
                0,
                lineinfo(
                    make_assign(
                        [lineinfo(Name(tracker, ast.Store()))],
                        lineinfo(Constant(False)),
                    )
                ),
            )

            # Mark except as taken when entering the except
            if old_handler is handler:
                if handlers is node.handlers:
                    handlers = list(node.handlers)

                # pyre-fixme[16]: `ImmutableTransformer` has no attribute `clone_node`.
                handlers[i] = handler = self.clone_node(old_handler)

            if old_handler.body is handler.body:
                handler.body = list(handler.body)

            handler.body.insert(
                0,
                lineinfo(
                    make_assign(
                        [lineinfo(Name(tracker, ast.Store()))],
                        lineinfo(Constant(True)),
                    )
                ),
            )

            if finalbody is node.finalbody:
                finalbody = list(node.finalbody)

            # restore the handler variable if necessary, and cleanup our
            # tracking variable on the way out
            finalbody[0:0] = [
                lineinfo(
                    If(
                        lineinfo(Name(tracker, ast.Load())),
                        [self.restore_builtin(handler_name)],
                        [],
                    )
                ),
                lineinfo(Delete([lineinfo(Name(tracker, ast.Del()))])),
            ]

        # pyre-fixme[16]: `ImmutableTransformer` has no attribute `update_node`.
        return self.update_node(
            node, body=body, orelse=orelse, handlers=handlers, finalbody=finalbody
        )

    def visit_Assign(self, node: Assign) -> TTransformedStmt:
        self.scopes.scopes[-1].scope_data.visit_Assign(node)
        # pyre-fixme[16]: `ImmutableTransformer` has no attribute `update_node`.
        node = self.update_node(
            node,
            # pyre-fixme[16]: `ImmutableTransformer` has no attribute `walk_list`.
            targets=self.walk_list(node.targets),
            value=self.visit(node.value),
        )

        res: Optional[List[AST]] = None

        for target in node.targets:
            extras = self.get_extra_assigns(target)
            if extras is not None:
                if res is None:
                    # pyre-fixme[35]: Target cannot be annotated.
                    res: List[AST] = [node]
                res.extend(extras)

        if res is not None:
            return res

        return node

    def visit_AnnAssign(self, node: AnnAssign) -> TTransformedStmt:
        self.scopes.scopes[-1].scope_data.visit_AnnAssign(node)
        return self.generic_visit(node)

    def restore_builtin(self, name: str) -> Assign:
        restore_to = load_builtin(name)
        return lineinfo(
            make_assign([lineinfo(ast.Name(name, ast.Store()))], restore_to)
        )

    def visit_Delete(self, node: Delete) -> TTransformedStmt:
        for target in node.targets:
            if (
                isinstance(target, ast.Name)
                and target.id in self.builtins
                and self.is_global(target.id)
            ):
                break
        else:
            # no global deletes
            # pyre-fixme[16]: `ImmutableTransformer` has no attribute `update_node`.
            # pyre-fixme[16]: `ImmutableTransformer` has no attribute `walk_list`.
            return self.update_node(node, targets=self.walk_list(node.targets))

        stmts: List[AST] = []
        for target in node.targets:
            if isinstance(target, ast.Name):
                if target.id in self.builtins and self.is_global(target.id):
                    # Transform a builtin delete into restoring the builtin value
                    # If we can potentially delete the builtin value then we
                    # also need to track whether or not it has been deleted, this
                    # gives the right error for:
                    #   min = 42
                    #   del min
                    #   del min
                    del_check = self.gen_del_check(target.id)
                    if del_check is not None:
                        stmts.append(del_check)
                    stmts.append(self.restore_builtin(target.id))
                    if target.id in self.global_dels:
                        stmts.append(self.update_del_state(target.id))
                    continue

            # preserve this deletion
            stmts.append(ast.Delete([self.visit(target)]))

        return stmts

    def post_process_classes(self, body: List[stmt], mark_now: bool = True) -> None:
        body[0:0] = [
            make_assign_empty_list("<classes>"),
            self.make_immutable_register(),
        ]

        if mark_now:
            # Immediately clear at the end of the function
            body.extend(self.make_immutable_marker_stmts())

    def make_immutable_register(self) -> FunctionDef:
        """Create a decorator which is used to capture the class so that we can
        add it to a list so we can mark it as immutable later"""

        register = make_function("<register-immutable>", [make_arg("cls")])
        register.body = [
            lineinfo(
                ast.Expr(
                    lineinfo(
                        ast.Call(
                            lineinfo(
                                ast.Attribute(
                                    lineinfo(ast.Name("<classes>", ast.Load())),
                                    "append",
                                    ast.Load(),
                                )
                            ),
                            [lineinfo(ast.Name("cls", ast.Load()))],
                            [],
                        )
                    )
                )
            ),
            lineinfo(ast.Return(lineinfo(ast.Name("cls", ast.Load())))),
        ]
        return register

    def make_immutable_marker(self, has_classdefs: bool) -> List[FunctionDef]:
        """Produces a function which can be used to support lazy initialization
        combined with marking types as frozen"""

        if not has_classdefs:
            return []

        marker = make_function("<mark-immutable>", [])
        marker.body = self.make_immutable_marker_stmts()
        return [marker]

    def make_immutable_marker_stmts(self) -> List[stmt]:
        # Generates the logic to mark types as frozen.  This is just a loop over
        # the classes which have been produced.
        #
        # for <cls> in <classes>:
        #     <freeze-type>(<cls>)
        # <cls>.clear()
        for_loop = lineinfo(
            ast.For(
                lineinfo(ast.Name("<cls>", ast.Store())),
                lineinfo(ast.Name("<classes>", ast.Load())),
                [
                    lineinfo(
                        ast.Expr(
                            lineinfo(
                                ast.Call(
                                    lineinfo(ast.Name("<freeze-type>", ast.Load())),
                                    [lineinfo(ast.Name("<cls>", ast.Load()))],
                                    [],
                                )
                            )
                        )
                    )
                ],
                [],
            )
        )
        for_loop.type_comment = ""

        return [
            for_loop,
            lineinfo(
                ast.Expr(
                    lineinfo(
                        ast.Call(
                            lineinfo(
                                ast.Attribute(
                                    lineinfo(ast.Name("<classes>", ast.Load())),
                                    "clear",
                                    ast.Load(),
                                )
                            ),
                            [],
                            [],
                        )
                    )
                )
            ),
        ]

    def make_base_class_dict_test_stmts(
        self, bases: List[TAst], instance_fields: Set[str]
    ) -> stmt:
        slots_stmt = self.make_slots_stmt(instance_fields)
        # if there are non-names in the bases of the class, give up and just create slots
        if not all(isinstance(b, ast.Name) for b in bases):
            return slots_stmt
        # if __dict__ is not added to instance fields (no loose slots), just slotify
        if "__dict__" not in instance_fields:
            return slots_stmt
        # generate code that decide whether __dict__ should be included
        # if any('__dict__' in getattr(_t, '__dict__', ()) for b in <bases> for _t in b.mro()):
        #   __slots__ = <slots without __dict__>
        # else:
        #   __slots__ = <slots with __dict__>
        names = [lineinfo(ast.Name(cast(ast.Name, n).id, ast.Load())) for n in bases]
        condition = lineinfo(
            ast.Call(
                lineinfo(ast.Name("any", ast.Load())),
                [
                    lineinfo(
                        ast.GeneratorExp(
                            lineinfo(
                                ast.Compare(
                                    lineinfo(Constant("__dict__")),
                                    [lineinfo(ast.In())],
                                    [
                                        lineinfo(
                                            ast.Call(
                                                lineinfo(
                                                    ast.Name("getattr", ast.Load())
                                                ),
                                                [
                                                    lineinfo(
                                                        ast.Name("_t", ast.Load())
                                                    ),
                                                    lineinfo(Constant("__dict__")),
                                                    lineinfo(ast.Tuple([], ast.Load())),
                                                ],
                                                [],
                                            )
                                        )
                                    ],
                                )
                            ),
                            [
                                lineinfo(
                                    ast.comprehension(
                                        lineinfo(ast.Name("b", ast.Store())),
                                        lineinfo(ast.List(names, ast.Load())),
                                        [],
                                        0,
                                    )
                                ),
                                lineinfo(
                                    ast.comprehension(
                                        lineinfo(ast.Name("_t", ast.Store())),
                                        lineinfo(
                                            ast.Call(
                                                lineinfo(
                                                    ast.Attribute(
                                                        lineinfo(
                                                            ast.Name("b", ast.Load())
                                                        ),
                                                        "mro",
                                                        ast.Load(),
                                                    )
                                                ),
                                                [],
                                                [],
                                            )
                                        ),
                                        [],
                                        0,
                                    )
                                ),
                            ],
                        )
                    )
                ],
                [],
            )
        )
        slots_stmt_without_dict = self.make_slots_stmt(instance_fields - {"__dict__"})
        return lineinfo(ast.If(condition, [slots_stmt_without_dict], [slots_stmt]))

    def make_slots_stmt(self, instance_fields: Set[str]) -> Assign:
        return lineinfo(
            make_assign(
                [lineinfo(Name("__slots__", ast.Store()))],
                lineinfo(
                    ast.Tuple(
                        [lineinfo(Str(name)) for name in instance_fields], ast.Load()
                    )
                ),
            )
        )

    def visit_ClassDef(self, node: ClassDef) -> TTransformedStmt:
        outer_scope_data = self.scopes.scopes[-1].scope_data
        outer_scope_data.has_classdefs = True
        orig_node = node
        # pyre-fixme[16]: `ImmutableTransformer` has no attribute `clone_node`.
        node = self.clone_node(node)
        self.visit_Class_Outer(node, True)
        class_scope = self.visit_Class_Inner(node, True, scope_node=orig_node)
        scope_data = class_scope.scope_data
        # lint-fixme: NoAssertsRule
        assert isinstance(scope_data, ClassScope), type(class_scope).__name__

        attrs = self.node_attrs.get(orig_node)
        if attrs is None or not attrs.mutable:
            node.decorator_list.append(
                lineinfo(ast.Name("<register-immutable>", ast.Load()))
            )

        slots_enabled = (
            attrs is not None and not attrs.slots_disabled
        ) and not self.is_static
        if slots_enabled:
            if attrs is not None:
                if attrs.loose_slots:
                    scope_data.instance_fields.add("__dict__")
                    scope_data.instance_fields.add("__loose_slots__")
                if attrs.extra_slots:
                    scope_data.instance_fields.update(attrs.extra_slots)

            node.body.append(
                self.make_base_class_dict_test_stmts(
                    node.bases, scope_data.instance_fields
                )
            )

        if scope_data.cached_props and slots_enabled:
            # Add a decorator which replaces our name-mangled cache properties
            # with a class-level decorator that converts them.  We apply it as
            # a decorator so that we get to initialize these before other
            # decorators see the class.
            node.decorator_list.append(
                self.make_cached_property_init_decorator(scope_data, class_scope)
            )

        if self.is_global(node.name):
            return [cast(AST, node), self.update_del_state(node.name, True)]

        return node

    def make_cached_property_init_decorator(
        self,
        scope_data: ClassScope,
        # pyre-fixme[34]
        class_scope: SymbolScope[TVar, TScopeData],
    ) -> expr:
        return lineinfo(
            ast.Call(
                lineinfo(ast.Name("<init-cached-properties>", ast.Load())),
                [
                    lineinfo(
                        ast.Dict(
                            [
                                lineinfo(
                                    Constant(mangle_priv_name(name, [class_scope]))
                                )
                                for name in scope_data.cached_props
                            ],
                            [
                                lineinfo(
                                    ast.Tuple(
                                        [
                                            lineinfo(
                                                Constant(
                                                    mangle_priv_name(
                                                        self.mangle_cached_prop(name),
                                                        [class_scope],
                                                    )
                                                )
                                            ),
                                            lineinfo(Constant(value)),
                                        ],
                                        ast.Load(),
                                    )
                                )
                                for name, value in scope_data.cached_props.items()
                            ],
                        )
                    )
                ],
                [],
            )
        )

    def mangle_cached_prop(self, name: str) -> str:
        return "_" + name + "_impl"

    def _create_track_import_call(self) -> ast.Expr:
        return lineinfo(
            ast.Expr(
                lineinfo(
                    Call(
                        lineinfo(ast.Name("<track-import-call>", ctx=ast.Load())),
                        [lineinfo(ast.Constant(self.modname))],
                        [],
                    )
                )
            )
        )

    def visit_FunctionDef(self, node: FunctionDef) -> TTransformedStmt:
        outer_scope = self.scopes.scopes[-1].scope_data
        orig_node = node
        # pyre-fixme[16]: `ImmutableTransformer` has no attribute `clone_node`.
        node = self.clone_node(node)
        self.visit_Func_Outer(node, True)

        scope_data = self.visit_Func_Inner(node, True, scope_node=orig_node).scope_data

        res: TTransformedStmt = node
        if self.is_global(node.name):
            res = [cast(AST, node), self.update_del_state(node.name, True)]

        self.check_cached_prop(orig_node, node, outer_scope)

        if scope_data.has_classdefs:
            # We have children class defs, we need to mark them as immutable
            # when leaving the scope.
            self.post_process_classes(node.body)
            node.body = [
                copyline(
                    orig_node.body[0],
                    ast.Try(node.body, [], [], [*self.make_immutable_marker_stmts()]),
                )
            ]
        if self.track_import_call:
            node.body.insert(0, self._create_track_import_call())

        return res

    def visit_AsyncFunctionDef(self, node: AsyncFunctionDef) -> TTransformedStmt:
        outer_scope = self.scopes.scopes[-1].scope_data
        orig_node = node
        # pyre-fixme[16]: `ImmutableTransformer` has no attribute `clone_node`.
        node = self.clone_node(node)
        self.visit_Func_Outer(node, True)

        self.visit_Func_Inner(node, True, scope_node=orig_node)

        orig_name = node.name
        self.check_cached_prop(orig_node, node, outer_scope)

        if self.track_import_call:
            node.body.insert(0, self._create_track_import_call())

        if self.is_global(orig_name):
            return [cast(AST, node), self.update_del_state(orig_name, True)]

        return node

    def check_cached_prop(
        self,
        orig_node: AsyncFunctionDef | FunctionDef,
        node: AsyncFunctionDef | FunctionDef,
        outer_scope: ScopeData,
    ) -> None:
        func_attrs = self.node_attrs.get(orig_node)

        if isinstance(outer_scope, ClassScope):
            cls_attrs = self.node_attrs.get(outer_scope.node)
            if (
                cls_attrs is not None
                and not cls_attrs.slots_disabled
                and func_attrs is not None
                and func_attrs.cached_props
            ):
                for dec in node.decorator_list:
                    dec_attrs = self.node_attrs.get(dec)
                    if dec_attrs is None:
                        continue

                    if dec_attrs.abs_value in func_attrs.cached_props:
                        # @cached_property decorator, we transform this into a
                        # slot entry and the cached property reads/writes to
                        # the slot
                        outer_scope.instance_fields.add(node.name)
                        outer_scope.cached_props[node.name] = func_attrs.cached_props[
                            dec_attrs.abs_value
                        ]
                        node.name = self.mangle_cached_prop(node.name)
                        node.decorator_list = [
                            d for d in node.decorator_list if d is not dec
                        ]
                        break

    def visit_Lambda(self, node: Lambda) -> TTransformedStmt:
        orig_node = node
        # pyre-fixme[16]: `ImmutableTransformer` has no attribute `clone_node`.
        node = self.clone_node(node)
        self.visit_Func_Outer(node, True)

        self.visit_Func_Inner(node, True, scope_node=orig_node)

        return node

    def visit_ImportFrom(self, node: ImportFrom) -> TTransformedStmt:
        if node.module == "__future__":
            # We push these to the top of the module where they're required to be
            return None

        if node.level == 0 and node.module is not None:
            mod_name = node.module
            mod = None
            if mod_name is not None:
                mod = FIXED_MODULES.get(mod_name)
            if mod is None:
                return node

            assigns: List[AST] = []
            # Load the module into a temporary...
            assigns.append(
                copy_location(
                    make_assign(
                        [copy_location(ast.Name("<tmp-module>", ast.Store()), node)],
                        copy_location(
                            ast.Subscript(
                                copy_location(
                                    ast.Name("<fixed-modules>", ast.Load()), node
                                ),
                                copy_location(
                                    ast.Index(
                                        copy_location(Constant(node.module), node)
                                    ),
                                    node,
                                ),
                                ast.Load(),
                            ),
                            node,
                        ),
                    ),
                    node,
                )
            )

            # Store all of the imported names from the module
            new_names = []
            for _i, name in enumerate(node.names):
                value = mod.get(name.name)
                if value is not None:
                    assigns.append(
                        copy_location(
                            make_assign(
                                [
                                    copy_location(
                                        ast.Name(name.asname or name.name, ast.Store()),
                                        node,
                                    )
                                ],
                                copy_location(
                                    ast.Subscript(
                                        copy_location(
                                            ast.Name("<tmp-module>", ast.Load()), node
                                        ),
                                        copy_location(
                                            ast.Index(
                                                copy_location(Constant(name.name), node)
                                            ),
                                            node,
                                        ),
                                        ast.Load(),
                                    ),
                                    node,
                                ),
                            ),
                            node,
                        )
                    )
                else:
                    new_names.append(name)

            # And then delete the temporary
            assigns.append(
                copy_location(
                    ast.Delete(
                        [copy_location(ast.Name("<tmp-module>", ast.Del()), node)]
                    ),
                    node,
                )
            )
            if new_names:
                # We have names we don't know about, keep them around...
                # pyre-fixme[16]: `ImmutableTransformer` has no attribute `clone_node`.
                node = self.clone_node(node)
                node.names = new_names
                assigns.insert(0, node)
            return assigns
        return node

    def visit_comp(
        self, node: ListComp | SetComp | DictComp | GeneratorExp
    ) -> Optional[expr]:
        orig_node = node
        # pyre-fixme[16]: `ImmutableTransformer` has no attribute `clone_node`.
        node = self.clone_node(node)
        self.visit_Comp_Outer(node, True)

        self.visit_Comp_Inner(node, True, scope_node=orig_node)

        return node
