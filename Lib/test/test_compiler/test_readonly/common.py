import ast
from compiler.readonly import (
    readonly_compile,
    ReadonlyCodeGenerator,
    ReadonlyTypeBinder,
)
from compiler.static import StaticCodeGenerator

from ..test_static.common import StaticTestBase as TestBase, TestErrors


class ReadonlyTestBase(TestBase):
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
