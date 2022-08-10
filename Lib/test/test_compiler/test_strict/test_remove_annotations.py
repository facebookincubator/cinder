from __future__ import annotations

import ast
from compiler.strict.rewriter import AnnotationRemover, remove_annotations
from textwrap import dedent
from typing import cast, final

from .common import StrictTestBase


@final
class AnnotationRemoverTests(StrictTestBase):
    def test_annotation_remover(self) -> None:
        code = """
        import __static__
        from typing import Optional, Tuple, Union

        def f(
            a: int, /, b: Optional[str], c: object, d: Union[int, float] = 4.0, *, e: Tuple = (1, "2", 3.0)
        ) -> int:
            ...
        """
        tree = ast.parse(dedent(code), "testmodule.py", "exec")
        transformed = AnnotationRemover().visit(tree)
        function = transformed.body[2]

        for arg in function.args.posonlyargs:
            self.assertEqual(arg.annotation, None)

        for arg in function.args.args:
            self.assertEqual(arg.annotation, None)

        for arg in function.args.kwonlyargs:
            self.assertEqual(arg.annotation, None)

    def test_annotation_remover_attributes(self) -> None:
        code = """
        import __static__
        from typing import Tuple

        class A:
            x: int
            y: str = "hi"
        """
        tree = ast.parse(dedent(code), "testmodule.py", "exec")
        transformed = AnnotationRemover().visit(tree)
        klass = transformed.body[2]

        x_assign = klass.body[0]
        y_assign = klass.body[1]

        self.assertIsInstance(x_assign, ast.Assign)
        self.assertEqual(ast.dump(x_assign.value), "Constant(value=Ellipsis)")

        self.assertIsInstance(y_assign, ast.Assign)
        self.assertEqual(ast.dump(y_assign.value), "Constant(value='hi')")

    def test_annotation_remover_methods(self) -> None:
        code = """
        import __static__
        from typing import Optional, Tuple, Union

        class A:
            def f(
                a: int, /, b: Optional[str], c: object, d: Union[int, float] = 4.0, *, e: Tuple = (1, "2", 3.0)
            ) -> int:
                ...
        """
        tree = ast.parse(dedent(code), "testmodule.py", "exec")
        transformed = AnnotationRemover().visit(tree)
        klass = transformed.body[2]
        method = klass.body[0]

        for arg in method.args.posonlyargs:
            self.assertEqual(arg.annotation, None)

        for arg in method.args.args:
            self.assertEqual(arg.annotation, None)

        for arg in method.args.kwonlyargs:
            self.assertEqual(arg.annotation, None)

    def test_annotation_remover_linenos_exist(self) -> None:
        code = """
        x: Tuple
        """
        tree = ast.parse(dedent(code), "testmodule.py", "exec")

        transformed_with_linenos = remove_annotations(tree)

        assign = transformed_with_linenos.body[0]
        self.assertEqual(assign.lineno, 2)
        self.assertIsInstance(assign, ast.Assign)
        # pyre does not refine types based on assertIsInstance() yet
        assign = cast(ast.Assign, assign)
        self.assertIsInstance(assign.value, ast.Constant)
        self.assertEqual(assign.value.lineno, 2)
