# Copyright (c) Meta Platforms, Inc. and affiliates. (http://www.meta.com)

from __future__ import annotations

import ast
from typing import final, TypeVar


FunctionDefNode = TypeVar("FunctionDefNode", ast.FunctionDef, ast.AsyncFunctionDef)


def remove_annotations(node: ast.AST) -> ast.Module:
    return ast.fix_missing_locations(AnnotationRemover().visit(node))


def _copy_attrs(src: ast.AST, dest: ast.AST) -> None:
    """
    Copies line and column info from one node to another.
    """
    dest.lineno = src.lineno
    dest.end_lineno = src.end_lineno
    dest.col_offset = src.col_offset
    dest.end_col_offset = src.end_col_offset


@final
class AnnotationRemover(ast.NodeTransformer):
    def visit_single_arg(self, arg: ast.arg) -> ast.arg:
        arg.annotation = None
        return arg

    def visit_fn_arguments(self, node: ast.arguments) -> ast.arguments:
        if node.posonlyargs:
            node.posonlyargs = [self.visit_single_arg(a) for a in node.posonlyargs]
        if node.args:
            node.args = [self.visit_single_arg(a) for a in node.args]
        if node.kwonlyargs:
            node.kwonlyargs = [self.visit_single_arg(a) for a in node.kwonlyargs]
        vararg = node.vararg
        if vararg:
            node.vararg = self.visit_single_arg(vararg)
        kwarg = node.kwarg
        if kwarg:
            node.kwarg = self.visit_single_arg(kwarg)
        return node

    def visit_function(self, node: FunctionDefNode) -> FunctionDefNode:
        node.arguments = self.visit_fn_arguments(node.args)
        node.returns = None
        node.decorator_list = [
            self.visit(decorator) for decorator in node.decorator_list
        ]
        return node

    def visit_FunctionDef(self, node: FunctionDefNode) -> FunctionDefNode:
        return self.visit_function(node)

    def visit_AsyncFunctionDef(self, node: FunctionDefNode) -> FunctionDefNode:
        return self.visit_function(node)

    def visit_AnnAssign(self, node: ast.AnnAssign) -> ast.Assign:
        # Here, we replace `x: A = a` with just `x = a`. In case there's no value,
        # we set the value to an `...`. E.g: `x: A` changes to `x = ...`.
        #
        # We need to be a little careful about ensuring that newly created nodes
        # get the line and column information copied to them. This helps us avoid
        # an extra pass over the AST with ast.fix_missing_locations()
        value = node.value
        if value is None:
            value = ast.Ellipsis()
            value.kind = None
            _copy_attrs(node, value)

        assign = ast.Assign(targets=[node.target], value=value, type_comment=None)

        _copy_attrs(node, assign)
        return assign
