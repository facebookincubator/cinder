import ast
import builtins
import cinder
from compiler.readonly import (
    readonly_compile,
    ReadonlyCodeGenerator,
    ReadonlyTypeBinder,
)
from compiler.static import StaticCodeGenerator
from contextlib import contextmanager
from typing import Any, List, NewType, Optional, Tuple

from ..test_static.common import StaticTestBase as TestBase, TestErrors


@contextmanager
def with_detection(detection_func):
    old_handler = cinder.get_immutable_warn_handler()
    cinder.set_immutable_warn_handler(detection_func)
    yield
    cinder.set_immutable_warn_handler(old_handler)
    return


Readonly = NewType("Readonly", object)


class ReadonlyTestBase(TestBase):
    def setUp(self):
        cinder.flush_immutable_warnings()  # make sure no existing warnings interfere with tests

    def tearDown(self):
        cinder.flush_immutable_warnings()

    def compile(
        self,
        code,
        generator=ReadonlyCodeGenerator,
        modname="<module>",
        optimize=0,
        peephole_enabled=True,
        ast_optimizer_enabled=True,
        enable_patching=False,
    ):
        if generator is ReadonlyCodeGenerator:
            tree = ast.parse(self.clean_code(code))
            return readonly_compile(modname, f"{modname}.py", tree, 0, optimize)
        else:
            return super().compile(
                code,
                generator,
                modname,
                optimize,
                peephole_enabled,
                ast_optimizer_enabled,
            )

    def compile_and_run(self, code):
        # find out indent
        index = 0
        while code[index].isspace():
            index += 1

        code = code[:index] + "from __future__ import annotations\n" + code
        errors = self.lint(code)
        self.assertEqual(errors.errors, [])
        compiled = self.compile(code)
        builts = builtins.__dict__
        builts["Readonly"] = Readonly
        d = {"<builtins>": builts}
        exec(compiled, d)
        return d

    def static_compile(
        self,
        code,
        generator=StaticCodeGenerator,
        modname="<module>",
        optimize=0,
        peephole_enabled=True,
        ast_optimizer_enabled=True,
        enable_patching=False,
    ):
        return super().compile(
            code,
            generator,
            modname,
            optimize,
            peephole_enabled,
            ast_optimizer_enabled,
            enable_patching,
        )

    def lint(self, code: str) -> TestErrors:
        code = self.clean_code(code)
        tree = ast.parse(code)
        s = ReadonlyCodeGenerator._SymbolVisitor()
        s.visit(tree)
        type_binder = ReadonlyTypeBinder(tree, "<module>.py", s)
        type_binder.get_types()
        return TestErrors(self, code, type_binder.error_sink.errors)

    def static_lint(self, code: str) -> TestErrors:
        return super().lint(code)

    def get_detection_func(self, errors):
        def detection_func(arg):
            errors.extend(arg)

        return detection_func

    @contextmanager
    def assertNoImmutableErrors(self):
        errors = []
        with with_detection(self.get_detection_func(errors)):
            yield
            cinder.flush_immutable_warnings()
            msg = (
                ""
                if len(errors) == 0
                else f"expected no errors but see error {errors[0][1]}"
            )
            self.assertFalse(errors, msg)

    @contextmanager
    def assertImmutableErrors(
        self, expected_errors: List[Tuple[int, str, Optional[object]]]
    ):
        errors = []
        with with_detection(self.get_detection_func(errors)):
            yield
            cinder.flush_immutable_warnings()
            self.assertTrue(len(errors) > 0, "expected errors but no errors found")
            self.assertEqual(errors, expected_errors)
