from .common import ReadonlyTestBase


class FuncCredentialTests(ReadonlyTestBase):
    def test_simple_function(self) -> None:
        code = """
        def f():
            return __function_credential__
        """
        self._check_credential(code, "f", "global::f")

    def test_simple_class(self) -> None:

        code = """
        class C:
            def f(self):
                return __function_credential__
        def test():
            c = C()
            return c.f()
        """
        self._check_credential(code, "test", "global:C:f")

    def test_nested_functions(self) -> None:
        code = """
        def f():
            def g():
                def h():
                    return __function_credential__
                return h()
            return g()
        """
        self._check_credential(code, "f", "global::f.g.h")

    def test_nested_class_function(self) -> None:
        code = """
        def f():
            class C():
                def g(self):
                    def h():
                        return __function_credential__
                    return h()
            c = C()
            return c.g()
        """
        self._check_credential(code, "f", "global:f.C:g.h")

    def _check_credential(self, code: str, func: str, expected: str) -> None:
        f = self.compile_and_run(code)[func]
        cred = f()
        self.assertEqual(f"{cred}", f"<Function Credential {expected}>")
