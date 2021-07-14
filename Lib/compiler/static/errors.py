# Copyright (c) Facebook, Inc. and its affiliates. (http://www.facebook.com)
from __future__ import annotations

import linecache
from ast import Constant
from contextlib import contextmanager
from typing import TYPE_CHECKING

if TYPE_CHECKING:
    from ast import AST
    from typing import Tuple, Optional, Generator, List


class TypedSyntaxError(SyntaxError):

    pass


def error_location(filename: str, node: AST) -> Tuple[int, int, Optional[str]]:
    source_line = linecache.getline(filename, node.lineno)
    return (node.lineno, node.col_offset, source_line or None)


class ErrorSink:
    def __init__(self) -> None:
        self.errors: List[TypedSyntaxError] = []

    def error(self, exception: TypedSyntaxError) -> None:
        raise exception

    def syntax_error(self, msg: str, filename: str, node: AST) -> None:
        lineno, offset, source_line = error_location(filename, node)
        self.error(TypedSyntaxError(msg, (filename, lineno, offset, source_line)))

    @property
    def has_errors(self) -> bool:
        return len(self.errors) > 0

    @contextmanager
    def error_context(self, filename: str, node: AST) -> Generator[None, None, None]:
        """Add error location context to any TypedSyntaxError raised in with block."""
        try:
            yield
        except TypedSyntaxError as exc:
            if exc.filename is None:
                exc.filename = filename
            if (exc.lineno, exc.offset) == (None, None):
                exc.lineno, exc.offset, exc.text = error_location(filename, node)
            self.error(exc)


class CollectingErrorSink(ErrorSink):
    def error(self, exception: TypedSyntaxError) -> None:
        self.errors.append(exception)
