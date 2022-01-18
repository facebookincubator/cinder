from .common import StaticTestBase


class SlotsWithDefaultTests(StaticTestBase):
    def test_access_from_instance_and_class(self) -> None:
        codestr = """
        class C:
            x: int = 42

        def f():
            c = C()
            return (C.x, c.x)
        """
        with self.in_module(codestr) as mod:
            self.assertNotInBytecode(mod.f, "LOAD_FIELD")
            self.assertEqual(mod.f(), (42, 42))

    def test_nonstatic_access_from_instance_and_class(self) -> None:
        codestr = """
        class C:
            x: int = 42
        """
        with self.in_module(codestr) as mod:
            C = mod.C
            self.assertEqual(C.x, 42)
            self.assertEqual(C().x, 42)

    def test_write_from_instance(self) -> None:
        codestr = """
        class C:
            x: int = 42

        def f():
            c = C()
            c.x = 21
            return (C.x, c.x)
        """
        with self.in_module(codestr) as mod:
            self.assertNotInBytecode(mod.f, "LOAD_FIELD")
            self.assertEqual(mod.f(), (42, 21))

    def test_nonstatic_write_from_instance(self) -> None:
        codestr = """
        class C:
            x: int = 42
        """
        with self.in_module(codestr) as mod:
            C = mod.C
            c = C()
            c.x = 21
            self.assertEqual(C.x, 42)
            self.assertEqual(c.x, 21)

    def test_write_from_class(self) -> None:
        codestr = """
        class C:
            x: int = 42

        def f():
            c = C()
            C.x = 21
            return (C.x, c.x)
        """
        with self.in_module(codestr) as mod:
            self.assertNotInBytecode(mod.f, "LOAD_FIELD")
            self.assertEqual(mod.f(), (21, 21))

    def test_nonstatic_write_from_class(self) -> None:
        codestr = """
        class C:
            x: int = 42
        """
        with self.in_module(codestr) as mod:
            C = mod.C
            c = C()
            C.x = 21
            self.assertEqual(C.x, 21)
            self.assertEqual(c.x, 21)

    def test_write_to_class_after_instance(self) -> None:
        codestr = """
        class C:
            x: int = 42

        def f():
            c = C()
            c.x = 36 # This write will get clobbered when the class gets patched below.
            C.x = 21
            return (C.x, c.x)
        """
        with self.in_module(codestr) as mod:
            self.assertNotInBytecode(mod.f, "LOAD_FIELD")
            self.assertEqual(mod.f(), (21, 21))

    def test_inheritance(self) -> None:
        codestr = """
        class C:
            x: int = 42

        class D(C):
            pass

        def f():
            d = D()
            return (D.x, d.x)
        """
        with self.in_module(codestr) as mod:
            self.assertEqual(mod.f(), (42, 42))

    def test_inheritance_with_override(self) -> None:
        codestr = """
        class C:
            x: int = 1

        class D(C):
            x: int = 3

        def f():
            c = C()
            c.x = 2
            d = D()
            return (C.x, c.x, D.x, d.x)
        """
        with self.in_module(codestr) as mod:
            self.assertEqual(mod.f(), (1, 2, 3, 3))

    def test_call(self) -> None:
        codestr = """
        class C:
            x: int = 1

        class D(C):
            pass

        def f(c: C):
            return c.x
        """
        with self.in_module(codestr) as mod:
            d = mod.D()
            self.assertEqual(mod.f(d), 1)
            d.x = 2
            self.assertEqual(mod.f(d), 2)
