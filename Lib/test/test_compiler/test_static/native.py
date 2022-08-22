from .common import StaticTestBase


class NativeDecoratorTests(StaticTestBase):
    def test_native_no_lib(self):
        codestr = """
        from __static__ import native, int64

        @native
        def something(i: int64) -> int64:
            return 1
        """
        self.type_error(
            codestr, "@native decorator must specify the library to be loaded"
        )

    def test_native_kwarg(self):
        codestr = """
        from __static__ import native, int64

        @native(x=1)
        def something(i: int64) -> int64:
            return 1
        """
        self.type_error(codestr, "@native decorator takes no keyword arguments")

    def test_native_stararg(self):
        codestr = """
        from __static__ import native, int64

        a = (1, 2)

        @native(*a)
        def something(i: int64) -> int64:
            return 1
        """
        self.type_error(codestr, "@native decorator takes no starred arguments")

    def test_native_multiple_arg(self):
        codestr = """
        from __static__ import native, int64

        @native("so.so", 1)
        def something(i: int64) -> int64:
            return 1
        """
        self.type_error(
            codestr,
            "@native decorator accepts a single parameter, the path to .so file",
        )

    def test_native_decorate_class(self):
        codestr = """
        from __static__ import native

        @native("so.so")
        class Hi:
            pass
        """
        self.type_error(codestr, "Cannot decorate a class with @native")

    def test_native_decorate_method(self):
        codestr = """
        from __static__ import native

        class Hi:
            @native("so.so")
            def fn(self):
                pass
        """
        self.type_error(codestr, "Cannot decorate a method with @native")

    def test_native_some_function_body(self):
        codestr = """
        from __static__ import native, int64

        @native("so.so")
        def something(i: int64) -> int64:
            return 1
        """

        self.type_error(
            codestr,
            "@native callables cannot contain a function body, only 'pass' is allowed",
        )

    def test_native_valid_usage_in_nonstatic_module(self):
        codestr = """
        from __static__ import native, int64

        @native("so.so")
        def something(i: int64) -> int64:
            pass
        """

        with self.in_strict_module(codestr) as mod:
            with self.assertRaisesRegex(
                RuntimeError,
                "native callable 'something' can only be called from static modules",
            ):
                mod.something(1)
