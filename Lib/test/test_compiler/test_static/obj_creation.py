from compiler.static.errors import TypedSyntaxError
from re import escape

from .common import StaticTestBase


class StaticObjCreationTests(StaticTestBase):
    def test_init(self):
        codestr = """
            class C:

                def __init__(self, a: int) -> None:
                    self.value = a

            def f(x: int) -> C:
                return C(x)
        """
        with self.in_module(codestr) as mod:
            f = mod["f"]
            self.assertEqual(f(42).value, 42)

    def test_init_unknown_base(self):
        codestr = """
            from re import Scanner
            class C(Scanner):
                pass

            def f(x: int) -> C:
                return C(x)
        """
        with self.in_module(codestr) as mod:
            f = mod["f"]
            # Unknown base class w/ no overrides should always be CALL_FUNCTION
            self.assertInBytecode(f, "CALL_FUNCTION")

    def test_init_wrong_type(self):
        codestr = """
            class C:

                def __init__(self, a: int) -> None:
                    self.value = a

            def f(x: str) -> C:
                return C(x)
        """
        with self.assertRaisesRegex(
            TypedSyntaxError,
            "type mismatch: str received for positional arg 'a', expected int",
        ):
            self.compile(codestr)

    def test_init_extra_arg(self):
        codestr = """
            class C:

                def __init__(self, a: int) -> None:
                    self.value = a

            def f(x: int) -> C:
                return C(x, 42)
        """
        with self.assertRaisesRegex(
            TypedSyntaxError,
            escape(
                "Mismatched number of args for function <module>.C.__init__. Expected 2, got 3"
            ),
        ):
            self.compile(codestr)

    def test_new(self):
        codestr = """
            class C:
                value: int
                def __new__(cls, a: int) -> "C":
                    res = object.__new__(cls)
                    res.value = a
                    return res

            def f(x: int) -> C:
                return C(x)
        """
        with self.in_module(codestr) as mod:
            f = mod["f"]
            self.assertEqual(f(42).value, 42)

    def test_new_wrong_type(self):
        codestr = """
            class C:
                value: int
                def __new__(cls, a: int) -> "C":
                    res = object.__new__(cls)
                    res.value = a
                    return res

            def f(x: str) -> C:
                return C(x)
        """
        with self.assertRaisesRegex(
            TypedSyntaxError,
            "type mismatch: str received for positional arg 'a', expected int",
        ):
            self.compile(codestr)

    def test_new_object(self):
        codestr = """
            class C:
                value: int
                def __new__(cls, a: int) -> object:
                    res = object.__new__(cls)
                    res.value = a
                    return res
                def __init__(self, a: int):
                    self.value = 100

            def f(x: int) -> object:
                return C(x)
        """
        with self.in_module(codestr) as mod:
            f = mod["f"]
            self.assertEqual(f(42).value, 100)

    def test_new_dynamic(self):
        codestr = """
            class C:
                value: int
                def __new__(cls, a: int):
                    res = object.__new__(cls)
                    res.value = a
                    return res
                def __init__(self, a: int):
                    self.value = 100

            def f(x: int) -> object:
                return C(x)
        """
        with self.in_module(codestr) as mod:
            f = mod["f"]
            self.assertEqual(f(42).value, 100)

    def test_new_odd_ret_type(self):
        codestr = """
            class C:
                value: int
                def __new__(cls, a: int) -> int:
                    return 42

            def f(x: int) -> int:
                return C(x)
        """
        with self.in_module(codestr) as mod:
            f = mod["f"]
            self.assertEqual(f(42), 42)

    def test_new_odd_ret_type_no_init(self):
        codestr = """
            class C:
                value: int
                def __new__(cls, a: int) -> int:
                    return 42
                def __init__(self, *args) -> None:
                    raise Exception("no way")

            def f(x: int) -> int:
                return C(x)
        """
        with self.in_module(codestr) as mod:
            f = mod["f"]
            self.assertEqual(f(42), 42)

    def test_new_odd_ret_type_error(self):
        codestr = """
            class C:
                value: int
                def __new__(cls, a: int) -> int:
                    return 42

            def f(x: int) -> str:
                return C(x)
        """
        with self.assertRaisesRegex(
            TypedSyntaxError, "return type must be str, not int"
        ):
            self.compile(codestr)
