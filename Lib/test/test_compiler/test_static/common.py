from __future__ import annotations

import ast
import asyncio
import builtins
import cinder
import gc
import re
import symtable
import sys
from cinder import StrictModule
from compiler.dis_stable import Disassembler
from compiler.errors import (
    CollectingErrorSink,
    ErrorSink,
    PerfWarning,
    TypedSyntaxError,
)
from compiler.static import Static38CodeGenerator, StaticCodeGenerator
from compiler.static.compiler import Compiler
from compiler.static.module_table import ModuleTable
from compiler.static.types import Value
from compiler.strict.common import FIXED_MODULES
from compiler.strict.compiler import Compiler as StrictCompiler
from compiler.strict.runtime import set_freeze_enabled
from contextlib import contextmanager
from functools import wraps
from types import CodeType
from typing import Any, ContextManager, Dict, Generator, List, Mapping, Tuple, Type

from _static import (
    TYPED_BOOL,
    TYPED_INT16,
    TYPED_INT32,
    TYPED_INT64,
    TYPED_INT8,
    TYPED_UINT16,
    TYPED_UINT32,
    TYPED_UINT64,
    TYPED_UINT8,
)
from _strictmodule import (
    NONSTRICT_MODULE_KIND,
    STATIC_MODULE_KIND,
    StrictAnalysisResult,
    STUB_KIND_MASK_NONE,
    STUB_KIND_MASK_STRICT,
)
from test.support import maybe_get_event_loop_policy

from ..common import CompilerTest

try:
    import cinderjit
except ImportError:
    cinderjit = None


def type_mismatch(from_type: str, to_type: str) -> str:
    return re.escape(f"type mismatch: {from_type} cannot be assigned to {to_type}")


def bad_ret_type(from_type: str, to_type: str) -> str:
    return re.escape(f"return type must be {to_type}, not {from_type}")


def disable_hir_inliner(f):
    if not cinderjit:
        return f

    @wraps(f)
    def impl(*args, **kwargs):
        enabled = cinderjit.is_hir_inliner_enabled()
        if enabled:
            cinderjit.disable_hir_inliner()
        result = f(*args, **kwargs)
        if enabled:
            cinderjit.enable_hir_inliner()
        return result

    return impl


PRIM_NAME_TO_TYPE = {
    "cbool": TYPED_BOOL,
    "int8": TYPED_INT8,
    "int16": TYPED_INT16,
    "int32": TYPED_INT32,
    "int64": TYPED_INT64,
    "uint8": TYPED_UINT8,
    "uint16": TYPED_UINT16,
    "uint32": TYPED_UINT32,
    "uint64": TYPED_UINT64,
}


def dis(code):
    Disassembler().dump_code(code, file=sys.stdout)


class TestCompiler(Compiler):
    def __init__(
        self,
        source_by_name: Mapping[str, str],
        test_case: StaticTestBase,
        code_generator: Type[Static38CodeGenerator] = StaticCodeGenerator,
        error_sink: ErrorSink | None = None,
        strict_modules: bool = False,
        enable_patching: bool = False,
    ) -> None:
        self.source_by_name = source_by_name
        self.test_case = test_case
        self.strict_modules = strict_modules
        self.enable_patching = enable_patching
        super().__init__(code_generator, error_sink)

    def import_module(self, name: str, optimize: int = 0) -> ModuleTable | None:
        if name not in self.modules:
            tree = self._get_module_ast(name)
            if tree is not None:
                self.add_module(name, self._get_filename(name), tree, optimize)
        return self.modules.get(name)

    def compile_module(self, name: str, optimize: int = 0) -> CodeType:
        tree = self._get_module_ast(name)
        if tree is None:
            raise ValueError(f"No source found for module '{name}'")
        return self.compile(name, self._get_filename(name), tree, optimize=optimize)

    def _get_module_ast(self, name: str) -> ast.Module | None:
        source = self.source_by_name.get(name)
        if source is not None:
            return ast.parse(source)

    @contextmanager
    def in_module(self, modname: str, optimize: int = 0) -> Generator[Any, None, None]:
        for mod in self.gen_modules(optimize):
            if mod.__name__ == modname:
                yield mod
                break
        else:
            raise Exception(f"No module named '{modname}' found")

    def gen_modules(self, optimize: int = 0) -> Generator[Any, None, None]:
        names = self.source_by_name.keys()
        compiled = [self.compile_module(name, optimize) for name in names]
        try:
            dicts = []
            for name, codeobj in zip(names, compiled):
                d, m = self._in_module(name, codeobj)
                dicts.append(d)
                yield m
        finally:
            for name, d in zip(names, dicts):
                self.test_case._finalize_module(name, d)

    def _in_module(self, name: str, codeobj: CodeType):
        if self.strict_modules:
            return self.test_case._in_strict_module(
                name, codeobj, enable_patching=self.enable_patching
            )
        return self.test_case._in_module(name, codeobj)

    def type_error(
        self, name: str, pattern: str, at: str | None = None
    ) -> TestCompiler:
        source = self.source_by_name[name]
        with self.test_case.type_error_ctx(source, pattern, at):
            self.compile_module(name)
        return self

    def revealed_type(self, name: str, type: str) -> TestCompiler:
        source = self.source_by_name[name]
        with self.test_case.revealed_type_ctx(source, type):
            self.compile_module(name)
        return self

    def _get_filename(self, name: str) -> str:
        return name.split(".")[-1] + ".py"


def add_fixed_module(d) -> None:
    d["<fixed-modules>"] = FIXED_MODULES


class ErrorMatcher:
    def __init__(self, msg: str, at: str | None = None) -> None:
        self.msg = msg
        self.at = at


class TestErrors:
    def __init__(
        self,
        case: StaticTestBase,
        code: str,
        errors: List[TypedSyntaxError],
        warnings: List[PerfWarning] = [],
    ) -> None:
        self.case = case
        self.code = code
        self.errors = errors
        self.warnings = warnings

    def _check(
        self,
        kind: str,
        errors: List[TypedSyntaxError] | List[PerfWarning],
        *matchers: List[ErrorMatcher],
        loc_only: bool = False,
    ) -> None:
        self.case.assertEqual(
            len(matchers),
            len(errors),
            f"Expected {len(matchers)} {kind}s, got {errors}",
        )
        for exc, matcher in zip(errors, matchers):
            if not loc_only:
                self.case.assertIn(matcher.msg, str(exc))
            if (at := matcher.at) is not None:
                actual = self.code.split("\n")[exc.lineno - 1][exc.offset :]
                if not actual.startswith(at):
                    self.case.fail(
                        f"Expected {kind} '{matcher.msg}' at '{at}', occurred at '{actual}'"
                    )

    def check(self, *matchers: List[ErrorMatcher], loc_only: bool = False) -> None:
        self._check("error", self.errors, *matchers, loc_only=loc_only)

    def check_warnings(
        self, *matchers: List[ErrorMatcher], loc_only: bool = False
    ) -> None:
        self._check("warning", self.warnings, *matchers, loc_only=loc_only)

    def match(self, msg: str, at: str | None = None) -> ErrorMatcher:
        return ErrorMatcher(msg, at)


class StaticTestBase(CompilerTest):
    def shortDescription(self):
        return None

    def compile(
        self,
        code,
        generator=StaticCodeGenerator,
        modname="<module>",
        optimize=0,
        ast_optimizer_enabled=True,
        enable_patching=False,
    ):
        assert ast_optimizer_enabled

        if generator is not StaticCodeGenerator:
            return super().compile(
                code,
                generator,
                modname,
                optimize,
                ast_optimizer_enabled,
            )

        compiler = Compiler(StaticCodeGenerator)
        tree = ast.parse(self.clean_code(code))
        return compiler.compile(
            modname, f"{modname}.py", tree, optimize, enable_patching=enable_patching
        )

    def get_strict_compiler(self, enable_patching=False) -> StrictCompiler:
        return StrictCompiler(
            [],
            "",
            [],
            [],
            raise_on_error=True,
            enable_patching=enable_patching,
            loader_factory=StaticTestsStrictModuleLoader,
        )

    def compile_strict(
        self, codestr, modname="<module>", optimize=0, enable_patching=False
    ):
        compiler = self.get_strict_compiler(enable_patching=enable_patching)

        code, is_valid_strict = compiler.load_compiled_module_from_source(
            self.clean_code(codestr), f"{modname}.py", modname, optimize
        )
        assert is_valid_strict
        return code

    def lint(self, code: str) -> TestErrors:
        errors = CollectingErrorSink()
        code = self.clean_code(code)
        tree = ast.parse(code)
        compiler = Compiler(StaticCodeGenerator, errors)
        compiler.bind("<module>", "<module>.py", tree, optimize=0)
        return TestErrors(self, code, errors.errors, errors.warnings)

    def perf_lint(self, code: str) -> TestErrors:
        errors = CollectingErrorSink()
        code = self.clean_code(code)
        tree = ast.parse(code)
        compiler = Compiler(StaticCodeGenerator, errors)
        tree, s = compiler._bind("<module>", "<module>.py", tree, optimize=0)
        if not errors.errors:
            graph = StaticCodeGenerator.flow_graph(
                "<module>", "<module>.py", s.scopes[tree]
            )
            code_gen = StaticCodeGenerator(None, tree, s, graph, compiler, "<module>")
            code_gen.visit(tree)
        return TestErrors(self, code, errors.errors, errors.warnings)

    @contextmanager
    def type_error_ctx(
        self,
        code: str,
        pattern: str,
        at: str | None = None,
        lineno: int | None = None,
        offset: int | None = None,
    ) -> Generator[None, None, None]:
        with self.assertRaisesRegex(TypedSyntaxError, pattern) as ctx:
            yield
        exc = ctx.exception
        errors = TestErrors(self, self.clean_code(code), [ctx.exception])
        if at is not None:
            errors.check(ErrorMatcher(pattern, at), loc_only=True)
        if lineno is not None:
            self.assertEqual(exc.lineno, lineno)
        if offset is not None:
            self.assertEqual(exc.offset, offset)

    def revealed_type_ctx(self, code: str, type: str) -> ContextManager[None]:
        return self.type_error_ctx(
            code, rf"reveal_type\(.+\): '{re.escape(type)}'", at="reveal_type("
        )

    def type_error(
        self,
        code: str,
        pattern: str,
        at: str | None = None,
        lineno: int | None = None,
        offset: int | None = None,
    ) -> None:
        with self.type_error_ctx(code, pattern, at, lineno, offset):
            self.compile(code)

    def revealed_type(self, code: str, type: str) -> None:
        with self.revealed_type_ctx(code, type):
            self.compile(code)

    def _clean_sources(self, sources: Dict[str, str]) -> Dict[str, str]:
        return {name: self.clean_code(code) for name, code in sources.items()}

    def compiler(self, **sources: str) -> TestCompiler:
        return TestCompiler(self._clean_sources(sources), self)

    def strict_compiler(self, **sources: str) -> TestCompiler:
        return TestCompiler(self._clean_sources(sources), self, strict_modules=True)

    def strict_patch_compiler(self, **sources: str) -> TestCompiler:
        return TestCompiler(
            self._clean_sources(sources),
            self,
            strict_modules=True,
            enable_patching=True,
        )

    _temp_mod_num = 0

    def _temp_mod_name(self):
        StaticTestBase._temp_mod_num += 1
        return sys._getframe().f_back.f_back.f_back.f_code.co_name + str(
            StaticTestBase._temp_mod_num
        )

    def _finalize_module(self, name, mod_dict=None):
        if name in sys.modules:
            del sys.modules[name]
        if mod_dict is not None:
            mod_dict.clear()
        gc.collect()

    def _in_module(self, name, code_obj):
        m = type(sys)(name)
        d = m.__dict__
        d["<builtins>"] = builtins.__dict__
        add_fixed_module(d)
        sys.modules[name] = m
        exec(code_obj, d)
        d["__name__"] = name
        return d, m

    @contextmanager
    def with_freeze_type_setting(self, freeze: bool):
        old_setting = set_freeze_enabled(freeze)
        try:
            yield
        finally:
            set_freeze_enabled(old_setting)

    @contextmanager
    def in_module(
        self,
        code,
        name=None,
        code_gen=StaticCodeGenerator,
        optimize=0,
        freeze=False,
        enable_patching=False,
        dump_bytecode=False,
    ):
        d = None
        if name is None:
            name = self._temp_mod_name()
        old_setting = set_freeze_enabled(freeze)
        try:
            compiled = self.compile(
                code, code_gen, name, optimize, enable_patching=enable_patching
            )
            if dump_bytecode:
                dis(compiled)
            d, m = self._in_module(name, compiled)
            yield m
        finally:
            set_freeze_enabled(old_setting)
            self._finalize_module(name, d)

    def _in_strict_module(
        self,
        name,
        code_obj,
        enable_patching=False,
    ):
        d = {
            "__name__": name,
            "<builtins>": builtins.__dict__,
            "<imported-from>": code_obj.co_consts[-1],
        }
        add_fixed_module(d)
        m = StrictModule(d, enable_patching)
        sys.modules[name] = m
        exec(code_obj, d)
        return d, m

    @contextmanager
    def in_strict_module(
        self,
        code,
        name=None,
        optimize=0,
        enable_patching=False,
        freeze=True,
        dump_bytecode=False,
    ):
        d = None
        if name is None:
            name = self._temp_mod_name()
        old_setting = set_freeze_enabled(freeze)
        try:
            compiled = self.compile_strict(
                code, name, optimize, enable_patching=enable_patching
            )
            if dump_bytecode:
                dis(compiled)
            d, m = self._in_strict_module(name, compiled, enable_patching)
            yield m
        finally:
            set_freeze_enabled(old_setting)
            self._finalize_module(name, d)

    def _run_code(self, code, generator, modname):
        if modname is None:
            modname = self._temp_mod_name()
        compiled = self.compile(code, generator, modname)
        d = {"<builtins>": builtins.__dict__}
        add_fixed_module(d)
        exec(compiled, d)
        return modname, d

    def run_code(self, code, generator=StaticCodeGenerator, modname=None):
        _, r = self._run_code(code, generator, modname)
        return r

    @property
    def base_size(self):
        class C:
            __slots__ = ()

        return C().__sizeof__()

    @property
    def ptr_size(self):
        return 8 if sys.maxsize > 2**32 else 4

    def assert_jitted(self, func):
        if cinderjit is None:
            return

        self.assertTrue(cinderjit.is_jit_compiled(func), func.__name__)

    def assert_not_jitted(self, func):
        if cinderjit is None:
            return

        self.assertFalse(cinderjit.is_jit_compiled(func))

    def assert_not_jitted(self, func):
        if cinderjit is None:
            return

        self.assertFalse(cinderjit.is_jit_compiled(func))

    def setUp(self):
        # ensure clean classloader/vtable slate for all tests
        cinder.clear_classloader_caches()
        # ensure our async tests don't change the event loop policy
        policy = maybe_get_event_loop_policy()
        self.addCleanup(lambda: asyncio.set_event_loop_policy(policy))

    def subTest(self, **kwargs):
        cinder.clear_classloader_caches()
        return super().subTest(**kwargs)

    def make_async_func_hot(self, func):
        async def make_hot():
            for i in range(50):
                await func()

        asyncio.run(make_hot())

    def assertReturns(self, code: str, typename: str) -> None:
        actual = self.bind_final_return(code).name
        self.assertEqual(actual, typename)

    def bind_final_return(self, code: str) -> Value:
        mod, comp = self.bind_module(code)
        types = comp.modules["foo"].types
        node = mod.body[-1].body[-1].value
        return types[node]

    def bind_stmt(
        self,
        code: str,
        optimize: bool = False,
        getter=lambda stmt: stmt,
    ) -> Tuple[ast.stmt, Compiler]:
        mod, comp = self.bind_module(code, optimize)
        types = comp.modules["foo"].types
        return types[getter(mod.body[-1])], comp

    def bind_expr(
        self,
        code: str,
        optimize: bool = False,
    ) -> Tuple[Value, Compiler]:
        mod, comp = self.bind_module(code, optimize)
        types = comp.modules["foo"].types
        return types[mod.body[-1].value], comp

    def bind_module(
        self,
        code: str,
        optimize: int = 0,
    ) -> Tuple[ast.Module, Compiler]:
        tree = ast.parse(self.clean_code(code))

        compiler = Compiler(StaticCodeGenerator)
        tree, s = compiler._bind("foo", "foo.py", tree, optimize=optimize)

        # Make sure we can compile the code, just verifying all nodes are
        # visited.
        graph = StaticCodeGenerator.flow_graph("foo", "foo.py", s.scopes[tree])
        code_gen = StaticCodeGenerator(None, tree, s, graph, compiler, "foo", optimize)
        code_gen.visit(tree)

        return tree, compiler


class StaticTestsStrictModuleLoader:
    """Fake StrictModuleLoader for Static Python tests.

    Allows running code through strict rewrite without actually doing a
    full strict analysis on it.
    """

    def __init__(
        self,
        _import_paths: List[str],
        _stub_import_path: str,
        _allow_list: List[str],
        _allow_list_exact: List[str],
        _load_strictmod_builtin: bool = True,
        _allow_list_regex: List[str] | None = None,
        _verbose_logs: bool = False,
        _disable_analysis: bool = False,
        /,
    ) -> None:
        pass

    def check_source(
        self,
        _source: str | bytes,
        _file_name: str,
        _mod_name: str,
        _submodule_search_locations: List[str],
        /,
    ) -> StrictAnalysisResult:
        return self._get_result(
            _source,
            _mod_name,
            _file_name,
            STATIC_MODULE_KIND,
            STUB_KIND_MASK_NONE,
        )

    def check(self, _mod_name: str, /) -> StrictAnalysisResult:
        return self._get_result(
            "",
            _mod_name,
            f"{_mod_name}.py",
            NONSTRICT_MODULE_KIND,
            STUB_KIND_MASK_STRICT,
        )

    def _get_result(
        self,
        source: str | bytes,
        modname: str,
        filename: str,
        mod_kind: int,
        stub_kind: int,
    ) -> StrictAnalysisResult:
        tree = ast.parse(source)
        symbols = symtable.symtable(source, filename, "exec")
        return StrictAnalysisResult(
            modname,
            filename,
            mod_kind,
            stub_kind,
            tree,
            tree,
            symbols._table,
            [],
        )
