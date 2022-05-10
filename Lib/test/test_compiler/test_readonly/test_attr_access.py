import unittest

from .common import ReadonlyTestBase


class AttrAccessTests(ReadonlyTestBase):
    def test_readonly_flags0(self) -> None:
        code = """
        class Descr:
            @readonly_func
            def __get__(self, cls: Readonly[object], t):
                pass

        class NewClass:
            a = Descr()

        def f():
            return NewClass.__flags__
        """

        result = self._compile_and_run(code, "f")
        self.assertEqual(result & 0x3, 0)

    def test_readonly_flags1(self) -> None:
        code = """
        class Descr:
            @readonly_func
            def __get__(self, cls, t):
                pass

        class NewClass:
            a = Descr()

        def f():
            return NewClass.__flags__
        """

        result = self._compile_and_run(code, "f")
        self.assertEqual(result & 0x3, 1)

    def test_readonly_flags2(self) -> None:
        code = """
        class Descr:
            @readonly_func
            def __get__(self, cls: Readonly[object], t) -> Readonly[object]:
                pass

        class NewClass:
            a = Descr()

        def f():
            return NewClass.__flags__
        """

        result = self._compile_and_run(code, "f")
        self.assertEqual(result & 0x3, 2)

    def test_readonly_flags3(self) -> None:
        code = """
        class Descr:
            @readonly_func
            def __get__(self, cls, t) -> Readonly[object]:
                pass

        class NewClass:
            a = Descr()

        def f():
            return NewClass.__flags__
        """

        result = self._compile_and_run(code, "f")
        self.assertEqual(result & 0x3, 3)

    def test_readonly_flags_no_readonly(self) -> None:
        code = """
        class Descr:
            def __get__(self, cls, t):
                pass

        class NewClass:
            a = Descr()

        def f():
            return NewClass.__flags__
        """

        result = self._compile_and_run(code, "f")
        self.assertEqual(result & 0x3, 1)

    def test_readonly_flags_inheritance(self) -> None:
        code = """
        class Descr:
            @readonly_func
            def __get__(self, cls, t):
                pass

        class NewClass:
            a = Descr()

        class DerivedClass(NewClass):
            pass

        def f():
            return DerivedClass.__flags__
        """

        result = self._compile_and_run(code, "f")
        self.assertEqual(result & 0x3, 1)

    def test_readonly_flags_multiple_inheritance(self) -> None:
        code = """
        class Descr1:
            @readonly_func
            def __get__(self, cls, t):
                pass

        class NewClass1:
            a = Descr1()

        class Descr2:
            @readonly_func
            def __get__(self, cls: Readonly[object], t) -> Readonly[object]:
                pass

        class NewClass2:
            a = Descr2()

        class DerivedClass(NewClass1, NewClass2):
            pass

        def f():
            return DerivedClass.__flags__
        """

        result = self._compile_and_run(code, "f")
        self.assertEqual(result & 0x3, 3)

    def _compile_and_run(self, code: str, func: str) -> None:
        f = self.compile_and_run(code)[func]
        return f()
