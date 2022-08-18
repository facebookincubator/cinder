from unittest import skip

from .common import StaticTestBase


class DynamicReturnTests(StaticTestBase):
    def test_dynamic_return(self):
        codestr = """
            from __future__ import annotations
            from __static__ import allow_weakrefs, dynamic_return
            import weakref

            singletons = []

            @allow_weakrefs
            class C:
                @dynamic_return
                @staticmethod
                def make() -> C:
                    return weakref.proxy(singletons[0])

                def g(self) -> int:
                    return 1

            singletons.append(C())

            def f() -> int:
                c = C.make()
                return c.g()
        """
        with self.in_strict_module(codestr) as mod:
            # We don't try to cast the return type of make
            self.assertNotInBytecode(mod.C.make, "CAST")
            # We can statically invoke make
            self.assertInBytecode(
                mod.f, "INVOKE_FUNCTION", ((mod.__name__, "C", "make"), 0)
            )
            # But we can't statically invoke a method on the returned instance
            self.assertNotInBytecode(mod.f, "INVOKE_METHOD")
            self.assertEqual(mod.f(), 1)
            # We don't mess with __annotations__
            self.assertEqual(mod.C.make.__annotations__, {"return": "C"})

    def test_dynamic_return_known_type(self):
        codestr = """
            from __future__ import annotations
            from __static__ import allow_weakrefs, dynamic_return
            import weakref

            singletons = []

            @allow_weakrefs
            class C:
                @dynamic_return
                @staticmethod
                def make() -> C:
                    return 1

            singletons.append(C())

            def f() -> int:
                return C.make()
        """
        with self.in_strict_module(codestr) as mod:
            # We don't try to cast the return type of make
            self.assertNotInBytecode(mod.C.make, "CAST")
            # We can statically invoke make
            self.assertInBytecode(
                mod.f, "INVOKE_FUNCTION", ((mod.__name__, "C", "make"), 0)
            )
            # But we can't statically invoke a method on the returned instance
            self.assertNotInBytecode(mod.f, "INVOKE_METHOD")
            self.assertEqual(mod.f(), 1)
            # We don't mess with __annotations__
            self.assertEqual(mod.C.make.__annotations__, {"return": "C"})

    def test_dynamic_return_async_fn(self):
        codestr = """
        from __static__ import dynamic_return

        class C:
            @dynamic_return
            def fn(self) -> int:
                return 3

        def f() -> int:
            return C().fn()
        """
        with self.in_strict_module(codestr) as mod:
            f = mod.f
            self.assertInBytecode(f, "CAST", ("builtins", "int"))
            self.assertEqual(f(), 3)
