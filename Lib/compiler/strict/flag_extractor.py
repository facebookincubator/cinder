# Copyright (c) Meta Platforms, Inc. and affiliates. (http://www.meta.com)

from __future__ import annotations

import ast

import sys
from ast import Import, ImportFrom
from dataclasses import dataclass
from typing import Optional, Sequence, Union

from ..symbols import ModuleScope, Scope, SymbolVisitor


@dataclass
class Flags:
    is_static: bool = False
    is_strict: bool = False

    def merge(self, other: Optional[Flags]) -> Flags:
        if other is None:
            return self

        return Flags(
            is_static=self.is_static or other.is_static,
            is_strict=self.is_strict or other.is_strict,
        )


class BadFlagException(Exception):
    pass


class FlagExtractor(SymbolVisitor):
    is_static: bool
    is_strict: bool

    flag_may_appear: bool
    seen_docstring: bool
    seen_import: bool

    def __init__(self, future_flags: int = 0) -> None:
        super().__init__(future_flags)
        self.is_static = False
        self.is_strict = False

        self.flag_may_appear = True
        self.seen_docstring = False
        self.seen_import = False

    def get_flags(self, node: Union[AST, Sequence[AST]]) -> Flags:
        self.visit(node)
        return Flags(is_static=self.is_static, is_strict=self.is_strict)

    def visit(self, node: Union[AST, Sequence[AST]], *args):
        super().visit(node, *args)

        match node:
            case ast.Expr(ast.Constant(value)) if isinstance(
                value, str
            ) and not self.seen_docstring:
                self.seen_docstring = True
            case ast.ImportFrom(module) if module == "__future__":
                pass
            case ast.Module(_) | ast.Constant(_):
                pass
            case ast.Import(_) as node:
                self._handle_import(node, *args)
            case _:
                self.flag_may_appear = False

    def _handle_import(self, node: Import, scope: Scope) -> None:
        for import_ in node.names:
            name = import_.name

            if name not in ("__static__", "__strict__", "__future__"):
                self.flag_may_appear = False
                continue

            if not self.flag_may_appear:
                raise BadFlagException(
                    f"Cinder flag {name} must be at the top of a file"
                )

            if not isinstance(scope, ModuleScope):
                raise BadFlagException(f"{name} must be a globally namespaced import")

            if len(node.names) > 1:
                raise BadFlagException(
                    f"{name} flag may not be combined with other imports",
                )

            if import_.asname is not None:
                raise BadFlagException(f"{name} flag may not be aliased")

            match name:
                case "__static__":
                    self.is_static = True
                case "__strict__":
                    self.is_strict = True
