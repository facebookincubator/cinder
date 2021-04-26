# pyre-unsafe
import ast
import operator
import sys
from ast import Bytes, Constant, Ellipsis, NameConstant, Num, Str, cmpop, copy_location
from typing import Dict, Iterable, Optional, Type

from .peephole import safe_lshift, safe_mod, safe_multiply, safe_power
from .visitor import ASTRewriter


def is_const(node):
    return isinstance(node, (Constant, Num, Str, Bytes, Ellipsis, NameConstant))


def get_const_value(node):
    if isinstance(node, (Constant, NameConstant)):
        return node.value
    elif isinstance(node, Num):
        return node.n
    elif isinstance(node, (Str, Bytes)):
        return node.s
    elif isinstance(node, Ellipsis):
        return ...

    raise TypeError("Bad constant value")


class Py37Limits:
    MAX_INT_SIZE = 128
    MAX_COLLECTION_SIZE = 256
    MAX_STR_SIZE = 4096
    MAX_TOTAL_ITEMS = 1024


UNARY_OPS = {
    ast.Invert: operator.invert,
    ast.Not: operator.not_,
    ast.UAdd: operator.pos,
    ast.USub: operator.neg,
}
INVERSE_OPS: Dict[Type[cmpop], Type[cmpop]] = {
    ast.Is: ast.IsNot,
    ast.IsNot: ast.Is,
    ast.In: ast.NotIn,
    ast.NotIn: ast.In,
}

BIN_OPS = {
    ast.Add: operator.add,
    ast.Sub: operator.sub,
    ast.Mult: lambda l, r: safe_multiply(l, r, Py37Limits),
    ast.Div: operator.truediv,
    ast.FloorDiv: operator.floordiv,
    ast.Mod: lambda l, r: safe_mod(l, r, Py37Limits),
    ast.Pow: lambda l, r: safe_power(l, r, Py37Limits),
    ast.LShift: lambda l, r: safe_lshift(l, r, Py37Limits),
    ast.RShift: operator.rshift,
    ast.BitOr: operator.or_,
    ast.BitXor: operator.xor,
    ast.BitAnd: operator.and_,
}

IS_PY38_ABOVE = sys.version_info >= (3, 8)


class AstOptimizer(ASTRewriter):
    def __init__(self, optimize: bool = False):
        super().__init__()
        self.optimize = optimize

    def visitUnaryOp(self, node: ast.UnaryOp) -> ast.expr:
        op = self.visit(node.operand)
        if is_const(op):
            conv = UNARY_OPS[type(node.op)]
            val = get_const_value(op)
            try:
                return copy_location(Constant(conv(val)), node)
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

        if is_const(left) and is_const(right):
            handler = BIN_OPS.get(type(node.op))
            if handler is not None:
                lval = get_const_value(left)
                rval = get_const_value(right)
                try:
                    return copy_location(Constant(handler(lval, rval)), node)
                except Exception:
                    pass

        return self.update_node(node, left=left, right=right)

    def makeConstTuple(self, elts: Iterable[ast.expr]) -> Optional[Constant]:
        if all(is_const(elt) for elt in elts):
            return Constant(tuple(get_const_value(elt) for elt in elts))

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
            and is_const(value)
            and isinstance(slice, ast.Index)
            and is_const(slice.value)
        ):
            try:
                return copy_location(
                    Constant(get_const_value(value)[get_const_value(slice.value)]), node
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
            if IS_PY38_ABOVE and not any(isinstance(e, ast.Starred) for e in elts):
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

    def visitName(self, node: ast.Name):
        if node.id == "__debug__":
            return copy_location(Constant(not self.optimize), node)

        return self.generic_visit(node)

    def visitAssert(self, node: ast.Assert):
        if self.optimize:
            # Skip asserts if we're optimizing
            return None
        return self.generic_visit(node)
