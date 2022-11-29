# Portions copyright (c) Meta Platforms, Inc. and affiliates. (http://www.meta.com)
# pyre-unsafe

import ast
from ast import AST, copy_location
from typing import Any, Sequence, TypeVar, Union

# XXX should probably rename ASTVisitor to ASTWalker
# XXX can it be made even more generic?


class ASTVisitor:
    """Performs a depth-first walk of the AST

    The ASTVisitor is responsible for walking over the tree in the
    correct order.  For each node, it checks the visitor argument for
    a method named 'visitNodeType' where NodeType is the name of the
    node's class, e.g. Class.  If the method exists, it is called
    with the node as its sole argument.

    This is basically the same as the built-in ast.NodeVisitor except
    for the following differences:
        It accepts extra parameters through the visit methods for flowing state
        It uses "visitNodeName" instead of "visit_NodeName"
        It accepts a list to the generic_visit function rather than just nodes
    """

    VERBOSE = 0

    def __init__(self):
        self.node = None
        self._cache = {}

    def generic_visit(self, node, *args):
        """Called if no explicit visitor function exists for a node."""
        if isinstance(node, list):
            for item in node:
                if isinstance(item, ast.AST):
                    self.visit(item, *args)
            return

        for _field, value in ast.iter_fields(node):
            if isinstance(value, list):
                for item in value:
                    if isinstance(item, ast.AST):
                        self.visit(item, *args)
            elif isinstance(value, ast.AST):
                self.visit(value, *args)

    def walk_list(self, nodes: Sequence[AST], *args):
        for item in nodes:
            if isinstance(item, ast.AST):
                self.visit(item, *args)

    def skip_visit(self):
        return False

    def visit(self, node: Union[AST, Sequence[AST]], *args):
        if self.skip_visit():
            return
        if isinstance(node, list):
            return self.walk_list(node, *args)
        self.node = node
        klass = node.__class__
        meth = self._cache.get(klass, None)
        if meth is None:
            className = klass.__name__
            meth = getattr(self, "visit" + className, self.generic_visit)
            self._cache[klass] = meth
        return meth(node, *args)


TAst = TypeVar("TAst", bound=AST)


class ASTRewriter(ASTVisitor):
    """performs rewrites on the AST, rewriting parent nodes when child nodes
    are replaced."""

    @staticmethod
    def update_node(node: TAst, **replacement: Any) -> TAst:
        res = node
        for name, val in replacement.items():
            existing = getattr(res, name)
            if existing is val:
                continue

            if node is res:
                res = ASTRewriter.clone_node(node)

            setattr(res, name, val)
        return res

    @staticmethod
    def clone_node(node: TAst) -> TAst:
        attrs = []
        for name in node._fields:
            attr = getattr(node, name, None)
            if isinstance(attr, list):
                attr = list(attr)
            attrs.append(attr)

        new = type(node)(*attrs)
        return copy_location(new, node)

    def walk_list(self, nodes: Sequence[TAst], *args) -> Sequence[TAst]:
        new_values = []
        changed = False
        for value in nodes:
            if isinstance(value, AST):
                new_value = self.visit(value)
                changed |= new_value is not value
                if new_value is None:
                    continue
                elif not isinstance(new_value, AST):
                    new_values.extend(new_value)
                    continue
                value = new_value

            new_values.append(value)
        return new_values if changed else nodes

    def skip_field(self, node: AST, field: str) -> bool:
        return False

    def generic_visit(self, node: TAst, *args) -> TAst:
        ret_node = node
        for field, old_value in ast.iter_fields(node):
            if self.skip_field(node, field):
                continue
            if not isinstance(old_value, (AST, list)):
                continue

            new_node = self.visit(old_value)
            assert (  # noqa: IG01
                new_node is not None
            ), f"can't remove AST nodes that aren't part of a list {old_value!r}"
            if new_node is not old_value:
                if ret_node is node:
                    ret_node = self.clone_node(node)

                setattr(ret_node, field, new_node)

        return ret_node


class ExampleASTVisitor(ASTVisitor):
    """Prints examples of the nodes that aren't visited

    This visitor-driver is only useful for development, when it's
    helpful to develop a visitor incrementally, and get feedback on what
    you still have to do.
    """

    examples = {}

    def visit(self, node, *args):
        self.node = node
        meth = self._cache.get(node.__class__, None)
        className = node.__class__.__name__
        if meth is None:
            meth = getattr(self, "visit" + className, 0)
            self._cache[node.__class__] = meth
        if self.VERBOSE > 1:
            print("visit", className, meth and meth.__name__ or "")
        if meth:
            meth(node, *args)
        elif self.VERBOSE > 0:
            klass = node.__class__
            if klass not in self.examples:
                self.examples[klass] = klass
                print()
                print(self)
                print(klass)
                for attr in dir(node):
                    if attr[0] != "_":
                        print("\t", "%-12.12s" % attr, getattr(node, attr))
                print()
            return self.default(node, *args)


# XXX this is an API change


def walk(tree, visitor):
    return visitor.visit(tree)


def dumpNode(node):
    print(node.__class__)
    for attr in dir(node):
        if attr[0] != "_":
            print("\t", "%-10.10s" % attr, getattr(node, attr))
