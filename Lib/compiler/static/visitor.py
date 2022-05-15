from __future__ import annotations

from ast import AST
from contextlib import contextmanager, nullcontext
from typing import (
    ContextManager,
    Generator,
    Generic,
    Optional,
    Sequence,
    TYPE_CHECKING,
    TypeVar,
    Union,
)

from ..visitor import ASTVisitor

if TYPE_CHECKING:
    from ..errors import ErrorSink
    from .compiler import Compiler
    from .module_table import ModuleTable
    from .types import TypeEnvironment


TVisitRet = TypeVar("TVisitRet", covariant=True)


class GenericVisitor(ASTVisitor, Generic[TVisitRet]):
    def __init__(self, module: ModuleTable) -> None:
        super().__init__()
        self.module = module
        self.module_name: str = module.name
        self.filename: str = module.filename
        self.compiler: Compiler = module.compiler
        self.error_sink: ErrorSink = module.compiler.error_sink
        self.type_env: TypeEnvironment = module.compiler.type_env

    def visit(self, node: Union[AST, Sequence[AST]], *args: object) -> TVisitRet:
        # if we have a sequence of nodes, don't catch TypedSyntaxError here;
        # walk_list will call us back with each individual node in turn and we
        # can catch errors and add node info then.
        ctx = self.error_context(node) if isinstance(node, AST) else nullcontext()
        with ctx:
            return super().visit(node, *args)

    def syntax_error(self, msg: str, node: AST) -> None:
        return self.error_sink.syntax_error(msg, self.filename, node)

    def perf_warning(self, msg: str, node: AST) -> None:
        return self.error_sink.perf_warning(msg, self.filename, node)

    def error_context(self, node: Optional[AST]) -> ContextManager[None]:
        if node is None:
            return nullcontext()
        return self.error_sink.error_context(self.filename, node)

    @contextmanager
    def temporary_error_sink(self, sink: ErrorSink) -> Generator[None, None, None]:
        orig_sink = self.error_sink
        self.error_sink = sink
        try:
            yield
        finally:
            self.error_sink = orig_sink
