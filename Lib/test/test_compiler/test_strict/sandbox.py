from __future__ import annotations

import sys
from contextlib import contextmanager
from importlib.machinery import FileFinder
from pathlib import Path
from tempfile import TemporaryDirectory
from textwrap import dedent
from typing import Callable, ContextManager, Generator, Type, TypeVar
from unittest import TestCase

import __strict__
from strict_modules.compiler.static import StaticCompiler
from strict_modules.loader import StrictSourceFileLoader


class Sandbox:
    def __init__(self, root: Path) -> None:
        self.root = root

    def write_file(self, relpath: str, contents: str = "") -> Path:
        """Write contents to given relative path and return full path."""
        path = self.root / relpath
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(dedent(contents))
        return path

    def unlink(self, relpath: str) -> None:
        path = self.root / relpath
        path.unlink()


_SandboxT = TypeVar("_SandboxT", bound=Sandbox)


@contextmanager
def file_loader(loader: object) -> Generator[None, None, None]:
    orig_hooks = sys.path_hooks[:]
    sys.path_hooks[:] = [
        FileFinder.path_hook(loader)  # pyre-ignore[6]: Loader type isn't expose
    ]
    sys.path_importer_cache.clear()
    try:
        yield
    finally:
        sys.path_hooks[:] = orig_hooks
        sys.path_importer_cache.clear()


@contextmanager
def restore_sys_modules() -> Generator[None, None, None]:
    orig_modules = sys.modules.copy()
    try:
        yield
    finally:
        sys.modules.clear()
        sys.modules.update(orig_modules)


@contextmanager
def restore_strict_modules() -> Generator[None, None, None]:
    orig_modules = StrictSourceFileLoader.ensure_compiler()._modules.copy()
    try:
        yield
    finally:
        StrictSourceFileLoader.ensure_compiler()._modules.clear()
        StrictSourceFileLoader.ensure_compiler()._modules.update(orig_modules)


@contextmanager
def restore_static_symtable() -> Generator[None, None, None]:
    compiler = StrictSourceFileLoader.ensure_compiler()
    if isinstance(compiler, StaticCompiler):
        modules = compiler.symtable.modules.copy()
    else:
        modules = None

    try:
        yield
    finally:
        if modules is not None and isinstance(compiler, StaticCompiler):
            compiler.symtable.modules.clear()
            compiler.symtable.modules.update(modules)


@contextmanager
def on_sys_path(dir: str) -> Generator[None, None, None]:
    orig_path = sys.path[:]
    sys.path.insert(0, dir)
    try:
        yield
    finally:
        sys.path[:] = orig_path


@contextmanager
def sandbox(cls: Type[_SandboxT] = Sandbox) -> Generator[_SandboxT, None, None]:
    with TemporaryDirectory() as root:
        yield cls(Path(root))


_T = TypeVar("_T")


def use_cm(cm_factory: Callable[[], ContextManager[_T]], testcase: TestCase) -> _T:
    cm = cm_factory()
    ret = cm.__enter__()
    testcase.addCleanup(cm.__exit__, None, None, None)
    return ret
