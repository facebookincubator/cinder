from .common import ReadonlyTestBase


class TypeBinderTest(ReadonlyTestBase):
    def test_readonly_redeclare_0(self) -> None:
        code = """
        def f():
            x = 1
            x: Readonly[int] = 2
        """
        errors = self.lint(code)
        errors.check(
            errors.match("cannot re-declare the readonliness of 'x'"),
        )

    def test_readonly_redeclare_1(self) -> None:
        code = """
        def f():
            x: Readonly[int] = 1
            x: int = 2
        """
        errors = self.lint(code)
        errors.check(
            errors.match("cannot re-declare the readonliness of 'x'"),
        )

    def test_readonly_wrapper(self) -> None:
        # Might be confusing. The number 2 is narrowed to
        # readonly because `x` is declared readonly
        code = """
        def f():
            x = readonly([])
            x = 2
        """
        errors = self.lint(code)
        self.assertEqual(errors.errors, [])

    def test_readonly_wrapper_redeclare(self) -> None:
        # x is redeclared
        code = """
        def f():
            x = readonly([])
            x: int = 2
        """
        errors = self.lint(code)
        errors.check(
            errors.match("cannot re-declare the readonliness of 'x'"),
        )

    def test_readonly_assign(self) -> None:
        code = """
        def f():
            x: Readonly[int] = 1
            x = 2
        """
        errors = self.lint(code)
        self.assertEqual(errors.errors, [])

    def test_readonly_augassign(self) -> None:
        code = """
        def f():
            l: Readonly[List[int]] = []
            l += [1]
        """
        errors = self.lint(code)
        errors.check(
            errors.match(
                "Cannot modify readonly reference 'l' via aug assign",
            ),
        )

    def test_call_readonly(self) -> None:
        code = """
        def f():
            x: Readonly[int] = 1
            f(x, 1 ,2)
        """
        errors = self.lint(code)
        self.assertEqual(errors.errors, [])

    def test_call_readonly_kw(self) -> None:
        code = """
        def f():
            x: Readonly[int] = 1
            f(x, 1 ,2, y=1)
        """
        errors = self.lint(code)
        errors.check(
            errors.match(
                "Unsupported: cannot use keyword args or"
                " star args when ANY argument is readonly"
            ),
        )

    def test_call_readonly_star_args(self) -> None:
        code = """
        def f():
            x: Readonly[List[int]] = [1, 2]
            f(1 ,2, *x)
        """
        errors = self.lint(code)
        errors.check(
            errors.match(
                "Unsupported: cannot use keyword args or"
                " star args when ANY argument is readonly"
            ),
        )

    def test_readonly_class_var(self) -> None:
        code = """
        class C:
            x: Readonly[int] = 1
        """
        errors = self.lint(code)
        errors.check(
            errors.match("cannot declare 'x' readonly in class/module"),
        )

    def test_readonly_base_class(self) -> None:
        code = """
        def f():
            C: Readonly[object]
            class D(C):
                ...
        """
        errors = self.lint(code)
        errors.check(
            errors.match("cannot inherit from a readonly base class 'C'"),
        )

    def test_readonly_func_global(self) -> None:
        code = """
        x = 1
        @readonly_func
        def f():
            x += 1
        """
        errors = self.lint(code)
        self.assertEqual(errors.errors, [])

    def test_readonly_func_closure(self) -> None:
        code = """
        def g():
            x = 1
            @readonly_func
            def f():
                nonlocal x
                x += 1
        """
        errors = self.lint(code)
        errors.check(
            errors.match(
                "Cannot modify readonly reference 'x' via aug assign",
            ),
            errors.match(
                "cannot modify 'x' from a closure, inside a readonly_func annotated function"
            ),
        )

    def test_readonly_func_closure_1(self) -> None:
        code = """
        def g():
            x = 1
            @readonly_func
            def f():
                nonlocal x
                x = 2
        """
        errors = self.lint(code)
        errors.check(
            errors.match(
                "cannot modify 'x' from a closure, inside a readonly_func annotated function"
            ),
        )

    def test_readonly_in_method(self) -> None:
        code = """
        class C:
            def f(self, x: Readonly[List[int]]):
                x += [1]
        """
        errors = self.lint(code)
        errors.check(
            errors.match(
                "Cannot modify readonly reference 'x' via aug assign",
            ),
        )

    def test_readonly_nested_func_late_declare(self) -> None:
        code = """
        def g():
            def f():
                y: int = x
            x = readonly(1)
        """
        errors = self.lint(code)
        errors.check(
            errors.match("cannot assign readonly value to mutable 'y'"),
        )

    def test_readonly_nested_class_late_declare(self) -> None:
        code = """
        def g():
            class C:
                def f():
                    y: int = x
            x = readonly(1)
        """
        errors = self.lint(code)
        errors.check(
            errors.match("cannot assign readonly value to mutable 'y'"),
        )

    def test_readonly_triple_nested_function_late_declare(self) -> None:
        code = """
        def g():
            def h():
                def f():
                    y: int = x
            x = readonly(1)
        """
        errors = self.lint(code)
        errors.check(
            errors.match("cannot assign readonly value to mutable 'y'"),
        )

    def test_readonly_for_readonly_mutable(self) -> None:
        code = """
        def g(arr: List[int]):
            y = 0
            for a in readonly(arr):
                y = a
        """
        errors = self.lint(code)
        errors.check(
            errors.match("cannot assign readonly value to mutable 'y'"),
        )

    def test_readonly_for_readonly(self) -> None:
        code = """
        def g(arr: List[int]):
            y = readonly(0)
            for a in readonly(arr):
                y = a
        """
        errors = self.lint(code)
        self.assertEqual(errors.errors, [])

    def test_readonly_async_for_readonly_mutable(self) -> None:
        code = """
        def g(arr):
            y = 0
            async for a in readonly(arr):
                y = a
        """
        errors = self.lint(code)
        errors.check(
            errors.match("cannot assign readonly value to mutable 'y'"),
        )

    def test_readonly_for_readonly(self) -> None:
        code = """
        def g(arr):
            y = readonly(0)
            async for a in readonly(arr):
                y = a
        """
        errors = self.lint(code)
        self.assertEqual(errors.errors, [])

    def test_readonly_delete(self) -> None:
        code = """
        def g(arr):
            x = 0
            y = readonly(0)
            del x
            del y
        """
        errors = self.lint(code)
        errors.check(
            errors.match("Cannot explicitly delete readonly value 'y'"),
        )

    def test_readonly_raise(self) -> None:
        code = """
        def g(arr):
            y = readonly(0)
            raise y
        """
        errors = self.lint(code)
        errors.check(
            errors.match("Cannot raise readonly expression 'y'"),
        )

    def test_readonly_raise_cause(self) -> None:
        code = """
        def g(arr):
            x = 0
            y = readonly(0)
            raise x from y
        """
        errors = self.lint(code)
        errors.check(
            errors.match("Cannot raise with readonly cause 'y'"),
        )

    def test_readonly_return(self) -> None:
        code = """
        def g(arr) -> int:
            y = readonly(0)
            return y
        """
        errors = self.lint(code)
        errors.check(
            errors.match(
                "Cannot return readonly expression 'y' from a function returning a mutable type"
            ),
        )

    def test_readonly_return_good(self) -> None:
        code = """
        def g(arr) -> Readonly[int]:
            y = readonly(0)
            return y
        """
        errors = self.lint(code)
        self.assertEqual(errors.errors, [])
