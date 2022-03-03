from .common import StaticTestBase


class PerfLintTests(StaticTestBase):
    def test_two_starargs(self) -> None:
        codestr = """
        def f(x: int, y: int, z: int) -> int:
            return x + y + z

        a = [1, 2]
        b = [3]
        f(*a, *b)
        """

        errors = self.perf_lint(codestr)
        errors.check_warnings(
            errors.match(
                "Multiple *args prevents more efficient static call",
                at="f(*a, *b)",
            ),
        )

    def test_positional_after_starargs(self) -> None:
        codestr = """
        def f(x: int, y: int, z: int) -> int:
            return x + y + z

        a = [1, 2]
        f(*a, 3)
        """

        errors = self.perf_lint(codestr)
        errors.check_warnings(
            errors.match(
                "Positional arg after *args prevents more efficient static call",
                at="f(*a, 3)",
            ),
        )

    def test_multiple_kwargs(self) -> None:
        codestr = """
        def f(x: int, y: int, z: int) -> int:
            return x + y + z

        a = {{"x": 1, "y": 2}}
        b = {{"z": 3}}
        f(**a, **b)
        """

        errors = self.perf_lint(codestr)
        errors.check_warnings(
            errors.match(
                "Multiple **kwargs prevents more efficient static call",
                at="f(**a, **b)",
            ),
        )

    def test_starargs_and_default(self) -> None:
        codestr = """
        def f(x: int, y: int, z: int = 0) -> int:
            return x + y + z

        a = [3]
        f(1, 2, *a)
        """

        errors = self.perf_lint(codestr)
        errors.check_warnings(
            errors.match(
                "Passing *args to function with default values prevents more efficient static call",
                at="f(1, 2, *a)",
            ),
        )

    def test_kwonly(self) -> None:
        codestr = """
        def f(*, x: int = 0) -> int:
            return x

        f(1)
        """

        errors = self.perf_lint(codestr)
        errors.check_warnings(
            errors.match(
                "Keyword-only args in called function prevents more efficient static call",
                at="f(1)",
            ),
        )

    def test_load_attr_dynamic(self) -> None:
        codestr = """
        a.b
        """

        errors = self.perf_lint(codestr)
        errors.check_warnings(
            errors.match(
                "Define the object's class in a Static Python "
                "module for more efficient attribute load",
                at="a.b",
            ),
        )

    def test_load_attr_dynamic_base(self) -> None:
        codestr = """
        class C(B):
            pass

        c = C()
        c.a
        """

        errors = self.perf_lint(codestr)
        errors.check_warnings(
            errors.match(
                "Make the base class of <module>.C that defines "
                "attribute a static for more efficient attribute load",
                at="c.a",
            ),
        )

    def test_store_attr_dynamic(self) -> None:
        codestr = """
        a.b = c
        """

        errors = self.perf_lint(codestr)
        errors.check_warnings(
            errors.match(
                "Define the object's class in a Static Python "
                "module for more efficient attribute store",
                at="a.b = c",
            ),
        )

    def test_store_attr_dynamic_base(self) -> None:
        codestr = """
        class C(B):
            pass

        c = C()
        c.a = 1
        """

        errors = self.perf_lint(codestr)
        errors.check_warnings(
            errors.match(
                "Make the base class of <module>.C that defines "
                "attribute a static for more efficient attribute store",
                at="c.a",
            ),
        )

    def test_nonfinal_property_load(self) -> None:
        codestr = """
        class C:
            @property
            def a(self) -> int:
                return 0

        c = C()
        c.a
        """

        errors = self.perf_lint(codestr)
        errors.check_warnings(
            errors.match(
                "Getter for property a can be overridden. Make method "
                "or class final for more efficient property load",
                at="c.a",
            ),
        )

    def test_property_setter_no_warning(self) -> None:
        codestr = """
        class C:
            @property
            def a(self) -> int:
                return 0

            @a.setter
            def a(self, value: int) -> None:
                pass
        """

        errors = self.perf_lint(codestr)
        errors.check_warnings()

    def test_nonfinal_property_store(self) -> None:
        codestr = """
        class C:
            @property
            def a(self) -> int:
                return 0

            @a.setter
            def a(self, value: int) -> None:
                pass

        c = C()
        c.a = 1
        """

        errors = self.perf_lint(codestr)
        errors.check_warnings(
            errors.match(
                "Setter for property a can be overridden. Make method "
                "or class final for more efficient property store",
                at="c.a = 1",
            ),
        )

    def test_nonfinal_method_call(self) -> None:
        codestr = """
        class C:
            def add1(self, n: int) -> int:
                return n + 1

        def foo(c: C) -> None:
            c.add1(10)
        """

        errors = self.perf_lint(codestr)
        errors.check_warnings(
            errors.match(
                "Method add1 can be overridden. Make method or class final for more efficient call",
                at="c.add1(10)",
            )
        )

    def test_final_class_method_call(self) -> None:
        codestr = """
        @final
        class C:
            def add1(self, n: int) -> int:
                return n + 1

        c = C()
        c.add1(10)
        """

        errors = self.perf_lint(codestr)
        errors.check_warnings()

    def test_final_method_method_call(self) -> None:
        codestr = """
        class C:
            @final
            def add1(self, n: int) -> int:
                return n + 1

        c = C()
        c.add1(10)
        """

        errors = self.perf_lint(codestr)
        errors.check_warnings()

    def test_nonfinal_classmethod_call(self) -> None:
        codestr = """
        class C:
            @classmethod
            def add1(cls, n: int) -> int:
                return n + 1

            @classmethod
            def add2(cls, n: int) -> int:
                return cls.add1(n) + 1
        """

        errors = self.perf_lint(codestr)
        errors.check_warnings(
            errors.match(
                "Method add1 can be overridden. Make method or class final for more efficient call",
                at="cls.add1(n)",
            )
        )

    def test_final_class_classmethod_call(self) -> None:
        codestr = """
        @final
        class C:
            @classmethod
            def add1(cls, n: int) -> int:
                return n + 1

        C.add1(10)
        """

        errors = self.perf_lint(codestr)
        errors.check_warnings()

    def test_final_method_classmethod_call(self) -> None:
        codestr = """
        class C:
            @final
            @classmethod
            def add1(cls, n: int) -> int:
                return n + 1

        C.add1(10)
        """

        errors = self.perf_lint(codestr)
        errors.check_warnings()
