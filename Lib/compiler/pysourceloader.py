# Copyright (c) Facebook, Inc. and its affiliates. (http://www.facebook.com)
# pyre-unsafe
import _imp
import importlib
import sys
import zipimport
from importlib.machinery import (
    SourceFileLoader,
    SOURCE_SUFFIXES,
    ExtensionFileLoader,
    SourcelessFileLoader,
    BYTECODE_SUFFIXES,
    FileFinder,
)

from . import compile as python_compile
from .readonly import ReadonlyCodeGenerator


# pyre-fixme[13]: path inherited but not initialized
class PySourceFileLoader(importlib.machinery.SourceFileLoader):
    def source_to_code(self, data, path, *, _optimize=-1):
        """Similar to SourceFileLoader.source_to_code
        but use the python based bytecode generator from
        Lib/compiler/pycodegen.py
        """
        return importlib._bootstrap._call_with_frames_removed(
            python_compile, data, path, "exec", optimize=_optimize
        )


# pyre-fixme[13]: path inherited but not initialized
class ReadonlySourceFileLoader(importlib.machinery.SourceFileLoader):
    def source_to_code(self, data, path, *, _optimize=-1):
        """Similar to SourceFileLoader.source_to_code
        but use the python based bytecode generator from
        Lib/compiler/readonly/codegen.py
        """
        return importlib._bootstrap._call_with_frames_removed(
            python_compile,
            data,
            path,
            "exec",
            optimize=_optimize,
            compiler=ReadonlyCodeGenerator,
        )


def _install_source_loader_helper(source_loader_type):
    extensions = ExtensionFileLoader, _imp.extension_suffixes()
    source = source_loader_type, SOURCE_SUFFIXES
    bytecode = SourcelessFileLoader, BYTECODE_SUFFIXES
    supported_loaders = [extensions, source, bytecode]
    sys.path_hooks[:] = [
        zipimport.zipimporter,
        FileFinder.path_hook(*supported_loaders),
    ]
    sys.path_importer_cache.clear()


def _install_py_loader():
    _install_source_loader_helper(PySourceFileLoader)


def _install_readonly_loader():
    _install_source_loader_helper(ReadonlySourceFileLoader)
