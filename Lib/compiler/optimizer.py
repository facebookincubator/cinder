# Portions copyright (c) Facebook, Inc. and its affiliates. (http://www.facebook.com)
from __future__ import annotations

import ast
import operator
import sys
from ast import Bytes, Constant, Ellipsis, NameConstant, Num, Str, cmpop, copy_location
from typing import Callable, Dict, Iterable, Mapping, Optional, Type

from .visitor import ASTRewriter


class PyLimits:
    MAX_INT_SIZE = 128
    MAX_COLLECTION_SIZE = 256
    MAX_STR_SIZE = 4096
    MAX_TOTAL_ITEMS = 1024


UNARY_OPS: Mapping[Type[ast.unaryop], Callable[[object], object]] = {
    ast.Invert: operator.invert,
    ast.Not: operator.not_,
    ast.UAdd: operator.pos,
    ast.USub: operator.neg,
}
INVERSE_OPS: Mapping[Type[cmpop], Type[cmpop]] = {
    ast.Is: ast.IsNot,
    ast.IsNot: ast.Is,
    ast.In: ast.NotIn,
    ast.NotIn: ast.In,
}

BIN_OPS: Mapping[Type[ast.operator], Callable[[object, object], object]] = {
    ast.Add: operator.add,
    ast.Sub: operator.sub,
    ast.Mult: lambda l, r: safe_multiply(l, r, PyLimits),
    ast.Div: operator.truediv,
    ast.FloorDiv: operator.floordiv,
    ast.Mod: lambda l, r: safe_mod(l, r, PyLimits),
    ast.Pow: lambda l, r: safe_power(l, r, PyLimits),
    ast.LShift: lambda l, r: safe_lshift(l, r, PyLimits),
    ast.RShift: operator.rshift,
    ast.BitOr: operator.or_,
    ast.BitXor: operator.xor,
    ast.BitAnd: operator.and_,
}


class DefaultLimits:
    MAX_INT_SIZE = 128
    MAX_COLLECTION_SIZE = 20
    MAX_STR_SIZE = 20
    MAX_TOTAL_ITEMS = 1024


def safe_lshift(left, right, limits=DefaultLimits):
    if isinstance(left, int) and isinstance(right, int) and left and right:
        lbits = left.bit_length()
        if (
            right < 0
            or right > limits.MAX_INT_SIZE
            or lbits > limits.MAX_INT_SIZE - right
        ):
            raise OverflowError()

    return left << right


def check_complexity(obj: object, limit: int) -> int:
    if isinstance(obj, (frozenset, tuple)):
        limit -= len(obj)
        for item in obj:
            limit = check_complexity(item, limit)
            if limit < 0:
                break

    return limit


def safe_multiply(left, right, limits=DefaultLimits):
    if isinstance(left, int) and isinstance(right, int) and left and right:
        lbits = left.bit_length()
        rbits = right.bit_length()
        if lbits + rbits > limits.MAX_INT_SIZE:
            raise OverflowError()
    elif isinstance(left, int) and isinstance(right, (tuple, frozenset)):
        rsize = len(right)
        if rsize:
            if left < 0 or left > limits.MAX_COLLECTION_SIZE / rsize:
                raise OverflowError()
            if left:
                if check_complexity(right, limits.MAX_TOTAL_ITEMS / left) < 0:
                    raise OverflowError()
    elif isinstance(left, int) and isinstance(right, (str, bytes)):
        rsize = len(right)
        if rsize:
            if left < 0 or left > limits.MAX_STR_SIZE / rsize:
                raise OverflowError()
    elif isinstance(right, int) and isinstance(left, (tuple, frozenset, str, bytes)):
        return safe_multiply(right, left, limits)

    return left * right


def safe_power(left, right, limits=DefaultLimits):
    if isinstance(left, int) and isinstance(right, int) and left and right > 0:
        lbits = left.bit_length()
        if lbits > limits.MAX_INT_SIZE / right:
            raise OverflowError()

    return left ** right


def safe_mod(left, right, limits=DefaultLimits):
    if isinstance(left, (str, bytes)):
        raise OverflowError()

    return left % right


class AstOptimizer(ASTRewriter):
    def __init__(self, optimize: bool = False) -> None:
        super().__init__()
        self.optimize = optimize

    def visitUnaryOp(self, node: ast.UnaryOp) -> ast.expr:
        op = self.visit(node.operand)
        if isinstance(op, Constant):
            conv = UNARY_OPS[type(node.op)]
            try:
                return copy_location(Constant(conv(op.value)), node)
            except Exception:
                pass
        elif (
            isinstance(node.op, ast.Not)
            and isinstance(op, ast.Compare)
            and len(op.ops) == 1
        ):
            cmp_op = op.ops[0]
            new_op = INVERSE_OPS.get(type(cmp_op))
            if new_op is not None:
                return self.update_node(op, ops=[new_op()])

        return self.update_node(node, operand=op)

    def visitBinOp(self, node: ast.BinOp) -> ast.expr:
        left = self.visit(node.left)
        right = self.visit(node.right)

        if isinstance(left, Constant) and isinstance(right, Constant):
            handler = BIN_OPS.get(type(node.op))
            if handler is not None:
                try:
                    return copy_location(
                        Constant(handler(left.value, right.value)), node
                    )
                except Exception:
                    pass

        return self.update_node(node, left=left, right=right)

    def makeConstTuple(self, elts: Iterable[ast.expr]) -> Optional[Constant]:
        if all(isinstance(elt, Constant) for elt in elts):
            # pyre-ignore[16]: each elt is a constant at this point.
            return Constant(tuple(elt.value for elt in elts))

        return None

    def visitTuple(self, node: ast.Tuple) -> ast.expr:
        elts = self.walk_list(node.elts)

        if isinstance(node.ctx, ast.Load):
            res = self.makeConstTuple(elts)
            if res is not None:
                return copy_location(res, node)

        return self.update_node(node, elts=elts)

    def visitSubscript(self, node: ast.Subscript) -> ast.expr:
        value = self.visit(node.value)
        slice = self.visit(node.slice)

        if (
            isinstance(node.ctx, ast.Load)
            and isinstance(value, Constant)
            and isinstance(slice, Constant)
        ):
            try:
                return copy_location(
                    # pyre-ignore[16] we know this is unsafe, so it's wrapped in try/except
                    Constant(value.value[slice.value]),
                    node,
                )
            except Exception:
                pass

        return self.update_node(node, value=value, slice=slice)

    def _visitIter(self, node: ast.expr) -> ast.expr:
        if isinstance(node, ast.List):
            elts = self.walk_list(node.elts)
            res = self.makeConstTuple(elts)
            if res is not None:
                return copy_location(res, node)
            if not any(isinstance(e, ast.Starred) for e in elts):
                return self.update_node(ast.Tuple(elts=elts, ctx=node.ctx))
            return self.update_node(node, elts=elts)
        elif isinstance(node, ast.Set):
            elts = self.walk_list(node.elts)
            res = self.makeConstTuple(elts)
            if res is not None:
                return copy_location(Constant(frozenset(res.value)), node)

            return self.update_node(node, elts=elts)

        return self.generic_visit(node)

    def visitcomprehension(self, node: ast.comprehension) -> ast.comprehension:
        target = self.visit(node.target)
        iter = self.visit(node.iter)
        ifs = self.walk_list(node.ifs)
        iter = self._visitIter(iter)

        return self.update_node(node, target=target, iter=iter, ifs=ifs)

    def visitFor(self, node: ast.For) -> ast.For:
        target = self.visit(node.target)
        iter = self.visit(node.iter)
        body = self.walk_list(node.body)
        orelse = self.walk_list(node.orelse)

        iter = self._visitIter(iter)
        return self.update_node(
            node, target=target, iter=iter, body=body, orelse=orelse
        )

    def visitCompare(self, node: ast.Compare) -> ast.expr:
        left = self.visit(node.left)
        comparators = self.walk_list(node.comparators)

        if isinstance(node.ops[-1], (ast.In, ast.NotIn)):
            new_iter = self._visitIter(comparators[-1])
            if new_iter is not None and new_iter is not comparators[-1]:
                comparators = list(comparators)
                comparators[-1] = new_iter

        return self.update_node(node, left=left, comparators=comparators)

    def visitName(self, node: ast.Name) -> ast.Name | ast.Constant:
        if node.id == "__debug__":
            return copy_location(Constant(not self.optimize), node)

        return self.generic_visit(node)

    def visitNamedExpr(self, node: ast.NamedExpr) -> ast.NamedExpr:
        return self.generic_visit(node)
