# Copyright (c) Meta Platforms, Inc. and affiliates. (http://www.meta.com)

from __future__ import annotations

import linecache
from contextlib import contextmanager
from typing import TYPE_CHECKING

if TYPE_CHECKING:
    from ast import AST
    from typing import Generator, List, Optional, Tuple


class TypedSyntaxError(SyntaxError):
    pass


class PerfWarning(Warning):
    def __init__(
        self,
        msg: str,
        filename: str,
        lineno: int,
        offset: int,
        text: Optional[str],
    ) -> None:
        super().__init__(msg, filename, lineno, offset, text)
        self.msg = msg
        self.filename = filename
        self.lineno = lineno
        self.offset = offset
        self.text = text


def error_location(filename: str, node: AST) -> Tuple[int, int, Optional[str]]:
    source_line = linecache.getline(filename, node.lineno)
    return (node.lineno, node.col_offset, source_line or None)


class ErrorSink:
    throwing = True

    def __init__(self) -> None:
        self.errors: List[TypedSyntaxError] = []
        self.warnings: List[PerfWarning] = []

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

    def warn(self, warning: PerfWarning) -> None:
        pass

    def perf_warning(self, msg: str, filename: str, node: AST) -> None:
        lineno, offset, source_line = error_location(filename, node)
        self.warn(PerfWarning(msg, filename, lineno, offset, source_line))

    @property
    def has_warnings(self) -> bool:
        return len(self.warnings) > 0


class CollectingErrorSink(ErrorSink):
    throwing = False

    def error(self, exception: TypedSyntaxError) -> None:
        self.errors.append(exception)

    def warn(self, warning: PerfWarning) -> None:
        self.warnings.append(warning)
