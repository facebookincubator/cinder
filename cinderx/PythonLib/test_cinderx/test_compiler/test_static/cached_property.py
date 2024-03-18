import asyncio
import unittest

from cinder import async_cached_property, cached_property
from unittest import skip

from cinderx.compiler.errors import TypedSyntaxError
from cinderx.compiler.pycodegen import PythonCodeGenerator
from cinderx.compiler.static import (
    ASYNC_CACHED_PROPERTY_IMPL_PREFIX,
    CACHED_PROPERTY_IMPL_PREFIX,
)

from .common import StaticTestBase


class CachedPropertyTests(StaticTestBase):
    def test_cached_property(self):
        codestr = """
        from cinder import cached_property

        class C:
            def __init__(self):
                self.hit_count = 0

            @cached_property
            def x(self):
                self.hit_count += 1
                return 3
        """
        with self.in_strict_module(codestr) as mod:
            C = mod.C
            c = C()
            self.assertEqual(c.x, 3)
            self.assertEqual(c.hit_count, 1)

            # This next access shouldn't bump the hit count
            self.assertEqual(c.x, 3)
            self.assertEqual(c.hit_count, 1)

    def test_cached_property_invoked(self):
        codestr = """
        from cinder import cached_property

        class C:
            def __init__(self):
                self.hit_count = 0

            @cached_property
            def x(self):
                self.hit_count += 1
                return 3

        def f() -> C:
            c = C()
            c.x
            c.x
            return c
        """
        with self.in_strict_module(codestr) as mod:
            self.assertInBytecode(
                mod.f,
                "INVOKE_METHOD",
                ((mod.__name__, "C", ("x", "fget")), 0),
            )
            r = mod.f()
            self.assertEqual(r.hit_count, 1)

    def test_cached_property_invoked_frozen(self):
        codestr = """
        from typing import final
        from cinder import cached_property

        @final
        class C:
            def __init__(self):
                self.hit_count = 0

            @cached_property
            def x(self):
                self.hit_count += 1
                return 3

        def f() -> C:
            c = C()
            c.x
            c.x
            return c
        """
        with self.in_strict_module(codestr, freeze=True) as mod:
            self.assertInBytecode(
                mod.f,
                "INVOKE_METHOD",
                ((mod.__name__, "C", ("x", "fget")), 0),
            )
            r = mod.f()
            self.assertEqual(r.hit_count, 1)

    def test_multiple_cached_properties(self):
        codestr = """
        from cinder import cached_property

        class C:
            def __init__(self):
                self.hit_count_x = 0
                self.hit_count_y = 0

            @cached_property
            def x(self):
                self.hit_count_x += 1
                return 3

            @cached_property
            def y(self):
                self.hit_count_y += 1
                return 7
        """
        with self.in_strict_module(codestr) as mod:
            C = mod.C
            c = C()
            self.assertEqual(c.x, 3)
            self.assertEqual(c.hit_count_x, 1)

            self.assertEqual(c.y, 7)
            self.assertEqual(c.hit_count_y, 1)

            # This next access shouldn't bump the hit count
            self.assertEqual(c.x, 3)
            self.assertEqual(c.hit_count_x, 1)

            # This next access shouldn't bump the hit count
            self.assertEqual(c.y, 7)
            self.assertEqual(c.hit_count_y, 1)

    def test_cached_property_on_class(self):
        codestr = """
        from cinder import cached_property

        @cached_property
        class C:
            pass
        """
        with self.assertRaisesRegex(
            TypedSyntaxError, "Cannot decorate a class with @cached_property"
        ):
            self.compile(codestr)

    def test_cached_property_intermediary_cleaned_up(self):
        codestr = """
        from cinder import cached_property

        class C:
            def __init__(self):
                self.hit_count = 0

            @cached_property
            def x(self):
                self.hit_count += 1
                return 3
        """
        with self.in_strict_module(codestr) as mod:
            C = mod.C
            self.assertEqual(C().x, 3)
            with self.assertRaisesRegex(
                AttributeError,
                f"type object 'C' has no attribute '{CACHED_PROPERTY_IMPL_PREFIX}x'",
            ):
                getattr(C, CACHED_PROPERTY_IMPL_PREFIX + "x")

    def test_cached_property_skip_decorated_methods(self):
        codestr = """
        from cinder import cached_property

        def my_decorator(fn):
            return fn

        class C:
            @cached_property
            @my_decorator
            def x(self):
                pass

        class D:
            @my_decorator
            @cached_property
            def x(self):
                pass
        """
        with self.in_strict_module(codestr) as mod:
            C = mod.C
            with self.assertRaisesRegex(
                TypeError,
                f"No '__dict__' attribute on 'C' instance to cache 'x' property.",
            ):
                C().x

            D = mod.D
            with self.assertRaisesRegex(
                TypeError,
                f"No '__dict__' attribute on 'D' instance to cache 'x' property.",
            ):
                D().x

    def test_cached_property_override_property(self):
        codestr = """
        from cinder import cached_property

        class C:
            @property
            def x(self):
                return 3

        class D(C):
            @cached_property
            def x(self):
                return 4
        """
        with self.in_strict_module(codestr) as mod:
            C = mod.C
            D = mod.D
            self.assertEqual(C().x, 3)
            self.assertEqual(D().x, 4)

    def test_property_override_cached_property(self):
        codestr = """
        from cinder import cached_property

        class C:
            @cached_property
            def x(self):
                return 3

        class D(C):
            @property
            def x(self):
                return 4
        """
        with self.in_strict_module(codestr) as mod:
            C = mod.C
            D = mod.D
            self.assertEqual(C().x, 3)
            self.assertEqual(D().x, 4)

    def test_cached_property_override_cached_property(self):
        codestr = """
        from cinder import cached_property

        class C:
            @cached_property
            def x(self):
                return 3

        class D(C):
            @cached_property
            def x(self):
                return 4
        """
        with self.in_strict_module(codestr) as mod:
            C = mod.C
            D = mod.D
            self.assertEqual(C().x, 3)
            self.assertEqual(D().x, 4)

    def test_cached_property_override_cached_property_non_static(self):
        codestr = """
        from cinder import cached_property

        class C:
            @cached_property
            def x(self):
                return 3

        """
        with self.in_strict_module(codestr) as mod:
            C = mod.C

            class D(C):
                @cached_property
                def x(self):
                    return 4

            self.assertEqual(C().x, 3)
            self.assertEqual(D().x, 4)

    def test_property_override_cached_property_non_static(self):
        codestr = """
        from cinder import cached_property

        class C:
            @cached_property
            def x(self):
                return 3

        """
        with self.in_strict_module(codestr) as mod:
            C = mod.C

            class D(C):
                @property
                def x(self):
                    return 4

            self.assertEqual(C().x, 3)
            self.assertEqual(D().x, 4)

    def test_cached_property_override_cached_property_non_static_invoked(self):
        codestr = """
        from cinder import cached_property

        class C:
            @cached_property
            def x(self):
                return 3

        def f(c: C):
            return c.x
        """
        with self.in_strict_module(codestr) as mod:
            C = mod.C

            class D(C):
                def __init__(self):
                    self.hit_count = 0

                @cached_property
                def x(self):
                    self.hit_count += 1
                    return 4

            d = D()
            self.assertEqual(mod.f(d), 4)
            self.assertEqual(d.hit_count, 1)
            mod.f(D())
            self.assertEqual(d.hit_count, 1)

    def test_async_cached_property(self):
        codestr = """
        from cinder import async_cached_property

        class C:
            def __init__(self):
                self.hit_count = 0

            @async_cached_property
            async def x(self) -> int:
                self.hit_count += 1
                return 3
        """
        with self.in_strict_module(codestr) as mod:
            C = mod.C
            c = C()

            async def await_c_x():
                res = await c.x
                return res

            self.assertEqual(asyncio.run(await_c_x()), 3)
            self.assertEqual(c.hit_count, 1)

            # This next access shouldn't bump the hit count
            self.assertEqual(asyncio.run(await_c_x()), 3)
            self.assertEqual(c.hit_count, 1)

    def test_async_cached_property_invoked(self):
        codestr = """
        from cinder import async_cached_property

        class C:
            def __init__(self):
                self.hit_count = 0

            @async_cached_property
            async def x(self):
                self.hit_count += 1
                return 3

        async def f() -> C:
            c = C()
            await c.x
            await c.x
            return c
        """
        with self.in_strict_module(codestr) as mod:
            self.assertInBytecode(
                mod.f,
                "INVOKE_METHOD",
                ((mod.__name__, "C", ("x", "fget")), 0),
            )
            r = asyncio.run(mod.f())
            self.assertEqual(r.hit_count, 1)

    def test_async_cached_property_invoked_frozen(self):
        codestr = """
        from typing import final
        from cinder import async_cached_property

        @final
        class C:
            def __init__(self):
                self.hit_count = 0

            @async_cached_property
            async def x(self):
                self.hit_count += 1
                return 3

        async def f() -> C:
            c = C()
            await c.x
            await c.x
            return c
        """
        with self.in_strict_module(codestr, freeze=True) as mod:
            self.assertInBytecode(
                mod.f,
                "INVOKE_METHOD",
                ((mod.__name__, "C", ("x", "fget")), 0),
            )
            r = asyncio.run(mod.f())
            self.assertEqual(r.hit_count, 1)

    def test_multiple_async_cached_properties(self):
        codestr = """
        from cinder import async_cached_property

        class C:
            def __init__(self):
                self.hit_count_x = 0
                self.hit_count_y = 0

            @async_cached_property
            async def x(self):
                self.hit_count_x += 1
                return 3

            @async_cached_property
            async def y(self):
                self.hit_count_y += 1
                return 7
        """
        with self.in_strict_module(codestr) as mod:
            C = mod.C
            c = C()

            async def await_c_x():
                res = await c.x
                return res

            async def await_c_y():
                res = await c.y
                return res

            self.assertEqual(asyncio.run(await_c_x()), 3)
            self.assertEqual(c.hit_count_x, 1)
            self.assertEqual(c.hit_count_y, 0)

            self.assertEqual(asyncio.run(await_c_y()), 7)
            self.assertEqual(c.hit_count_y, 1)

            # This next access shouldn't bump the hit count
            self.assertEqual(asyncio.run(await_c_x()), 3)
            self.assertEqual(c.hit_count_x, 1)

            # This next access shouldn't bump the hit count
            self.assertEqual(asyncio.run(await_c_y()), 7)
            self.assertEqual(c.hit_count_y, 1)

    def test_async_cached_property_on_class(self):
        codestr = """
        from cinder import async_cached_property

        @async_cached_property
        class C:
            pass
        """
        with self.assertRaisesRegex(
            TypedSyntaxError, "Cannot decorate a class with @async_cached_property"
        ):
            self.compile(codestr)

    def test_async_cached_property_intermediary_cleaned_up(self):
        codestr = """
        from cinder import async_cached_property

        class C:
            def __init__(self):
                self.hit_count = 0

            @async_cached_property
            async def x(self):
                self.hit_count += 1
                return 3
        """
        with self.in_strict_module(codestr) as mod:
            C = mod.C
            c = C()

            async def await_c_x():
                res = await c.x
                return res

            self.assertEqual(asyncio.run(await_c_x()), 3)
            with self.assertRaisesRegex(
                AttributeError,
                f"type object 'C' has no attribute '{ASYNC_CACHED_PROPERTY_IMPL_PREFIX}x'",
            ):
                getattr(C, ASYNC_CACHED_PROPERTY_IMPL_PREFIX + "x")

    def test_async_cached_property_override_property(self):
        codestr = """
        from cinder import async_cached_property

        class C:
            @property
            async def x(self):
                return 3

        class D(C):
            @async_cached_property
            async def x(self):
                return 4
        """
        with self.in_strict_module(codestr) as mod:
            C = mod.C
            D = mod.D

            async def await_c_x():
                res = await C().x
                return res

            async def await_d_x():
                res = await D().x
                return res

            self.assertEqual(asyncio.run(await_c_x()), 3)
            self.assertEqual(asyncio.run(await_d_x()), 4)

    def test_property_override_async_cached_property(self):
        codestr = """
        from cinder import async_cached_property

        class C:
            @async_cached_property
            async def x(self):
                return 3

        class D(C):
            @property
            async def x(self):
                return 4
        """
        with self.in_strict_module(codestr) as mod:
            C = mod.C
            D = mod.D

            async def await_c_x():
                res = await C().x
                return res

            async def await_d_x():
                res = await D().x
                return res

            self.assertEqual(asyncio.run(await_c_x()), 3)
            self.assertEqual(asyncio.run(await_d_x()), 4)

    def test_async_cached_property_override_async_cached_property(self):
        codestr = """
        from cinder import async_cached_property

        class C:
            @async_cached_property
            async def x(self) -> int:
                return 3

        class D(C):
            @async_cached_property
            async def x(self) -> int:
                return 4

        def async_get_x(c: C) -> int:
            return await c.x
        """
        with self.in_strict_module(codestr) as mod:
            C = mod.C
            D = mod.D

            self.assertEqual(asyncio.run(mod.async_get_x(C())), 3)
            self.assertEqual(asyncio.run(mod.async_get_x(D())), 4)

    def test_async_cached_property_override_async_cached_property_non_static(self):
        codestr = """
        from cinder import async_cached_property

        class C:
            @async_cached_property
            async def x(self):
                return 3

        """
        with self.in_strict_module(codestr) as mod:
            C = mod.C

            class D(C):
                @async_cached_property
                async def x(self):
                    return 4

            async def await_c_x():
                res = await C().x
                return res

            async def await_d_x():
                res = await D().x
                return res

            self.assertEqual(asyncio.run(await_c_x()), 3)
            self.assertEqual(asyncio.run(await_d_x()), 4)

    def test_async_cached_property_override_async_cached_property_2(self):
        codestr = """
        from cinder import async_cached_property

        class C:
            @async_cached_property
            async def x(self):
                return 3

        class D(C):
            @async_cached_property
            async def x(self):
                return 4
        """
        with self.in_strict_module(codestr) as mod:
            C = mod.C
            D = mod.D

            async def await_c_x():
                res = await C().x
                return res

            async def await_d_x():
                res = await D().x
                return res

            self.assertEqual(asyncio.run(await_c_x()), 3)
            self.assertEqual(asyncio.run(await_d_x()), 4)

    def test_async_cached_property_on_class_raises_type_error(self):
        codestr = """
        from cinder import async_cached_property

        @async_cached_property
        class C:
            pass
        """
        self.type_error(codestr, "Cannot decorate a class with @async_cached_property")

    def test_async_cached_property_setter_raises_type_error(self):
        codestr = """
        from cinder import async_cached_property

        class C:
            @async_cached_property
            async def fn(self) -> int:
                return 3

            @fn.setter
            def fn(self, new: int) -> None:
                pass
        """
        self.type_error(
            codestr, "async_cached_property <module>.C.fn does not support setters"
        )

    def test_cached_property_setter_raises_type_error(self):
        codestr = """
        from cinder import cached_property

        class C:
            @cached_property
            def fn(self) -> int:
                return 3

            @fn.setter
            def fn(self, new: int) -> None:
                pass
        """
        self.type_error(
            codestr, "cached_property <module>.C.fn does not support setters"
        )

    def test_cached_property_reset(self):
        nonstatic_codestr = """
        class A:
            pass
        """
        with self.in_module(
            nonstatic_codestr, code_gen=PythonCodeGenerator
        ) as nonstatic_mod:

            codestr = f"""
            from {nonstatic_mod.__name__} import A
            from cinder import cached_property

            class C(A):
                def __init__(self):
                    self.ctr = 0

                @cached_property
                def x(self):
                    self.ctr += 1
                    return 3
            """
            with self.in_strict_module(
                codestr, enable_patching=True, freeze=False
            ) as mod:
                C = mod.C
                c = C()

                # First access, should bump ctr
                self.assertEqual(c.x, 3)
                self.assertEqual(c.ctr, 1)

                # Second access, should not bump ctr
                self.assertEqual(c.x, 3)
                self.assertEqual(c.ctr, 1)

                del c.x
                # Access after reset, should bump ctr
                self.assertEqual(c.x, 3)
                self.assertEqual(c.ctr, 2)

    def test_async_cached_property_reset(self):
        nonstatic_codestr = """
        class A:
            pass
        """
        with self.in_module(
            nonstatic_codestr, code_gen=PythonCodeGenerator
        ) as nonstatic_mod:

            codestr = f"""
            from {nonstatic_mod.__name__} import A
            from cinder import async_cached_property

            class C(A):
                def __init__(self):
                    self.ctr = 0

                @async_cached_property
                async def x(self):
                    self.ctr += 1
                    return 3
            """
            with self.in_strict_module(
                codestr, enable_patching=True, freeze=False
            ) as mod:
                C = mod.C
                c = C()

                async def await_x(z):
                    return await z

                # First access, should bump ctr
                self.assertEqual(asyncio.run(await_x(c.x)), 3)
                self.assertEqual(c.ctr, 1)

                # Second access, should not bump ctr
                self.assertEqual(asyncio.run(await_x(c.x)), 3)
                self.assertEqual(c.ctr, 1)

                del c.x

                # Access after reset, should bump ctr
                self.assertEqual(asyncio.run(await_x(c.x)), 3)
                self.assertEqual(c.ctr, 2)
