# Copyright (c) Meta Platforms, Inc. and affiliates. (http://www.meta.com)

from __future__ import annotations

import _imp
import builtins

import importlib._bootstrap_external
import marshal
import os
import sys
from cinder import StrictModule, watch_sys_modules

from enum import Enum
# pyre-ignore[21]: typeshed doesn't know about this
from importlib import _bootstrap, _pack_uint32
# pyre-ignore[21]: typeshed doesn't know about this
from importlib._bootstrap_external import _classify_pyc, _compile_bytecode, _validate_hash_pyc, _validate_timestamp_pyc
from importlib.abc import Loader
from importlib.machinery import (
    BYTECODE_SUFFIXES,
    EXTENSION_SUFFIXES,
    ExtensionFileLoader,
    FileFinder,
    ModuleSpec,
    SOURCE_SUFFIXES,
    SourceFileLoader,
    SourcelessFileLoader,
)
# pyre-ignore[21]: typeshed doesn't know about _RAW_MAGIC_NUMBER
from importlib.util import cache_from_source, _RAW_MAGIC_NUMBER
from os import getenv, makedirs
from os.path import dirname, isdir
from py_compile import (
    _get_default_invalidation_mode,
    PycInvalidationMode,
    PyCompileError,
)
from types import CodeType, ModuleType
from typing import (
    Callable,
    cast,
    Collection,
    Dict,
    final,
    Iterable,
    Mapping,
    Optional,
    Type,
)

from cinderx.static import install_sp_audit_hook

from ..consts import CO_STATICALLY_COMPILED
from .common import DEFAULT_STUB_PATH, FIXED_MODULES, MAGIC_NUMBER
from .compiler import Compiler, TIMING_LOGGER_TYPE
from .flag_extractor import Flags


# Force immediate resolution of Compiler in case it's deferred from Lazy Imports
Compiler = Compiler


_MAGIC_STRICT_OR_STATIC: bytes = (MAGIC_NUMBER + 2**15).to_bytes(
    2, "little"
) + b"\r\n"
# We don't actually need to increment anything here, because the strict modules
# AST rewrite has no impact on pycs for non-strict modules. So we just always
# use two zero bytes. This simplifies generating "fake" strict pycs for
# known-not-to-be-strict third-party modules.
_MAGIC_NEITHER_STRICT_NOR_STATIC: bytes = (0).to_bytes(2, "little") + b"\r\n"
_MAGIC_LEN: int = len(_MAGIC_STRICT_OR_STATIC)


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
            # There's a test that depends on this being mocked out.
            os.abort()


__builtins__: ModuleType


def _code_to_timestamp_pyc(code: CodeType, mtime: int = 0, source_size: int = 0) -> bytearray:
    "Produce the data for a timestamp-based pyc."
    data = bytearray(MAGIC_NUMBER)
    # pyre-ignore[16]: typeshed doesn't know about this
    data.extend(_pack_uint32(0))
    # pyre-ignore[16]: typeshed doesn't know about this
    data.extend(_pack_uint32(mtime))
    # pyre-ignore[16]: typeshed doesn't know about this
    data.extend(_pack_uint32(source_size))
    data.extend(marshal.dumps(code))
    return data


def _code_to_hash_pyc(code: CodeType, source_hash: bytes, checked: bool = True) -> bytearray:
    "Produce the data for a hash-based pyc."
    data = bytearray(MAGIC_NUMBER)
    flags = 0b1 | checked << 1
    # pyre-ignore[16]: typeshed doesn't know about this
    data.extend(_pack_uint32(flags))
    assert len(source_hash) == 8
    data.extend(source_hash)
    data.extend(marshal.dumps(code))
    return data


class StrictSourceFileLoader(SourceFileLoader):
    strict_or_static: bool = False
    compiler: Optional[Compiler] = None
    module: Optional[ModuleType] = None

    def __init__(
        self,
        fullname: str,
        path: str,
        import_path: Optional[Iterable[str]] = None,
        stub_path: Optional[str] = None,
        allow_list_prefix: Optional[Iterable[str]] = None,
        allow_list_exact: Optional[Iterable[str]] = None,
        enable_patching: bool = False,
        log_source_load: Optional[Callable[[str, Optional[str], bool], None]] = None,
        init_cached_properties: Optional[
            Callable[
                [Mapping[str, str | tuple[str, bool]]],
                Callable[[Type[object]], Type[object]],
            ]
        ] = None,
        log_time_func: Optional[Callable[[], TIMING_LOGGER_TYPE]] = None,
        use_py_compiler: bool = False,
        # The regexes are parsed on the C++ side, so re.Pattern is not accepted.
        allow_list_regex: Optional[Iterable[str]] = None,
    ) -> None:
        self.name = fullname
        self.path = path
        self.import_path: Iterable[str] = import_path or list(sys.path)
        configured_stub_path = sys._xoptions.get("strict-module-stubs-path") or getenv(
            "PYTHONSTRICTMODULESTUBSPATH"
        )
        if stub_path is None:
            stub_path = configured_stub_path or DEFAULT_STUB_PATH
        if stub_path and not isdir(stub_path):
            raise ValueError(f"Strict module stubs path does not exist: {stub_path}")
        self.stub_path: str = stub_path
        self.allow_list_prefix: Iterable[str] = allow_list_prefix or []
        self.allow_list_exact: Iterable[str] = allow_list_exact or []
        self.allow_list_regex: Iterable[str] = allow_list_regex or []
        self.enable_patching = enable_patching
        self.log_source_load: Optional[
            Callable[[str, Optional[str], bool], None]
        ] = log_source_load
        self.bytecode_found = False
        self.bytecode_path: Optional[str] = None
        self.init_cached_properties = init_cached_properties
        self.log_time_func = log_time_func
        self.use_py_compiler = use_py_compiler

    @classmethod
    def ensure_compiler(
        cls,
        path: Iterable[str],
        stub_path: str,
        allow_list_prefix: Iterable[str],
        allow_list_exact: Iterable[str],
        log_time_func: Optional[Callable[[], TIMING_LOGGER_TYPE]],
        enable_patching: bool = False,
        allow_list_regex: Optional[Iterable[str]] = None,
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
                allow_list_regex=allow_list_regex or [],
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
            if magic == _MAGIC_NEITHER_STRICT_NOR_STATIC:
                self.strict_or_static = False
            elif magic == _MAGIC_STRICT_OR_STATIC:
                self.strict_or_static = True
            else:
                # This is a bit ugly: OSError is the only kind of error that
                # get_code() ignores from get_data(). But this is way better
                # than the alternative of copying and modifying everything.
                raise OSError(f"Bad magic number {data[:4]!r} in {path}")
            data = data[_MAGIC_LEN:]
        return data

    # pyre-fixme[14]: `set_data` overrides method defined in `SourceFileLoader`
    #  inconsistently.
    def set_data(self, path: bytes | str, data: bytes, *, _mode=0o666) -> None:
        assert isinstance(path, str)
        if path.endswith(tuple(BYTECODE_SUFFIXES)):
            path = add_strict_tag(path, self.enable_patching)
            magic = (
                _MAGIC_STRICT_OR_STATIC
                if self.strict_or_static
                else _MAGIC_NEITHER_STRICT_NOR_STATIC
            )
            data = magic + data
        return super().set_data(path, data, _mode=_mode)

    def get_code(self, fullname: str) -> CodeType:
        source_path = self.get_filename(fullname)
        source_mtime = None
        source_bytes = None
        source_hash = None
        hash_based = False
        check_source = True
        try:
            bytecode_path = cache_from_source(source_path)
        except NotImplementedError:
            bytecode_path = None
        else:
            try:
                st = self.path_stats(source_path)
            except OSError:
                pass
            else:
                source_mtime = int(st['mtime'])
                try:
                    data = self.get_data(bytecode_path)
                except OSError:
                    pass
                else:
                    exc_details = {
                        'name': fullname,
                        'path': bytecode_path,
                    }
                    try:
                        # pyre-ignore[16]: typeshed doesn't know about this
                        flags = _classify_pyc(data, fullname, exc_details)
                        bytes_data = memoryview(data)[16:]
                        hash_based = flags & 0b1 != 0
                        if hash_based:
                            check_source = flags & 0b10 != 0
                            if (_imp.check_hash_based_pycs != 'never' and
                                (check_source or
                                 _imp.check_hash_based_pycs == 'always')):
                                source_bytes = self.get_data(source_path)
                                source_hash = _imp.source_hash(
                                    # pyre-ignore[16]: typeshed doesn't know about this
                                    _RAW_MAGIC_NUMBER,
                                    source_bytes,
                                )
                                # pyre-ignore[16]: typeshed doesn't know about this
                                _validate_hash_pyc(data, source_hash, fullname,
                                                   exc_details)
                        else:
                            # pyre-ignore[16]: typeshed doesn't know about this
                            _validate_timestamp_pyc(
                                data,
                                source_mtime,
                                st['size'],
                                fullname,
                                exc_details,
                            )
                    except (ImportError, EOFError):
                        pass
                    else:
                        # pyre-ignore[16]: typeshed doesn't know about this
                        _bootstrap._verbose_message('{} matches {}', bytecode_path,
                                                    source_path)
                        # pyre-ignore[16]: typeshed doesn't know about this
                        return _compile_bytecode(bytes_data, name=fullname,
                                                 bytecode_path=bytecode_path,
                                                 source_path=source_path)
        if source_bytes is None:
            source_bytes = self.get_data(source_path)
        code_object = self.source_to_code(source_bytes, source_path)
        # pyre-ignore[16]: typeshed doesn't know about this
        _bootstrap._verbose_message('code object from {}', source_path)
        if (not sys.dont_write_bytecode and bytecode_path is not None and
                source_mtime is not None):
            if hash_based:
                if source_hash is None:
                    # pyre-ignore[16]: typeshed doesn't know about _RAW_MAGIC_NUMBER
                    source_hash = _imp.source_hash(_RAW_MAGIC_NUMBER, source_bytes)
                data = _code_to_hash_pyc(code_object, source_hash, check_source)
            else:
                data = _code_to_timestamp_pyc(code_object, source_mtime,
                                              len(source_bytes))
            try:
                # pyre-ignore[16]: typeshed doesn't know about this
                self._cache_bytecode(source_path, bytecode_path, data)
            except NotImplementedError:
                pass
        return code_object

    def should_force_strict(self) -> bool:
        return False

    # pyre-ignore[40]: Non-static method `source_to_code` cannot override a static
    #  method defined in `importlib.abc.InspectLoader`.
    # pyre-fixme[14]: `source_to_code` overrides method defined in `InspectLoader`
    #  inconsistently.
    def source_to_code(
        self, data: bytes | str, path: str, *, _optimize: int = -1
    ) -> CodeType:
        log_source_load = self.log_source_load
        if log_source_load is not None:
            log_source_load(path, self.bytecode_path, self.bytecode_found)
        # pyre-ignore[28]: typeshed doesn't know about _optimize arg
        code = super().source_to_code(data, path, _optimize=_optimize)
        force = self.should_force_strict()
        if force or "__strict__" in code.co_names or "__static__" in code.co_names:
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
            code, is_valid_strict, is_static = self.ensure_compiler(
                self.import_path,
                self.stub_path,
                self.allow_list_prefix,
                self.allow_list_exact,
                self.log_time_func,
                self.enable_patching,
                self.allow_list_regex,
            ).load_compiled_module_from_source(
                data,
                path,
                self.name,
                opt,
                submodule_search_locations,
                override_flags=Flags(is_strict=force),
            )
            self.strict_or_static = is_valid_strict or is_static
            assert code is not None
            return code

        self.strict_or_static = False
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
            module.__cached__ = cached = add_strict_tag(cached, self.enable_patching)
        spec: Optional[ModuleSpec] = module.__spec__
        if cached and spec and spec.cached:
            spec.cached = cached

        if self.strict_or_static:
            if spec is None:
                raise ImportError(f"Missing module spec for {module.__name__}")

            new_dict = {
                "<fixed-modules>": cast(object, FIXED_MODULES),
                "<builtins>": builtins.__dict__,
                "<init-cached-properties>": self.init_cached_properties,
            }
            if code.co_flags & CO_STATICALLY_COMPILED:
                init_static_python()
                new_dict["<imported-from>"] = code.co_consts[-1]

            new_dict.update(module.__dict__)
            strict_mod = StrictModule(new_dict, self.enable_patching)

            sys.modules[module.__name__] = strict_mod

            exec(code, new_dict)
        else:
            exec(code, module.__dict__)


def add_strict_tag(path: str, enable_patching: bool) -> str:
    base, __, ext = path.rpartition(".")
    enable_patching_marker = ".patch" if enable_patching else ""

    return f"{base}.strict{enable_patching_marker}.{ext}"


def _get_supported_file_loaders() -> list[tuple[Type[Loader], list[str]]]:
    """Returns a list of file-based module loaders.

    Each item is a tuple (loader, suffixes).
    """
    extensions = ExtensionFileLoader, EXTENSION_SUFFIXES
    source = StrictSourceFileLoader, SOURCE_SUFFIXES
    bytecode = SourcelessFileLoader, BYTECODE_SUFFIXES
    return [extensions, source, bytecode]


def strict_compile(
    file: str,
    cfile: str,
    dfile: str | None = None,
    doraise: bool = False,
    optimize: int = -1,
    # Since typeshed doesn't yet know about PycInvalidationMode, no way to
    # convince Pyre it's a valid type here.  T54150924
    invalidation_mode: object = None,
    loader_override: object = None,
    loader_options: Dict[str, str | int | bool] | None = None,
) -> str | None:
    """Byte-compile one Python source file to Python bytecode, using strict loader.

    :param file: The source file name.
    :param cfile: The target byte compiled file name.
    :param dfile: Purported file name, i.e. the file name that shows up in
        error messages.  Defaults to the source file name.
    :param doraise: Flag indicating whether or not an exception should be
        raised when a compile error is found.  If an exception occurs and this
        flag is set to False, a string indicating the nature of the exception
        will be printed, and the function will return to the caller. If an
        exception occurs and this flag is set to True, a PyCompileError
        exception will be raised.
    :param optimize: The optimization level for the compiler.  Valid values
        are -1, 0, 1 and 2.  A value of -1 means to use the optimization
        level of the current interpreter, as given by -O command line options.
    :return: Path to the resulting byte compiled file.

    Copied and modified from https://github.com/python/cpython/blob/3.6/Lib/py_compile.py#L65

    This version does not support cfile=None, since compileall never passes that.

    """

    modname = file
    for dir in sys.path:
        if file.startswith(dir):
            modname = file[len(dir) :]
            break

    modname = modname.replace("/", ".")
    if modname.endswith("__init__.py"):
        modname = modname[: -len("__init__.py")]
    elif modname.endswith(".py"):
        modname = modname[: -len(".py")]
    modname = modname.strip(".")

    if loader_options is None:
        loader_options = {}

    # TODO we ignore loader_override; this module should be ported to Cinder
    loader = StrictSourceFileLoader(
        modname,
        file,
        import_path=sys.path,
        **loader_options,
    )
    source_bytes = loader.get_data(file)
    try:
        code = loader.source_to_code(source_bytes, dfile or file, _optimize=optimize)
    except Exception as err:
        raise
        py_exc = PyCompileError(err.__class__, err, dfile or file)
        if doraise:
            raise py_exc
        else:
            sys.stderr.write(py_exc.msg + "\n")
            return

    makedirs(dirname(cfile), exist_ok=True)

    # Incomplete typeshed stub.  T54150924
    if invalidation_mode is None:
        # Incomplete typeshed stub.  T54150924
        invalidation_mode = _get_default_invalidation_mode()
    if invalidation_mode == PycInvalidationMode.TIMESTAMP:
        source_stats = loader.path_stats(file)
        # pyre-fixme[16]: Module `importlib` has no attribute `_bootstrap_external`.
        bytecode = importlib._bootstrap_external._code_to_timestamp_pyc(
            code, source_stats["mtime"], source_stats["size"]
        )
    else:
        # Incomplete typeshed stub.  T54150924
        source_hash = importlib.util.source_hash(source_bytes)
        # pyre-fixme[16]: Module `importlib` has no attribute `_bootstrap_external`.
        bytecode = importlib._bootstrap_external._code_to_hash_pyc(
            code,
            source_hash,
            (invalidation_mode == PycInvalidationMode.CHECKED_HASH),
        )

    # Incomplete typeshed stub.
    # pyre-fixme[16]: `StrictSourceFileLoader` has no attribute `_cache_bytecode`.
    loader._cache_bytecode(file, cfile, bytecode)
    return cfile


def init_static_python() -> None:
    """Idempotent global initialization of Static Python.

    Should be called at least once if any Static modules/functions exist.
    """
    watch_sys_modules()
    install_sp_audit_hook()


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
    mod.__main__()
