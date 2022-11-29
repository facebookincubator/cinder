# Copyright (c) Meta Platforms, Inc. and affiliates. (http://www.meta.com)

from __future__ import annotations

import ast
from ast import (
    alias,
    AnnAssign,
    arg,
    Assign,
    AST,
    AsyncFunctionDef,
    Attribute,
    Call,
    ClassDef,
    Constant,
    copy_location,
    DictComp,
    expr,
    FunctionDef,
    GeneratorExp,
    Global,
    Import,
    ImportFrom,
    Lambda,
    ListComp,
    Module,
    Name,
    NodeVisitor,
    SetComp,
    stmt,
    Str,
    Try,
)
from symtable import SymbolTable
from types import ModuleType
from typing import (
    cast,
    Dict,
    final,
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
)

from ..common import (
    AstRewriter,
    get_symbol_map,
    imported_name,
    lineinfo,
    mangle_priv_name,
    ScopeStack,
    SymbolMap,
    SymbolScope,
)
from ..preprocessor import (
    ALL_INDICATORS,
    get_cached_prop_value,
    get_extra_slots,
    is_indicator_dec,
    is_loose_slots,
    is_mutable,
    is_strict_slots,
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
#
# x = 1
# def f():
#     return min(x, 20)
#
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
#     class C: pass
#     return C
#     <classes> = []
#
#
#
# # Top-level assigned names
#
# Note: we use non-valid Python identifeirs for anything which we introduce like
# <strict-module> and use valid Python identifiers for variables which will be accessible
# from within the module.


def make_arg(name: str) -> arg:
    return lineinfo(ast.arg(name, None))


TAst = TypeVar("TAst", bound=AST)


def make_assign(*a: object, **kw: object) -> Assign:
    node = Assign(*a, **kw)
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
        self.builtins: Mapping[str, object] = builtins
        self.symbol_map: SymbolMap = get_symbol_map(root, table)
        scope: SymbolScope[None, None] = SymbolScope(table, None)
        self.visitor = ImmutableVisitor(
            ScopeStack(scope, symbol_map=self.symbol_map), ALL_INDICATORS
        )
        # Top-level statements in the returned code object...
        self.code_stmts: List[stmt] = []
        self.is_static = is_static
        self.track_import_call = track_import_call

    def transform(self) -> ast.Module:
        original_first_node = self.root.body[0] if self.root.body else None
        self.visitor.visit(self.root)
        self.visitor.global_sets.update(_IMPLICIT_GLOBALS)

        for argname in _IMPLICIT_GLOBALS:
            self.visitor.globals.add(argname)

        mod = ast.Module(
            [
                *self.get_future_imports(),
                *self.load_helpers(),
                *self.transform_body(),
            ]
        )
        if mod.body and original_first_node:
            # this isn't obvious but the new mod body is empty
            # if the original module body is empty. Therefore there
            # is always a location to copy
            copy_location(mod.body[0], original_first_node)

        mod.type_ignores = []
        return mod

    def load_helpers(self) -> Iterable[stmt]:
        helpers = []
        # no need to load <track-import-call> if body is empty
        if self.track_import_call and self.root.body:
            helpers.append(
                lineinfo(
                    make_assign(
                        [lineinfo(ast.Name("<strict-modules>", ast.Store()))],
                        lineinfo(
                            ast.Subscript(
                                lineinfo(ast.Name("<fixed-modules>", ast.Load())),
                                lineinfo(ast.Index(lineinfo(Constant("__strict__")))),
                                ast.Load(),
                            )
                        ),
                    )
                )
            )
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
            self.is_static,
            self.track_import_call,
        )

    def transform_body(self) -> Iterable[stmt]:
        scopes = ScopeStack(
            SymbolScope(self.table, ScopeData()), symbol_map=self.symbol_map
        )
        transformer = self.make_transformer(scopes)
        body = transformer.visit(self.root).body

        return body


def rewrite(
    root: Module,
    table: SymbolTable,
    filename: str,
    modname: str,
    mode: str = "exec",
    optimize: int = -1,
    builtins: ModuleType | Mapping[str, object] = __builtins__,
    is_static: bool = False,
    track_import_call: bool = False,
) -> Module:
    return StrictModuleRewriter(
        root,
        table,
        filename,
        modname,
        mode,
        optimize,
        builtins,
        is_static=is_static,
        track_import_call=track_import_call,
    ).transform()


TTransformedStmt = Union[Optional[AST], List[AST]]
TVar = TypeVar("TScope")
TScopeData = TypeVar("TData")


class SymbolVisitor(Generic[TVar, TScopeData], NodeVisitor):
    def __init__(
        self, scopes: ScopeStack[TVar, TScopeData], ignore_names: Iterable[str]
    ) -> None:
        self.scopes = scopes
        self.ignore_names: Set[str] = set(ignore_names)

    @property
    def skip_annotations(self) -> bool:
        return False

    def is_global(self, name: str) -> bool:
        return name not in self.ignore_names and self.scopes.is_global(name)

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
            if retnode and not self.skip_annotations:
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
            # visit body in class scope
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
    def __init__(
        self, scopes: ScopeStack[None, None], ignore_names: Iterable[str]
    ) -> None:
        super().__init__(scopes, ignore_names)
        self.globals: Set[str] = set()
        self.global_sets: Set[str] = set()
        self.global_dels: Set[str] = set()
        self.future_imports: Set[alias] = set()

    def future_annotations(self) -> bool:
        for a in self.future_imports:
            if a.name == "annotations":
                return True
        return False

    @property
    def skip_annotations(self) -> bool:
        return self.future_annotations()

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

    def visit_arg(self, node: ast.arg) -> None:
        if not self.skip_annotations:
            return self.generic_visit(node)

    def visit_AsyncFunctionDef(self, node: AsyncFunctionDef) -> None:
        self.visit_Func_Outer(node)

        self.store_name(node.name)

        self.visit_Func_Inner(node)

    def visit_AnnAssign(self, node: AnnAssign) -> None:
        self.visit(node.target)
        if not self.skip_annotations:
            self.visit(node.annotation)
        value = node.value
        if value:
            self.visit(value)


class ScopeData:
    def __init__(self) -> None:
        self.has_classdefs = False

    def visit_Assign(self, node: Assign) -> None:
        pass

    def visit_AnnAssign(self, node: AnnAssign) -> None:
        pass

    def visit_decorators(self, node: ClassDef | FunctionDef | AsyncFunctionDef) -> None:
        # visit the current node held by the scope data
        pass


@final
class ClassScope(ScopeData):
    def __init__(self, node: ClassDef) -> None:
        super().__init__()
        self.node = node
        self.instance_fields: Set[str] = set()
        self.cached_props: Dict[str, object] = {}
        self.slots_enabled: bool = False
        self.loose_slots: bool = False
        self.extra_slots: Optional[List[str]] = None

    def visit_AnnAssign(self, node: AnnAssign) -> None:
        target = node.target
        if node.value is None and isinstance(target, Name):
            self.instance_fields.add(target.id)

    def visit_decorators(self, node: ClassDef | FunctionDef | AsyncFunctionDef) -> None:
        for dec in node.decorator_list:
            if is_indicator_dec(dec):
                if is_strict_slots(dec):
                    self.slots_enabled = True
                elif is_loose_slots(dec):
                    self.loose_slots = True
                elif (extra_slots := get_extra_slots(dec)) is not None:
                    self.extra_slots = extra_slots

        node.decorator_list = [
            d for d in node.decorator_list if (is_mutable(d) or not is_indicator_dec(d))
        ]


@final
class FunctionScope(ScopeData):
    def __init__(self, node: AsyncFunctionDef | FunctionDef, parent: ScopeData) -> None:
        super().__init__()
        self.node = node
        self.parent = parent
        self.is_cached_prop: bool = False
        self.cached_prop_value: object = None

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

    def visit_decorators(self, node: ClassDef | FunctionDef | AsyncFunctionDef) -> None:
        for dec in node.decorator_list:
            if is_indicator_dec(dec):
                cached_prop_value = get_cached_prop_value(dec)
                if cached_prop_value is not None:
                    self.is_cached_prop = True
                    self.cached_prop_value = cached_prop_value
                    break
        node.decorator_list = [
            d for d in node.decorator_list if not is_indicator_dec(d)
        ]


@final
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
        is_static: bool,
        track_import_call: bool,
    ) -> None:
        super().__init__(symbols, ALL_INDICATORS)
        symbols.scope_factory = self.make_scope
        self.modname = modname
        self.future_imports = future_imports
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

    def visit_Assign(self, node: Assign) -> TTransformedStmt:
        self.scopes.scopes[-1].scope_data.visit_Assign(node)
        node = self.update_node(
            node,
            targets=self.walk_list(node.targets),
            value=self.visit(node.value),
        )
        return node

    def visit_AnnAssign(self, node: AnnAssign) -> TTransformedStmt:
        self.scopes.scopes[-1].scope_data.visit_AnnAssign(node)
        return self.generic_visit(node)

    def make_base_class_dict_test_stmts(
        self, bases: List[TAst], instance_fields: Set[str], location_node: ast.ClassDef
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
                        ),
                        target=location_node,
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
        node = self.clone_node(node)
        self.visit_Class_Outer(node, True)
        class_scope = self.visit_Class_Inner(node, True, scope_node=orig_node)
        scope_data = class_scope.scope_data
        assert isinstance(scope_data, ClassScope), type(class_scope).__name__
        scope_data.visit_decorators(node)

        slots_enabled = scope_data.slots_enabled and not self.is_static
        if slots_enabled:
            if scope_data.loose_slots:
                scope_data.instance_fields.add("__dict__")
                scope_data.instance_fields.add("__loose_slots__")
            extra_slots = scope_data.extra_slots
            if extra_slots:
                scope_data.instance_fields.update(extra_slots)

            node.body.append(
                self.make_base_class_dict_test_stmts(
                    node.bases, scope_data.instance_fields, node
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

        return node

    def make_cached_property_init_decorator(
        self,
        scope_data: ClassScope,
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

    def _create_track_import_call(
        self, location_node: FunctionDef | AsyncFunctionDef
    ) -> ast.Expr:
        return lineinfo(
            ast.Expr(
                lineinfo(
                    Call(
                        lineinfo(ast.Name("<track-import-call>", ctx=ast.Load())),
                        [lineinfo(ast.Constant(self.modname))],
                        [],
                    )
                ),
            ),
            target=location_node,
        )

    def visit_FunctionDef(self, node: FunctionDef) -> TTransformedStmt:
        outer_scope = self.scopes.scopes[-1].scope_data
        orig_node = node
        node = self.clone_node(node)
        self.visit_Func_Outer(node, True)

        scope_data = self.visit_Func_Inner(node, True, scope_node=orig_node).scope_data
        scope_data.visit_decorators(node)

        res: TTransformedStmt = node

        self.check_cached_prop(node, scope_data, outer_scope)

        if self.track_import_call:
            node.body.insert(0, self._create_track_import_call(node))

        return res

    def visit_AsyncFunctionDef(self, node: AsyncFunctionDef) -> TTransformedStmt:
        outer_scope = self.scopes.scopes[-1].scope_data
        orig_node = node
        node = self.clone_node(node)
        self.visit_Func_Outer(node, True)

        scope_data = self.visit_Func_Inner(node, True, scope_node=orig_node).scope_data
        scope_data.visit_decorators(node)

        orig_name = node.name
        self.check_cached_prop(node, scope_data, outer_scope)

        if self.track_import_call:
            node.body.insert(0, self._create_track_import_call(node))

        return node

    def check_cached_prop(
        self,
        node: AsyncFunctionDef | FunctionDef,
        func_scope: ScopeData,
        outer_scope: ScopeData,
    ) -> None:
        if isinstance(func_scope, FunctionScope) and isinstance(
            outer_scope, ClassScope
        ):
            if func_scope.is_cached_prop:
                # preprocessor already checked that outer scope is slotified
                outer_scope.instance_fields.add(node.name)
                outer_scope.cached_props[node.name] = func_scope.cached_prop_value
                node.name = self.mangle_cached_prop(node.name)

    def visit_Lambda(self, node: Lambda) -> TTransformedStmt:
        orig_node = node
        node = self.clone_node(node)
        self.visit_Func_Outer(node, True)

        self.visit_Func_Inner(node, True, scope_node=orig_node)

        return node

    def visit_ImportFrom(self, node: ImportFrom) -> TTransformedStmt:
        if node.module == "__future__":
            # We push these to the top of the module where they're required to be
            return None
        return node

    def visit_comp(
        self, node: ListComp | SetComp | DictComp | GeneratorExp
    ) -> Optional[expr]:
        orig_node = node
        node = self.clone_node(node)
        self.visit_Comp_Outer(node, True)

        self.visit_Comp_Inner(node, True, scope_node=orig_node)

        return node
