from compiler.static import CACHED_PROPERTY_IMPL_PREFIX
from compiler.static.errors import TypedSyntaxError

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
                AttributeError,
                f"This object has no __dict__",
            ):
                C().x

            D = mod.D
            with self.assertRaisesRegex(AttributeError, "This object has no __dict__"):
                D().x
