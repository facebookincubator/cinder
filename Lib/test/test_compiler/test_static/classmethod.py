from .common import StaticTestBase


class ClassMethodTests(StaticTestBase):
    def test_classmethod_from_class_calls_invoke_function(self):
        codestr = """
            class C:
                 @classmethod
                 def foo(cls):
                     return cls
            def f():
                return C.foo()
        """
        with self.in_module(codestr) as mod:
            f = mod["f"]
            C = mod["C"]
            self.assertInBytecode(f, "INVOKE_FUNCTION")
            self.assertEqual(f(), C)

    def test_classmethod_from_instance_calls_invoke_method(self):
        codestr = """
            class C:
                 @classmethod
                 def foo(cls):
                     return cls
            def f(c: C):
                return c.foo()
        """
        with self.in_module(codestr) as mod:
            f = mod["f"]
            C = mod["C"]
            c = C()
            self.assertInBytecode(f, "INVOKE_METHOD")
            self.assertEqual(f(c), C)

    def test_classmethod_override_from_instance_calls_override(self):
        codestr = """
            class C:
                 @classmethod
                 def foo(cls, x: int) -> int:
                     return x
            class D(C):
                 @classmethod
                 def foo(cls, x: int) -> int:
                     return x + 2

            def f(c: C):
                return c.foo(0)
        """
        with self.in_module(codestr) as mod:
            f = mod["f"]
            D = mod["D"]
            d = D()
            self.assertInBytecode(f, "INVOKE_METHOD")
            self.assertEqual(f(d), 2)

    def test_classmethod_override_from_non_static_instance_calls_override(self):
        codestr = """
            class C:
                 @classmethod
                 def foo(cls, x: int) -> int:
                     return x

            def f(c: C) -> int:
                return c.foo(0)
        """
        with self.in_module(codestr) as mod:
            f = mod["f"]
            C = mod["C"]

            class D(C):
                @classmethod
                def foo(cls, x: int) -> int:
                    return x + 30

            d = D()
            self.assertInBytecode(f, "INVOKE_METHOD")
            self.assertEqual(f(d), 30)

    def test_classmethod_non_class_method_override(self):
        codestr = """
            class C:
                 @classmethod
                 def foo(cls, x: int) -> int:
                     return x
            class D(C):
                 def foo(cls, x: int) -> int:
                     return x + 2

            def f(c: C):
                return c.foo(0)
        """
        self.type_error(codestr, "class cannot hide inherited member")
