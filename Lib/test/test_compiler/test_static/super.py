from compiler.pycodegen import CinderCodeGenerator

from .common import StaticTestBase


class SuperTests(StaticTestBase):
    def test_dynamic_base_class(self):
        nonstatic = """
            class A:
                x = 1
        """
        with self.in_module(nonstatic, code_gen=CinderCodeGenerator) as nonstatic_mod:
            codestr = f"""
                from {nonstatic_mod.__name__} import A

                class B(A):
                    x = 2

                    def foo(self):
                        return super().x
            """
            with self.in_strict_module(codestr) as mod:
                self.assertInBytecode(mod.B.foo, "LOAD_ATTR_SUPER")
                self.assertEqual(mod.B().foo(), 1)

    def test_method_in_parent_class(self):
        codestr = """
        class A:
            def f(self):
                return 4

        class B(A):
            def g(self):
                return super().f()

        def foo():
            return B().g()
        """
        with self.in_strict_module(codestr) as mod:
            self.assertInBytecode(
                mod.B.g, "INVOKE_FUNCTION", ((mod.__name__, "A", "f"), 1)
            )
            self.assertEqual(mod.foo(), 4)

    def test_method_in_parents_parent_class(self):
        codestr = """
        class AA:
            def f(self):
                return 4

        class A(AA):
            def g(self):
                return 8

        class B(A):
            def g(self):
                return super().f()

        def foo():
            return B().g()
        """
        with self.in_strict_module(codestr) as mod:
            self.assertInBytecode(
                mod.B.g, "INVOKE_FUNCTION", ((mod.__name__, "AA", "f"), 1)
            )
            self.assertEqual(mod.foo(), 4)

    def test_super_call_with_parameters(self):
        codestr = """
        class A:
            def f(self):
                return 4

        class B(A):
            def f(self):
                return 5

        class C(B):
            def g(self):
                return super(B, self).f()

        def foo():
            return C().g()
        """
        with self.in_strict_module(codestr) as mod:
            self.assertNotInBytecode(mod.C.g, "INVOKE_FUNCTION")
            self.assertEqual(mod.foo(), 4)

    def test_unsupported_property_in_parent_class(self):
        codestr = """
        class A:
            @property
            def f(self):
                return 4

        class B(A):
            def g(self):
                return super().f

        def foo():
            return B().g()
        """
        with self.in_strict_module(codestr) as mod:
            self.assertNotInBytecode(mod.B.g, "INVOKE_FUNCTION")
            self.assertEqual(mod.foo(), 4)

    def test_unsupported_property_in_parents_parent_class(self):
        codestr = """
        class AA:
            @property
            def f(self):
                return 4

        class A(AA):
            pass

        class B(A):
            def g(self):
                return super().f

        def foo():
            return B().g()
        """
        with self.in_strict_module(codestr) as mod:
            self.assertNotInBytecode(mod.B.g, "INVOKE_FUNCTION")
            self.assertEqual(mod.foo(), 4)

    def test_unsupported_attr_in_parent_class(self):
        codestr = """
        class A:
            f = 4

        class B(A):
            def g(self):
                return super().f

        def foo():
            return B().g()
        """
        with self.in_strict_module(codestr) as mod:
            self.assertNotInBytecode(mod.B.g, "INVOKE_FUNCTION")
            self.assertEqual(mod.foo(), 4)

    def test_unsupported_attr_in_parent_class(self):
        codestr = """
        class A:
            f = 4

        class B(A):
            def g(self):
                return super().f

        def foo():
            return B().g()
        """
        with self.in_strict_module(codestr) as mod:
            self.assertNotInBytecode(mod.B.g, "INVOKE_FUNCTION")
            self.assertEqual(mod.foo(), 4)

    def test_unsupported_attr_in_parents_parent_class(self):
        codestr = """
        class AA:
            f = 4

        class A(AA):
            pass

        class B(A):
            def g(self):
                return super().f

        def foo():
            return B().g()
        """
        with self.in_strict_module(codestr) as mod:
            self.assertNotInBytecode(mod.B.g, "INVOKE_FUNCTION")
            self.assertEqual(mod.foo(), 4)

    def test_unsupported_class_nested_in_function(self):
        codestr = """
        def foo():

            class B:
                def g(self):
                    return super().f()

            return B().g()
        """
        with self.in_strict_module(codestr) as mod:
            self.assertNotInBytecode(mod.foo, "INVOKE_FUNCTION")

    def test_unsupported_class_nested_in_class(self):
        codestr = """
        class A:
            class B:
                def g(self):
                    return super().f()
        """
        with self.in_strict_module(codestr) as mod:
            self.assertNotInBytecode(mod.A.B.g, "INVOKE_FUNCTION")

    def test_unsupported_class_nested_in_funcdef(self):
        codestr = """
        class A:
            def g(self):
                def f():
                    return super().bar()
                return f()
        """
        with self.in_strict_module(codestr) as mod:
            self.assertNotInBytecode(mod.A.g, "INVOKE_FUNCTION")

    def test_unsupported_case_falls_back_to_dynamic(self):
        codestr = """
        class A(Exception):
            pass

        class B(A):
            def g(self):
                return super().__init__()
        """
        with self.in_strict_module(codestr) as mod:
            self.assertNotInBytecode(mod.B.g, "INVOKE_FUNCTION")
