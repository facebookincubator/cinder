from __future__ import annotations

import sys
from compiler.strict.common import DEFAULT_STUB_PATH
from compiler.strict.loader import StrictSourceFileLoader
from contextlib import contextmanager
from importlib.machinery import FileFinder
from pathlib import Path
from tempfile import TemporaryDirectory
from textwrap import dedent
from typing import Callable, ContextManager, Generator, Type, TypeVar
from unittest import TestCase

ALLOW_LIST = []
EXACT_ALLOW_LIST = []


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
    sys.path_hooks[:] = [FileFinder.path_hook(loader)]
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
    try:
        StrictSourceFileLoader.compiler = None
        StrictSourceFileLoader.ensure_compiler(
            sys.path,
            DEFAULT_STUB_PATH,
            ALLOW_LIST,
            EXACT_ALLOW_LIST,
            None,
            enable_patching=False,
        )
        yield
    finally:
        StrictSourceFileLoader.compiler = None


@contextmanager
def restore_static_symtable() -> Generator[None, None, None]:
    compiler = StrictSourceFileLoader.ensure_compiler(
        sys.path,
        DEFAULT_STUB_PATH,
        ALLOW_LIST,
        EXACT_ALLOW_LIST,
        None,
        enable_patching=False,
    )
    modules = compiler.modules.copy()

    try:
        yield
    finally:
        compiler.modules.clear()
        compiler.modules.update(modules)


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
