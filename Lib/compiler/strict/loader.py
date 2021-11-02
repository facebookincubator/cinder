# Copyright (c) Facebook, Inc. and its affiliates. (http://www.facebook.com)
from __future__ import annotations

import builtins
import os
import sys
from enum import Enum
from importlib.abc import Loader
from importlib.machinery import (
    BYTECODE_SUFFIXES,
    EXTENSION_SUFFIXES,
    SOURCE_SUFFIXES,
    ExtensionFileLoader,
    FileFinder,
    ModuleSpec,
    SourceFileLoader,
    SourcelessFileLoader,
)
from types import CodeType, MappingProxyType, ModuleType
from typing import (
    TYPE_CHECKING,
    Any,
    Callable,
    Collection,
    Dict,
    Generic,
    Iterable,
    List,
    Mapping,
    NoReturn,
    Optional,
    Tuple,
    Type,
    TypeVar,
    cast,
    final,
)

from cinder import StrictModule

from .common import FIXED_MODULES, MAGIC_NUMBER
from .compiler import NONSTRICT_MODULE_KIND, Compiler, TIMING_LOGGER_TYPE
from .track_import_call import tracker


# Force immediate resolution of Compiler in case it's deferred from Lazy Imports
Compiler = Compiler


_MAGIC_STRICT: bytes = (MAGIC_NUMBER + 2 ** 15).to_bytes(2, "little") + b"\r\n"
# We don't actually need to increment anything here, because the strict modules
# AST rewrite has no impact on pycs for non-strict modules. So we just always
# use two zero bytes. This simplifies generating "fake" strict pycs for
# known-not-to-be-strict third-party modules.
_MAGIC_NONSTRICT: bytes = (0).to_bytes(2, "little") + b"\r\n"
_MAGIC_LEN: int = len(_MAGIC_STRICT)


@final
class _PatchState(Enum):
    """Singleton used for tracking values which have not yet been patched."""

    Patched = 1
    Deleted = 2
    Unpatched = 3


# Unfortunately module passed in could be a mock object,
# which also has a `patch` method that clashes with the StrictModule method.
# Directly get the function to avoid name clash.


def _set_patch(module: StrictModule, name: str, value: object) -> None:
    type(module).patch(module, name, value)


def _del_patch(module: StrictModule, name: str) -> None:
    type(module).patch_delete(module, name)


@final
class StrictModuleTestingPatchProxy:
    """Provides a proxy object which enables patching of a strict module if the
    module has been loaded with the StrictSourceWithPatchingFileLoader.  The
    proxy can be used as a context manager in which case exiting the with block
    will result in the patches being disabled.  The process will be terminated
    if the patches are not unapplied and the proxy is deallocated."""

    def __init__(self, module: StrictModule) -> None:
        object.__setattr__(self, "module", module)
        object.__setattr__(self, "_patches", {})
        object.__setattr__(self, "__name__", module.__name__)
        object.__setattr__(
            self, "_final_constants", getattr(module, "__final_constants__", ())
        )
        if not type(module).__patch_enabled__.__get__(module, type(module)):
            raise ValueError(f"strict module {module} does not allow patching")

    def __setattr__(self, name: str, value: object) -> None:
        patches = object.__getattribute__(self, "_patches")
        prev_patched = patches.get(name, _PatchState.Unpatched)
        module = object.__getattribute__(self, "module")
        final_constants = object.__getattribute__(self, "_final_constants")
        if name in final_constants:
            raise AttributeError(
                f"Cannot patch Final attribute `{name}` of module `{module.__name__}`"
            )
        if value is prev_patched:
            # We're restoring the previous value
            del patches[name]
        elif prev_patched is _PatchState.Unpatched:
            # We're overwriting a value
            # only set patches[name] when name is patched for the first time
            patches[name] = getattr(module, name, _PatchState.Patched)

        if value is _PatchState.Deleted:
            _del_patch(module, name)
        else:
            _set_patch(module, name, value)

    def __delattr__(self, name: str) -> None:
        StrictModuleTestingPatchProxy.__setattr__(self, name, _PatchState.Deleted)

    def __getattribute__(self, name: str) -> object:
        res = getattr(object.__getattribute__(self, "module"), name)
        return res

    def __enter__(self) -> StrictModuleTestingPatchProxy:
        return self

    def __exit__(self, *excinfo: object) -> None:
        StrictModuleTestingPatchProxy.cleanup(self)

    def cleanup(self, ignore: Optional[Collection[str]] = None) -> None:
        patches = object.__getattribute__(self, "_patches")
        module = object.__getattribute__(self, "module")
        for name, value in list(patches.items()):
            if ignore and name in ignore:
                del patches[name]
                continue
            if value is _PatchState.Patched:
                # value is patched means that module originally
                # does not contain this field.
                try:
                    _del_patch(module, name)
                except AttributeError:
                    pass
                finally:
                    del patches[name]
            else:
                setattr(self, name, value)
        assert not patches

    def __del__(self) -> None:
        patches = object.__getattribute__(self, "_patches")
        if patches:
            print(
                "Patch(es)",
                ", ".join(patches.keys()),
                "failed to be detached from strict module",
                "'" + object.__getattribute__(self, "module").__name__ + "'",
                file=sys.stderr,
            )
            os.abort()


__builtins__: ModuleType


class StrictSourceFileLoader(SourceFileLoader):
    strict: bool = False
    compiler: Optional[Compiler] = None
    module: Optional[ModuleType] = None

    def __init__(
        self,
        fullname: str,
        path: str,
        import_path: Optional[Iterable[str]] = None,
        stub_path: str = "",
        allow_list_prefix: Optional[Iterable[str]] = None,
        allow_list_exact: Optional[Iterable[str]] = None,
        enable_patching: bool = False,
        log_source_load: Optional[Callable[[str, Optional[str], bool], None]] = None,
        track_import_call: bool = False,
        init_cached_properties: Optional[
            Callable[
                [Mapping[str, str | Tuple[str, bool]]],
                Callable[[Type[object]], Type[object]],
            ]
        ] = None,
        log_time_func: Optional[Callable[[], TIMING_LOGGER_TYPE]] = None,
    ) -> None:
        self.name = fullname
        self.path = path
        self.import_path: Iterable[str] = import_path or []
        configured_stub_path = sys._xoptions.get(
            "strict-module-stubs-path"
        ) or os.getenv("PYTHONSTRICTMODULESTUBSPATH")
        if stub_path == "" and configured_stub_path:
            stub_path = str(configured_stub_path)
        if stub_path and not os.path.isdir(stub_path):
            raise ValueError(f"Strict module stubs path does not exist: {stub_path}")
        self.stub_path: str = stub_path
        self.allow_list_prefix: Iterable[str] = allow_list_prefix or []
        self.allow_list_exact: Iterable[str] = allow_list_exact or []
        self.enable_patching = enable_patching
        self.log_source_load: Optional[
            Callable[[str, Optional[str], bool], None]
        ] = log_source_load
        self.bytecode_found = False
        self.bytecode_path: Optional[str] = None
        self.track_import_call = track_import_call
        self.init_cached_properties = init_cached_properties
        self.log_time_func = log_time_func

    @classmethod
    def ensure_compiler(
        cls,
        path: Iterable[str],
        stub_path: str,
        allow_list_prefix: Iterable[str],
        allow_list_exact: Iterable[str],
        log_time_func: Optional[Callable[[], TIMING_LOGGER_TYPE]],
        enable_patching: bool = False,
    ) -> Compiler:
        if (comp := cls.compiler) is None:
            comp = cls.compiler = Compiler(
                path,
                stub_path,
                allow_list_prefix,
                allow_list_exact,
                raise_on_error=True,
                log_time_func=log_time_func,
                enable_patching=enable_patching,
            )
        return comp

    def get_data(self, path: bytes | str) -> bytes:
        assert isinstance(path, str)
        is_pyc = False
        if path.endswith(tuple(BYTECODE_SUFFIXES)):
            is_pyc = True
            path = add_strict_tag(path, self.enable_patching)
            self.bytecode_path = path
        data = super().get_data(path)
        if is_pyc:
            self.bytecode_found = True
            magic = data[:_MAGIC_LEN]
            if magic == _MAGIC_NONSTRICT:
                self.strict = False
            elif magic == _MAGIC_STRICT:
                self.strict = True
            else:
                # This is a bit ugly: OSError is the only kind of error that
                # get_code() ignores from get_data(). But this is way better
                # than the alternative of copying and modifying everything.
                raise OSError(f"Bad magic number {data[:4]!r} in {path}")
            data = data[_MAGIC_LEN:]
        return data

    def set_data(self, path: bytes | str, data: bytes, *, _mode=0o666) -> None:
        assert isinstance(path, str)
        if path.endswith(tuple(BYTECODE_SUFFIXES)):
            path = add_strict_tag(path, self.enable_patching)
            magic = _MAGIC_STRICT if self.strict else _MAGIC_NONSTRICT
            data = magic + data
        return super().set_data(path, data, _mode=_mode)

    # pyre-ignore[40]: Non-static method `source_to_code` cannot override a static
    #  method defined in `importlib.abc.InspectLoader`.
    def source_to_code(
        self, data: bytes | str, path: str, *, _optimize: int = -1
    ) -> CodeType:
        log_source_load = self.log_source_load
        if log_source_load is not None:
            log_source_load(path, self.bytecode_path, self.bytecode_found)
        # pyre-ignore[28]: typeshed doesn't know about _optimize arg
        code = super().source_to_code(data, path, _optimize=_optimize)
        if "__strict__" in code.co_names or "__static__" in code.co_names:
            # Since a namespace package will never call `source_to_code` (there
            # is no source!), there are only two possibilities here: non-package
            # (submodule_search_paths should be None) or regular package
            # (submodule_search_paths should have one entry, the directory
            # containing the "__init__.py").
            submodule_search_locations = None
            if path.endswith("__init__.py"):
                submodule_search_locations = [path[:12]]
            # Usually _optimize will be -1 (which means "default to the value
            # of sys.flags.optimize"). But this default happens very deep in
            # Python's compiler (in PyAST_CompileObject), so if we just pass
            # around -1 and rely on that, it means we can't make any of our own
            # decisions based on that flag. So instead we do the default right
            # here, so we have the correct optimize flag value throughout our
            # compiler.
            opt = sys.flags.optimize if _optimize == -1 else _optimize
            # Let the ast transform attempt to validate the strict module.  This
            # will return an unmodified module if import __strict__ isn't
            # actually at the top-level
            code, mod = self.ensure_compiler(
                self.import_path,
                self.stub_path,
                self.allow_list_prefix,
                self.allow_list_exact,
                self.log_time_func,
                self.enable_patching,
            ).load_compiled_module_from_source(
                data,
                path,
                self.name,
                opt,
                submodule_search_locations,
                self.track_import_call,
            )
            self.strict = mod.module_kind != NONSTRICT_MODULE_KIND
            assert code is not None
            return code
        self.strict = False

        return code

    def exec_module(self, module: ModuleType) -> None:
        # This ends up being slightly convoluted, because create_module
        # gets called, then source_to_code gets called, so we don't know if
        # we have a strict module until after we were requested to create it.
        # So we'll run the module code we get back in the module that was
        # initially published in sys.modules, check and see if it's a strict
        # module, and then run the strict module body after replacing the
        # entry in sys.modules with a StrictModule entry.  This shouldn't
        # really be observable because no user code runs between publishing
        # the normal module in sys.modules and replacing it with the
        # StrictModule.
        code = self.get_code(module.__name__)
        if code is None:
            raise ImportError(
                f"Cannot import module {module.__name__}; get_code() returned None"
            )
        # fix up the pyc path
        cached = getattr(module, "__cached__", None)
        if cached:
            # pyre-ignore[16]: `ModuleType` has no attribute `__cached__`.
            module.__cached__ = cached = add_strict_tag(cached, self.enable_patching)
        spec: Optional[ModuleSpec] = module.__spec__
        if cached and spec and spec.cached:
            spec.cached = cached

        if self.track_import_call:
            tracker.enter_import()

        if self.strict:
            if spec is None:
                raise ImportError(f"Missing module spec for {module.__name__}")

            new_dict = {
                "<fixed-modules>": cast(object, FIXED_MODULES),
                "<builtins>": builtins.__dict__,
                "<init-cached-properties>": self.init_cached_properties,
            }
            new_dict.update(module.__dict__)
            strict_mod = StrictModule(new_dict, self.enable_patching)

            sys.modules[module.__name__] = strict_mod

            exec(code, new_dict)
        else:
            exec(code, module.__dict__)

        if self.track_import_call:
            tracker.exit_import()


def add_strict_tag(path: str, enable_patching: bool) -> str:
    base, __, ext = path.rpartition(".")
    enable_patching_marker = ".patch" if enable_patching else ""

    return f"{base}.strict{enable_patching_marker}.{ext}"


def _get_supported_file_loaders() -> List[Tuple[Loader, List[str]]]:
    """Returns a list of file-based module loaders.

    Each item is a tuple (loader, suffixes).
    """
    extensions = ExtensionFileLoader, EXTENSION_SUFFIXES
    source = StrictSourceFileLoader, SOURCE_SUFFIXES
    bytecode = SourcelessFileLoader, BYTECODE_SUFFIXES
    return cast(List[Tuple[Loader, List[str]]], [extensions, source, bytecode])


def install() -> None:
    """Installs a loader which is capable of loading and validating strict modules"""
    supported_loaders = _get_supported_file_loaders()

    for index, hook in enumerate(sys.path_hooks):
        if not isinstance(hook, type):
            sys.path_hooks.insert(index, FileFinder.path_hook(*supported_loaders))
            break
    else:
        sys.path_hooks.insert(0, FileFinder.path_hook(*supported_loaders))

    # We need to clear the path_importer_cache so that our new FileFinder will
    # start being used for existing directories we've loaded modules from.
    sys.path_importer_cache.clear()


if __name__ == "__main__":
    install()
    del sys.argv[0]
    mod: object = __import__(sys.argv[0])
    if not isinstance(mod, StrictModule):
        raise TypeError(
            "compiler.strict.loader should be used to run strict modules: "
            + type(mod).__name__
        )
    mod.__main__()  # pyre-ignore[16]: `object` has no attribute `__main__`.
