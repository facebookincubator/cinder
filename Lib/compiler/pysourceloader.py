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


def _install_py_loader():
    extensions = ExtensionFileLoader, _imp.extension_suffixes()
    source = PySourceFileLoader, SOURCE_SUFFIXES
    bytecode = SourcelessFileLoader, BYTECODE_SUFFIXES
    supported_loaders = [extensions, source, bytecode]
    sys.path_hooks[:] = [
        zipimport.zipimporter,
        FileFinder.path_hook(*supported_loaders),
    ]
    sys.path_importer_cache.clear()
