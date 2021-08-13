import ast
from textwrap import dedent
from typing import Optional, Sequence, final

from strict_modules.abstract import AbstractModule, AbstractUnknown, CallerContext
from strict_modules.common import CollectingErrorSink, ErrorSink
from strict_modules.compiler.compiler import (
    Compiler,
    StrictModuleBadStrictFlag,
    get_module_kind,
)
from strict_modules.compiler.modules import ModuleKind
from strict_modules.exceptions import UnknownValueAttributeException
from strict_modules.loader import StaticCompiler
from strict_modules.tests.base import get_implicit_source_from_file
from strict_modules.tests.sandbox import sandbox
from testing.unittest import UnitTest
from testing.utils import data_provider, patch


FakeModule = AbstractModule("<fake>", {})

TestErrorSink = ErrorSink()


class CompilerTests(UnitTest):
    ONCALL_SHORTNAME = "strictmod"

    compiler = Compiler

    def test_bad_strict_flag(self) -> None:
        code = "import __strict__ as foo"
        errors = CollectingErrorSink()
        compiler = self.compiler(
            import_path=[], error_factory=lambda: errors, support_cache=False
        )
        compiler.load_from_source(code, "test.py", "")
        self.assertEqual(
            list(map(str, errors.errors)),
            [str(StrictModuleBadStrictFlag("__strict__ import cannot be aliased", 1))],
        )

    def analyze(
        self,
        code: str,
        errors: Optional[ErrorSink] = None,
        import_path: Optional[Sequence[str]] = None,
    ) -> AbstractModule:
        code = dedent(code)
        compiler = self.compiler(
            import_path=import_path or [],
            error_factory=lambda: errors or ErrorSink(),
            support_cache=False,
        )
        module = compiler.load_from_source(code, "<input>", code)
        value = module.value
        assert value is not None
        return value

    def assert_analysis(self, code: str, var_names: Sequence[str]) -> None:
        env = {}
        exec(dedent(code), env)
        module = self.analyze(code)
        for _var in var_names:
            abstract_value = module.dict[_var]
            python_value = env.get(_var)
            self.assertEqual(
                abstract_value.get_primary_value(
                    CallerContext(FakeModule, "", 0, TestErrorSink)
                ),
                python_value,
                _var,
            )

    def test_abc(self) -> None:
        code = """
            import __strict__
            from abc import ABC, abstractmethod, abstractclassmethod, abstractstaticmethod, abstractproperty

            class A(ABC):
                @abstractclassmethod
                def cm(self):
                    pass
                @abstractmethod
                def f(self):
                    pass
                @abstractstaticmethod
                def sm():
                    pass
                @abstractproperty
                def prop(self):
                    pass


            a  = A.f.__isabstractmethod__
            b = A.cm.__isabstractmethod__
            c = A.sm.__isabstractmethod__
            d = A.prop.__isabstractmethod__
        """
        self.assert_analysis(code, ["a", "b", "c", "d"])

    def test_abc_override(self) -> None:
        code = """
            import __strict__
            from abc import ABC, abstractmethod

            class Abstract(ABC):
                @abstractmethod
                def f(self):
                    ...

            class Concrete(Abstract):
                def f(self):
                    return 42

            a = Concrete().f()
        """
        self.assert_analysis(code, ["a"])

    def test_enum(self) -> None:
        code = """
            import __strict__
            from enum import IntEnum

            class C(IntEnum):
                X = 1
                Y = 2

            xn = C.X.name
            yn = C.Y.name
            xv = C.X.value
            yv = C.Y.value
        """

        self.assert_analysis(code, ["xn", "yn", "xv", "yv"])

    def test_str_encode(self) -> None:
        code = """
        import __strict__

        Y: bytes = "X".encode()
        """
        self.assert_analysis(code, ["Y"])

    def test_inspect(self) -> None:
        code = """
            import __strict__
            import inspect

            def f(a, b, c): pass

            isfunc = inspect.isfunction(f)
            isnotfunc = inspect.isfunction(42)
            params = list(inspect.signature(f).parameters)
        """

        self.assert_analysis(code, ["isfunc", "isnotfunc", "params"])

    @patch("strict_modules.compiler.compiler.ALLOW_LIST", ["a"])
    def test_pyi_stub(self) -> None:
        with sandbox() as sbx:
            sbx.write_file(
                "a.pyi",
                """
                class C:
                    x = 1
                """,
            )
            code = """
            import __strict__
            from a import C
            x = C.x
            """
            self.analyze(code, import_path=[str(sbx.root)])

    @patch("strict_modules.compiler.compiler.ALLOW_LIST", ["a"])
    def test_allowlisted_stub_incomplete(self) -> None:
        with sandbox() as sbx:
            sbx.write_file(
                "a.pyi",
                """
                class C:
                    x = 1
                z = unknown.x
                """,
            )
            code = """
            import __strict__
            from a import C, z
            x = C.x
            """
            mod = self.analyze(code, import_path=[str(sbx.root)])
            self.assertEqual(
                mod.dict["x"].get_primary_value(
                    CallerContext(FakeModule, "", 0, TestErrorSink)
                ),
                1,
            )
            self.assertTrue(isinstance(mod.dict["z"], AbstractUnknown))

    @patch("strict_modules.compiler.compiler.ALLOW_LIST", ["b.a", "b"])
    def test_allowlisted_stub_qualified_access(self) -> None:
        with sandbox() as sbx:
            sbx.write_file(
                "b/a.py",
                """
                class C:
                    x = 1
                """,
            )
            code = """
            import __strict__
            import b.a
            x = b.a.C.x
            """
            mod = self.analyze(code, import_path=[str(sbx.root)])
            self.assertEqual(
                mod.dict["x"].get_primary_value(
                    CallerContext(FakeModule, "", 0, TestErrorSink)
                ),
                1,
            )

    @patch("strict_modules.compiler.compiler.ALLOW_LIST", ["c", "c.b"])
    def test_allowlisted_stub_qualified_access_nested(self) -> None:
        with sandbox() as sbx:
            sbx.write_file(
                "c/b/a.py",
                """
                import __strict__
                class C:
                    x = 1
                """,
            )
            code = """
            import __strict__
            import c.b.a
            x = c.b.a.C.x
            """
            mod = self.analyze(code, import_path=[str(sbx.root)])
            self.assertEqual(
                mod.dict["x"].get_primary_value(
                    CallerContext(FakeModule, "", 0, TestErrorSink)
                ),
                1,
            )

    @patch("strict_modules.compiler.compiler.ALLOW_LIST", ["b.a"])
    def test_allowlisted_stub_qualified_access_not_fully_allowlisted(self) -> None:
        """
        Module b is not on the allow list and therefore accessing
        qualified names from it is not allowed
        """
        with sandbox() as sbx:
            sbx.write_file(
                "b/a.py",
                """
                class C:
                    x = 1
                """,
            )
            code = """
            import __strict__
            import b.a
            x = b.a.C.x
            """
            with self.assertRaises(UnknownValueAttributeException):
                self.analyze(code, import_path=[str(sbx.root)])


@final
class StaticCompilerTests(CompilerTests):
    ONCALL_SHORTNAME = "strictmod"

    compiler = StaticCompiler

    def analyze(
        self,
        code: str,
        errors: Optional[ErrorSink] = None,
        import_path: Optional[Sequence[str]] = None,
    ) -> AbstractModule:
        code = dedent(code)
        compiler = self.compiler(
            import_path=import_path or [],
            error_factory=lambda: errors or ErrorSink(),
            support_cache=False,
        )
        compiler.load_from_source(
            get_implicit_source_from_file("__static__"), "__static__.pys", "__static__"
        )
        module = compiler.load_from_source(code, "<input>", code)
        value = module.value
        if value is None:
            raise TypeError("expected AbstractModule")
        return value

    def test_static(self) -> None:
        code = """
            import __static__
            from __static__ import int8, box

            def f():
                x: int8 = 0
                return box(x)

            x = f()
        """

        self.assert_analysis(code, ["x"])


@final
class GetModuleKindTest(UnitTest):
    ONCALL_SHORTNAME = "strictmod"

    def _get_kind(self, code: str) -> ModuleKind:
        root = ast.parse(dedent(code))
        return get_module_kind(root)

    @data_provider(
        [
            (
                """
                import __strict__
                x = 1
                """,
                ModuleKind.Strict,
            ),
            (
                """
                import __static__
                x = 1
                """,
                ModuleKind.StrictStatic,
            ),
            (
                """
                import __static__, __strict__
                x = 1
                """,
                StrictModuleBadStrictFlag(
                    "__static__ flag may not be combined with other imports", 2
                ),
            ),
            ("x = 1", ModuleKind.Normal),
            (
                """
                '''First docstring.'''
                '''Second "docstring."'''
                import __strict__
                """,
                StrictModuleBadStrictFlag(
                    "__strict__ flag must be at top of module", 4
                ),
            ),
            (
                """
                '''Module docstring.'''
                import __strict__
                """,
                ModuleKind.Strict,
            ),
            (
                """
                '''Module docstring.'''
                # A comment
                import __strict__
                """,
                ModuleKind.Strict,
            ),
            (
                """
                import foo
                import __strict__
                """,
                StrictModuleBadStrictFlag(
                    "__strict__ flag must be at top of module", 3
                ),
            ),
            (
                """
                from __future__ import annotations
                import __strict__
                """,
                ModuleKind.Strict,
            ),
            (
                """
                import __strict__
                import foo
                import __strict__
                """,
                StrictModuleBadStrictFlag(
                    "__strict__ flag must be at top of module", 4
                ),
            ),
            (
                "import __strict__, foo",
                StrictModuleBadStrictFlag(
                    "__strict__ flag may not be combined with other imports", 1
                ),
            ),
            (
                "import __strict__ as foo",
                StrictModuleBadStrictFlag("__strict__ import cannot be aliased", 1),
            ),
            (
                """
                def f():
                    import __strict__
                """,
                ModuleKind.Normal,
            ),
        ]
    )
    def test_strict_flag(
        self, code: str, expected: ModuleKind | StrictModuleBadStrictFlag
    ) -> None:
        if isinstance(expected, ModuleKind):
            self.assertEqual(self._get_kind(code), expected)
        else:
            with self.assertRaises(type(expected)) as ex:
                self._get_kind(code)
            self.assertEqual(ex.exception.lineno, expected.lineno)
            self.assertEqual(ex.exception.args, expected.args)
