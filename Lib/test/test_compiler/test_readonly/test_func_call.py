import unittest

from .common import ReadonlyTestBase


class FuncCallTests(ReadonlyTestBase):
    def test_call_nro_ro(self) -> None:
        code = """
        @readonly_func
        def g():
            return 1

        def f():
            return g()
        """
        with self.assertNoImmutableErrors():
            self._compile_and_run(code, "f")

    def test_call_ro_nro(self) -> None:
        code = """
        def g():
            return 1

        @readonly_func
        def f():
            return g()
        """
        with self.assertImmutableErrors(
            [
                (1, "A mutable function cannot be called in a readonly function.", ()),
                (3, "Cannot assign a readonly value to a mutable variable.", ()),
            ]
        ):
            self._compile_and_run(code, "f")

    def test_call_ro_ro(self) -> None:
        code = """
        @readonly_func
        def g():
            return 1

        @readonly_func
        def f():
            return g()
        """
        with self.assertNoImmutableErrors():
            self._compile_and_run(code, "f")

    def test_returns_readonly_err(self) -> None:
        code = """
        @readonly_func
        def g() -> Readonly[int]:
            return 1

        @readonly_func
        def f():
            t = g()
            return t
        """
        with self.assertImmutableErrors(
            [(3, "Cannot assign a readonly value to a mutable variable.", ())]
        ):
            self._compile_and_run(code, "f")

    def test_no_returns_readonly_error(self) -> None:
        code = """
        @readonly_func
        def g() -> Readonly[int]:
            return 1

        @readonly_func
        def f() -> Readonly[int]:
            t: Readonly[int] = g()
            return t
        """
        with self.assertNoImmutableErrors():
            self._compile_and_run(code, "f")

    def test_arguments(self) -> None:
        code = """
        @readonly_func
        def g(a, b: Readonly[int], c):
            return 1

        @readonly_func
        def f():
            c: Readonly[int] = 3
            t = g(1, 2, c)
            return t
        """
        with self.assertImmutableErrors(
            [(4, "Passing a readonly variable to Argument 2, which is mutable.", (2,))]
        ):
            self._compile_and_run(code, "f")

    def test_readonly_closure_no_error(self) -> None:
        code = """
        def f():
            @readonly_func
            @readonly_closure
            def h():
                pass

            @readonly_func
            @readonly_closure
            def g():
                h()

            g()
        """
        with self.assertNoImmutableErrors():
            self._compile_and_run(code, "f")

    def test_readonly_closure_error(self) -> None:
        code = """
        def f():
            @readonly_func
            def i():
                pass

            @readonly_func
            @readonly_closure
            def g():
                i()

            g()
        """
        with self.assertImmutableErrors(
            [
                (
                    2,
                    "A function decorated with @readonly_closure cannot call another function without the @readonly_closure decoration.",
                    (),
                )
            ]
        ):
            self._compile_and_run(code, "f")

    @unittest.skipUnderCinderJIT("Not implemented yet.")
    def test_method_call_nro_ro(self) -> None:
        code = """
        class C:
            @readonly_func
            def g(self):
                return 1

        def f():
            return C().g()
        """
        with self.assertNoImmutableErrors():
            self._compile_and_run(code, "f")

    @unittest.skipUnderCinderJIT("Not implemented yet.")
    def test_method_call_ro_nro(self) -> None:
        code = """
        class C:
            def g(self):
                return 1

        @readonly_func
        def f():
            return C().g()
        """
        with self.assertImmutableErrors(
            [
                (1, "A mutable function cannot be called in a readonly function.", ()),
                (3, "Cannot assign a readonly value to a mutable variable.", ()),
            ]
        ):
            self._compile_and_run(code, "f")

    @unittest.skipUnderCinderJIT("Not implemented yet.")
    def test_method_call_ro_ro(self) -> None:
        code = """
        class C:
            @readonly_func
            def g(self):
                return 1

        @readonly_func
        def f():
            return C().g()
        """
        with self.assertNoImmutableErrors():
            self._compile_and_run(code, "f")

    @unittest.skipUnderCinderJIT("Not implemented yet.")
    def test_method_call_arguments(self) -> None:
        code = """
        class C:
            @readonly_func
            def g(self: Readonly[C], a, b: Readonly[int], c):
                return 1

        @readonly_func
        def f():
            c: Readonly[int] = 3
            t = C().g(1, 2, c)
            return t
        """
        with self.assertImmutableErrors(
            [(4, "Passing a readonly variable to Argument 3, which is mutable.", (3,))]
        ):
            self._compile_and_run(code, "f")

    @unittest.skipUnderCinderJIT("Not implemented yet.")
    def test_method_check_self(self) -> None:
        code = """
        class C:
            @readonly_func
            def g(self, a, b: Readonly[int], c):
                return 1

        @readonly_func
        def f():
            c: Readonly[C] = C()
            t = c.g(1, 2, 3)
            return t
        """
        with self.assertImmutableErrors(
            [(4, "Passing a readonly variable to Argument 0, which is mutable.", (0,))]
        ):
            self._compile_and_run(code, "f")

    def test_call_in_subscr(self) -> None:
        code = """
        @readonly_func
        def f():
            return 0

        @readonly_func
        def g():
            l = [0]
            l[f()] = 1
            return l
        """
        with self.assertNoImmutableErrors():
            self._compile_and_run(code, "g")

    def test_disallowed_call_in_subscr(self) -> None:
        code = """
        @readonly_func
        def f(x: int) -> int:
            return 0

        @readonly_func
        def g():
            l = [0]
            l[f(readonly(2))] = 1
            return l
        """
        with self.assertImmutableErrors(
            [(4, "Passing a readonly variable to Argument 0, which is mutable.", (0,))]
        ):
            self._compile_and_run(code, "g")

    def _compile_and_run(self, code: str, func: str) -> None:
        f = self.compile_and_run(code)[func]
        return f()
