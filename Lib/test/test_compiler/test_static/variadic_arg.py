from compiler.errors import TypedSyntaxError
from unittest import skip, skipIf

from .common import StaticTestBase

try:
    import cinderjit
except ImportError:
    cinderjit = None


class VariadicArgTests(StaticTestBase):
    def test_load_iterable_arg(self):
        codestr = """
        def x(a: int, b: int, c: str, d: float, e: float) -> int:
            return 7

        def y() -> int:
            p = ("hi", 0.1, 0.2)
            return x(1, 3, *p)
        """
        y = self.find_code(self.compile(codestr, modname="foo"), name="y")
        self.assertInBytecode(y, "LOAD_ITERABLE_ARG", 0)
        self.assertInBytecode(y, "LOAD_ITERABLE_ARG", 1)
        self.assertInBytecode(y, "LOAD_ITERABLE_ARG", 2)
        self.assertNotInBytecode(y, "LOAD_ITERABLE_ARG", 3)
        with self.in_module(codestr) as mod:
            y_callable = mod.y
            self.assertEqual(y_callable(), 7)

    def test_load_iterable_arg_default_overridden(self):
        codestr = """
            def x(a: int, b: int, c: str, d: float = 10.1, e: float = 20.1) -> bool:
                return bool(
                    a == 1
                    and b == 3
                    and c == "hi"
                    and d == 0.1
                    and e == 0.2
                )

            def y() -> bool:
                p = ("hi", 0.1, 0.2)
                return x(1, 3, *p)
        """
        y = self.find_code(self.compile(codestr, modname="foo"), name="y")
        self.assertNotInBytecode(y, "LOAD_ITERABLE_ARG", 3)
        self.assertNotInBytecode(y, "LOAD_MAPPING_ARG", 3)
        with self.in_module(codestr) as mod:
            y_callable = mod.y
            self.assertTrue(y_callable())

    def test_load_iterable_arg_multi_star(self):
        codestr = """
        def x(a: int, b: int, c: str, d: float, e: float) -> int:
            return 7

        def y() -> int:
            p = (1, 3)
            q = ("hi", 0.1, 0.2)
            return x(*p, *q)
        """
        y = self.find_code(self.compile(codestr, modname="foo"), name="y")
        # we should fallback to the normal Python compiler for this
        self.assertNotInBytecode(y, "LOAD_ITERABLE_ARG")
        with self.in_module(codestr) as mod:
            y_callable = mod.y
            self.assertEqual(y_callable(), 7)

    def test_load_iterable_arg_star_not_last(self):
        codestr = """
        def x(a: int, b: int, c: str, d: float, e: float) -> int:
            return 7

        def y() -> int:
            p = (1, 3, 'abc', 0.1)
            return x(*p, 1.0)
        """
        y = self.find_code(self.compile(codestr, modname="foo"), name="y")
        # we should fallback to the normal Python compiler for this
        self.assertNotInBytecode(y, "LOAD_ITERABLE_ARG")
        with self.in_module(codestr) as mod:
            y_callable = mod.y
            self.assertEqual(y_callable(), 7)

    def test_load_iterable_arg_failure(self):
        codestr = """
        def x(a: int, b: int, c: str, d: float, e: float) -> int:
            return 7

        def y() -> int:
            p = ("hi", 0.1)
            return x(1, 3, *p)
        """
        y = self.find_code(self.compile(codestr, modname="foo"), name="y")
        self.assertInBytecode(y, "LOAD_ITERABLE_ARG", 0)
        self.assertInBytecode(y, "LOAD_ITERABLE_ARG", 1)
        self.assertInBytecode(y, "LOAD_ITERABLE_ARG", 2)
        self.assertNotInBytecode(y, "LOAD_ITERABLE_ARG", 3)
        with self.in_module(codestr) as mod:
            y_callable = mod.y
            with self.assertRaises(IndexError):
                y_callable()

    def test_load_iterable_arg_sequence(self):
        codestr = """
        def x(a: int, b: int, c: str, d: float, e: float) -> int:
            return 7

        def y() -> int:
            p = ["hi", 0.1, 0.2]
            return x(1, 3, *p)
        """
        y = self.find_code(self.compile(codestr, modname="foo"), name="y")
        self.assertInBytecode(y, "LOAD_ITERABLE_ARG", 0)
        self.assertInBytecode(y, "LOAD_ITERABLE_ARG", 1)
        self.assertInBytecode(y, "LOAD_ITERABLE_ARG", 2)
        self.assertNotInBytecode(y, "LOAD_ITERABLE_ARG", 3)
        with self.in_module(codestr) as mod:
            y_callable = mod.y
            self.assertEqual(y_callable(), 7)

    def test_load_iterable_arg_sequence_1(self):
        codestr = """
        def x(a: int, b: int, c: str, d: float, e: float) -> int:
            return 7

        def gen():
            for i in ["hi", 0.05, 0.2]:
                yield i

        def y() -> int:
            g = gen()
            return x(1, 3, *g)
        """
        y = self.find_code(self.compile(codestr, modname="foo"), name="y")
        self.assertInBytecode(y, "LOAD_ITERABLE_ARG", 0)
        self.assertInBytecode(y, "LOAD_ITERABLE_ARG", 1)
        self.assertInBytecode(y, "LOAD_ITERABLE_ARG", 2)
        self.assertNotInBytecode(y, "LOAD_ITERABLE_ARG", 3)
        with self.in_module(codestr) as mod:
            y_callable = mod.y
            self.assertEqual(y_callable(), 7)

    def test_load_iterable_arg_sequence_failure(self):
        codestr = """
        def x(a: int, b: int, c: str, d: float, e: float) -> int:
            return 7

        def y() -> int:
            p = ["hi", 0.1]
            return x(1, 3, *p)
        """
        y = self.find_code(self.compile(codestr, modname="foo"), name="y")
        self.assertInBytecode(y, "LOAD_ITERABLE_ARG", 0)
        self.assertInBytecode(y, "LOAD_ITERABLE_ARG", 1)
        self.assertInBytecode(y, "LOAD_ITERABLE_ARG", 2)
        self.assertNotInBytecode(y, "LOAD_ITERABLE_ARG", 3)
        with self.in_module(codestr) as mod:
            y_callable = mod.y
            with self.assertRaises(IndexError):
                y_callable()

    def test_load_mapping_arg(self):
        codestr = """
        def x(a: int, b: int, c: str, d: float=-0.1, e: float=1.1, f: str="something") -> bool:
            return bool(f == "yo" and d == 1.0 and e == 1.1)

        def y() -> bool:
            d = {"d": 1.0}
            return x(1, 3, "hi", f="yo", **d)
        """
        y = self.find_code(self.compile(codestr, modname="foo"), name="y")
        self.assertInBytecode(y, "LOAD_MAPPING_ARG", 3)
        with self.in_module(codestr) as mod:
            y_callable = mod.y
            self.assertTrue(y_callable())

    def test_load_mapping_and_iterable_args_failure_1(self):
        """
        Fails because we don't supply enough positional args
        """

        codestr = """
        def x(a: int, b: int, c: str, d: float=2.2, e: float=1.1, f: str="something") -> bool:
            return bool(a == 1 and b == 3 and f == "yo" and d == 2.2 and e == 1.1)

        def y() -> bool:
            return x(1, 3, f="yo")
        """
        with self.assertRaisesRegex(
            SyntaxError, "Function foo.x expects a value for argument c"
        ):
            self.compile(codestr, modname="foo")

    def test_load_mapping_arg_failure(self):
        """
        Fails because we supply an extra kwarg
        """
        codestr = """
        def x(a: int, b: int, c: str, d: float=2.2, e: float=1.1, f: str="something") -> bool:
            return bool(a == 1 and b == 3 and f == "yo" and d == 2.2 and e == 1.1)

        def y() -> bool:
            return x(1, 3, "hi", f="yo", g="lol")
        """
        with self.assertRaisesRegex(
            TypedSyntaxError,
            "Given argument g does not exist in the definition of foo.x",
        ):
            self.compile(codestr, modname="foo")

    def test_load_mapping_arg_custom_class(self):
        """
        Fails because we supply a custom class for the mapped args, instead of a dict
        """
        codestr = """
        def x(a: int, b: int, c: str="hello") -> bool:
            return bool(a == 1 and b == 3 and c == "hello")

        class C:
            def __getitem__(self, key: str) -> str | None:
                if key == "c":
                    return "hi"

            def keys(self):
                return ["c"]

        def y() -> bool:
            return x(1, 3, **C())
        """
        with self.in_module(codestr) as mod:
            y_callable = mod.y
            with self.assertRaisesRegex(
                TypeError, r"argument after \*\* must be a dict, not C"
            ):
                self.assertTrue(y_callable())

    def test_load_mapping_arg_use_defaults(self):
        codestr = """
        def x(a: int, b: int, c: str, d: float=-0.1, e: float=1.1, f: str="something") -> bool:
            return bool(f == "yo" and d == -0.1 and e == 1.1)

        def y() -> bool:
            d = {"d": 1.0}
            return x(1, 3, "hi", f="yo")
        """
        y = self.find_code(self.compile(codestr, modname="foo"), name="y")
        self.assertInBytecode(y, "LOAD_CONST", 1.1)
        with self.in_module(codestr) as mod:
            y_callable = mod.y
            self.assertTrue(y_callable())

    def test_default_arg_non_const(self):
        codestr = """
        class C: pass
        def x(val=C()) -> C:
            return val

        def f() -> C:
            return x()
        """
        with self.in_module(codestr) as mod:
            f = mod.f
            self.assertInBytecode(f, "CALL_FUNCTION")

    def test_default_arg_non_const_kw_provided(self):
        codestr = """
        class C: pass
        def x(val:object=C()):
            return val

        def f():
            return x(val=42)
        """

        with self.in_module(codestr) as mod:
            f = mod.f
            self.assertEqual(f(), 42)

    def test_load_mapping_arg_order(self):
        codestr = """
        def x(a: int, b: int, c: str, d: float=-0.1, e: float=1.1, f: str="something") -> bool:
            return bool(
                a == 1
                and b == 3
                and c == "hi"
                and d == 1.1
                and e == 3.3
                and f == "hmm"
            )

        stuff = []
        def q() -> float:
            stuff.append("q")
            return 1.1

        def r() -> float:
            stuff.append("r")
            return 3.3

        def s() -> str:
            stuff.append("s")
            return "hmm"

        def y() -> bool:
            return x(1, 3, "hi", f=s(), d=q(), e=r())
        """
        y = self.find_code(self.compile(codestr, modname="foo"), name="y")
        self.assertInBytecode(y, "STORE_FAST", "_pystatic_.0._tmp__d")
        self.assertInBytecode(y, "LOAD_FAST", "_pystatic_.0._tmp__d")
        with self.in_module(codestr) as mod:
            y_callable = mod.y
            self.assertTrue(y_callable())
            self.assertEqual(["s", "q", "r"], mod.stuff)

    def test_load_mapping_arg_order_with_variadic_kw_args(self):
        codestr = """
        def x(a: int, b: int, c: str, d: float=-0.1, e: float=1.1, f: str="something", g: str="look-here") -> bool:
            return bool(
                a == 1
                and b == 3
                and c == "hi"
                and d == 1.1
                and e == 3.3
                and f == "hmm"
                and g == "overridden"
            )

        stuff = []
        def q() -> float:
            stuff.append("q")
            return 1.1

        def r() -> float:
            stuff.append("r")
            return 3.3

        def s() -> str:
            stuff.append("s")
            return "hmm"

        def y() -> bool:
            kw = {"g": "overridden"}
            return x(1, 3, "hi", f=s(), **kw, d=q(), e=r())
        """
        y = self.find_code(self.compile(codestr, modname="foo"), name="y")
        self.assertInBytecode(y, "STORE_FAST", "_pystatic_.0._tmp__d")
        self.assertInBytecode(y, "LOAD_FAST", "_pystatic_.0._tmp__d")
        with self.in_module(codestr) as mod:
            y_callable = mod.y
            self.assertTrue(y_callable())
            self.assertEqual(["s", "q", "r"], mod.stuff)

    def test_load_mapping_arg_order_with_variadic_kw_args_one_positional(self):
        codestr = """
        def x(a: int, b: int, c: str, d: float=-0.1, e: float=1.1, f: str="something", g: str="look-here") -> bool:
            return bool(
                a == 1
                and b == 3
                and c == "hi"
                and d == 1.1
                and e == 3.3
                and f == "hmm"
                and g == "overridden"
            )

        stuff = []
        def q() -> float:
            stuff.append("q")
            return 1.1

        def r() -> float:
            stuff.append("r")
            return 3.3

        def s() -> str:
            stuff.append("s")
            return "hmm"


        def y() -> bool:
            kw = {"g": "overridden"}
            return x(1, 3, "hi", 1.1, f=s(), **kw, e=r())
        """
        y = self.find_code(self.compile(codestr, modname="foo"), name="y")
        self.assertNotInBytecode(y, "STORE_FAST", "_pystatic_.0._tmp__d")
        self.assertNotInBytecode(y, "LOAD_FAST", "_pystatic_.0._tmp__d")
        with self.in_module(codestr) as mod:
            y_callable = mod.y
            self.assertTrue(y_callable())
            self.assertEqual(["s", "r"], mod.stuff)

    def test_load_mapping_arg_stack_effect(self) -> None:
        codestr = """
        def g(x=None) -> None:
            pass

        def f():
            return [
                g(**{})
                for i in ()
            ]
        """
        with self.in_module(codestr) as mod:
            f = mod.f
            if self._inline_comprehensions:
                self.assertInBytecode(f, "LOAD_MAPPING_ARG", 3)
            else:
                self.assertNotInBytecode(f, "LOAD_MAPPING_ARG", 3)
            self.assertEqual(f(), [])
