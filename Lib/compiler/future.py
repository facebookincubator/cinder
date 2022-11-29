# Portions copyright (c) Meta Platforms, Inc. and affiliates. (http://www.meta.com)
# pyre-unsafe

"""Parser for future statements

"""
from __future__ import print_function

import ast

from .visitor import ASTVisitor, walk


def is_future(stmt):
    """Return true if statement is a well-formed future statement"""
    if not isinstance(stmt, ast.ImportFrom):
        return 0
    if stmt.module == "__future__":
        return 1
    else:
        return 0


class FutureParser(ASTVisitor):

    features = (
        "nested_scopes",
        "generators",
        "division",
        "absolute_import",
        "with_statement",
        "print_function",
        "unicode_literals",
        "generator_stop",
        "barry_as_FLUFL",
        "annotations",
        "eager_imports",
    )

    def __init__(self):
        super().__init__()
        self.found = {}  # set
        self.possible_docstring = True

    def visitModule(self, node):
        for s in node.body:
            if (
                self.possible_docstring
                and isinstance(s, ast.Expr)
                and isinstance(s.value, ast.Str)
            ):
                self.possible_docstring = False
                continue
            # no docstring after first statement
            self.possible_docstring = False
            if not self.check_stmt(s):
                break

    def check_stmt(self, stmt):
        if is_future(stmt):
            for alias in stmt.names:
                name = alias.name
                if name in self.features:
                    self.found[name] = 1
                elif name == "braces":
                    raise SyntaxError("not a chance")
                else:
                    raise SyntaxError("future feature %s is not defined" % name)
            stmt.valid_future = 1
            return 1
        return 0

    def get_features(self):
        """Return list of features enabled by future statements"""
        return self.found.keys()


class BadFutureParser(ASTVisitor):
    """Check for invalid future statements"""

    def visitImportFrom(self, node):
        if hasattr(node, "valid_future"):
            return
        if node.module != "__future__":
            return
        raise SyntaxError(
            "from __future__ imports must occur at the beginning of the file"
        )


def find_futures(node):
    p1 = FutureParser()
    p2 = BadFutureParser()
    walk(node, p1)
    walk(node, p2)
    return p1.get_features()
