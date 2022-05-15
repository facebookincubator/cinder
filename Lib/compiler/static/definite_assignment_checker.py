# @oncall strictmod

from __future__ import annotations

import ast
from ast import (
    AnnAssign,
    Assign,
    AST,
    AsyncFunctionDef,
    AugAssign,
    ClassDef,
    comprehension,
    copy_location,
    DictComp,
    For,
    FunctionDef,
    GeneratorExp,
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
    stmt,
    Try,
    While,
    With,
)
from typing import Iterable, List, Optional, Set, Union

from ..consts import SC_CELL, SC_LOCAL
from ..symbols import Scope


class DefiniteAssignmentVisitor(NodeVisitor):
    def __init__(self, scope: Scope) -> None:
        self.scope = scope
        self.assigned: Set[str] = set()
        self.unassigned: Set[Name] = set()

    def analyzeFunction(self, node: FunctionDef | AsyncFunctionDef) -> None:
        for arg in node.args.args:
            self.assigned.add(arg.arg)
        for arg in node.args.kwonlyargs:
            self.assigned.add(arg.arg)
        for arg in node.args.posonlyargs:
            self.assigned.add(arg.arg)

        vararg = node.args.vararg
        if vararg:
            self.assigned.add(vararg.arg)
        kwarg = node.args.kwarg
        if kwarg:
            self.assigned.add(kwarg.arg)
        for stmt in node.body:
            self.visit(stmt)

    def set_assigned(self, name: str) -> None:
        if self.is_local(name):
            self.assigned.add(name)

    def is_local(self, name: str) -> bool:
        scope = self.scope.check_name(name)
        return scope == SC_LOCAL or scope == SC_CELL

    def visit_Name(self, node: Name) -> None:
        # we only walk the module and class defs and nested class defs
        # can't access parent class def variables, so the variable is
        # definitely assigned in the inner most class def
        # TODO: T52624111 needs special handling for __class__
        if not self.is_local(node.id):
            return

        if isinstance(node.ctx, ast.Load):
            if node.id not in self.assigned:
                self.unassigned.add(node)
        elif isinstance(node.ctx, ast.Del):
            if node.id not in self.assigned:
                self.unassigned.add(node)
            else:
                self.assigned.remove(node.id)
        else:
            self.assigned.add(node.id)

    def visit_Assign(self, node: Assign) -> None:
        # generic_visit will evaluate these in a different order due to _fields
        # being targets, value
        self.visit(node.value)
        for target in node.targets:
            self.visit(target)

    def visit_AugAssign(self, node: AugAssign) -> None:
        target = node.target
        if isinstance(target, ast.Name):
            if target.id not in self.assigned:
                self.unassigned.add(target)
            self.generic_visit(node.value)
            return

        self.generic_visit(node)

    def visit_Try(self, node: Try) -> None:
        if not node.handlers:
            # Try/finally, all assignments are guaranteed
            entry = set(self.assigned)
            self.walk_stmts(node.body)

            post_try = set(self.assigned)

            # finally is not guaranteed to have any try statements executed,
            # but any deletes should be assumed
            self.assigned = entry.intersection(post_try)

            self.walk_stmts(node.finalbody)

            # Anything the finally deletes is removed (the finally cannot delete
            # things which weren't defined on entry, because they aren't definitely
            # assigned)
            for value in entry:
                if value not in self.assigned and value in post_try:
                    post_try.remove(value)
            #
            # Anything that was assigned in the try is assigned after
            post_try.update(self.assigned)
            self.assigned = post_try
            return

        # try/except/maybe finally.  Only the finally assignments are guaranteed
        entry = set(self.assigned)
        self.walk_stmts(node.body)
        # Remove anything that got deleted...

        elseentry = set(self.assigned)
        entry.intersection_update(self.assigned)

        finalentry = set(entry)
        for handler in node.handlers:
            self.assigned = set(entry)
            handler_name = handler.name
            if handler_name is not None:
                self.set_assigned(handler_name)

            self.walk_stmts(handler.body)
            # All deletes for any entry apply to the finally
            finalentry.intersection_update(self.assigned)

        if node.orelse:
            self.assigned = elseentry
            self.walk_stmts(node.orelse)
            finalentry.intersection_update(self.assigned)

        self.assigned = finalentry
        if node.finalbody:
            self.walk_stmts(node.finalbody)

    def visit_ClassDef(self, node: ClassDef) -> None:
        for base in node.bases:
            self.visit(base)

        for kw in node.keywords:
            self.visit(kw)

        for dec in node.decorator_list:
            self.visit(dec)

        self.set_assigned(node.name)

    def visit_FunctionDef(self, node: FunctionDef) -> None:
        self._visit_func_like(node)

    def visit_AsyncFunctionDef(self, node: AsyncFunctionDef) -> None:
        self._visit_func_like(node)

    def visit_Lambda(self, node: Lambda) -> None:
        self.visit(node.args)

    def _visit_func_like(self, node: Union[FunctionDef, AsyncFunctionDef]) -> None:
        self.visit(node.args)

        returns = node.returns
        if returns:
            self.visit(returns)

        for dec in node.decorator_list:
            self.visit(dec)

        # Body doesn't run so isn't processed
        self.set_assigned(node.name)

    def visit_With(self, node: With) -> None:
        # All of the with items will be definitely assigned
        for item in node.items:
            self.visit(item)

        entry = set(self.assigned)

        self.walk_stmts(node.body)
        # No assignments are guaranteed, the body can throw and the context
        # manager can suppress it.  But all deletes need to be assumed to have
        # occurred
        entry.intersection_update(self.assigned)
        self.assigned = entry

    def visit_Import(self, node: Import) -> None:
        for name in node.names:
            self.set_assigned(name.asname or name.name.partition(".")[0])

    def visit_ImportFrom(self, node: ImportFrom) -> None:
        for name in node.names:
            self.set_assigned(name.asname or name.name)

    def visit_AnnAssign(self, node: AnnAssign) -> None:
        # We won't actually define anything w/o a value
        if node.value:
            self.generic_visit(node)

    # Don't need AsyncFor, AsyncWith Return as they can't be present at
    # module / class level

    def visit_For(self, node: For) -> None:
        self.visit(node.iter)

        entry = set(self.assigned)
        self.visit(node.target)
        self.walk_stmts(node.body)

        # no assigns are guaranteed, need to assume deletes occur, and
        # they impact the else block too.
        entry.intersection_update(self.assigned)
        if node.orelse:
            self.assigned = set(entry)
            self.walk_stmts(node.orelse)

            # else may not run, so again no signs guaranteed, deletes are assumed
            entry.intersection_update(self.assigned)

        self.assigned = entry

    def visit_If(self, node: If) -> None:
        test = node.test
        self.visit(node.test)
        entry = set(self.assigned)
        self.walk_stmts(node.body)

        post_if = self.assigned

        if node.orelse:
            self.assigned = set(entry)

            self.walk_stmts(node.orelse)

            self.assigned = self.assigned.intersection(post_if)
        else:
            # Remove anything which was deleted
            entry.intersection_update(post_if)
            self.assigned = entry

    def visit_While(self, node: While) -> None:
        self.visit(node.test)
        entry = set(self.assigned)
        self.walk_stmts(node.body)

        # Any dels will remove assignment
        entry.intersection_update(self.assigned)
        if node.orelse:
            self.assigned = set(entry)

            self.walk_stmts(node.orelse)
            # Any dels will remove definite assignment
            entry.intersection_update(self.assigned)

        # While loop may never be entered, else may never be entered, so
        # it never definitely assigns anything
        self.assigned = entry

    def walk_stmts(self, nodes: Iterable[stmt]) -> None:
        for node in nodes:
            self.visit(node)
            if isinstance(node, Raise):
                # following statements are unreachable
                return
