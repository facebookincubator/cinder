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
        # the qualname that should be the "requester" of types used (for dep tracking)
        self._context_qualname: str = ""
        # if true, all deps tracked in visiting should be considered decl deps
        self.force_decl_deps: bool = False

    @property
    def context_qualname(self) -> str:
        return self._context_qualname

    @contextmanager
    def temporary_context_qualname(
        self, qualname: str | None, force_decl: bool = False
    ) -> Generator[None, None, None]:
        old_qualname = self._context_qualname
        self._context_qualname = qualname or ""
        old_decl = self.force_decl_deps
        self.force_decl_deps = force_decl
        try:
            yield
        finally:
            self._context_qualname = old_qualname
            self.force_decl_deps = old_decl

    def record_dependency(self, source: tuple[str, str]) -> None:
        self.module.record_dependency(
            self.context_qualname, source, force_decl=self.force_decl_deps
        )

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
