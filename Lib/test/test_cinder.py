# Copyright (c) Facebook, Inc. and its affiliates. (http://www.facebook.com)
import asyncio
import asyncio.tasks
import inspect
import sys
import unittest
import weakref
from functools import wraps
from textwrap import dedent
from types import FunctionType, GeneratorType, ModuleType, CodeType

import cinder
from cinder import (
    async_cached_property,
    async_cached_classproperty,
    cached_property,
    cached_classproperty,
    StrictModule,
    strict_module_patch,
)
from test.support import gc_collect, requires_type_collecting, temp_dir, unlink, TESTFN
from test.support.cinder import get_await_stack
from test.support.script_helper import assert_python_ok, make_script


class NoWayError(Exception):
    pass


class LeakDetector:
    def __init__(self, finalized):
        self.finalized = finalized

    def __del__(self):
        self.finalized[0] = True


def create_strict_module(
    name="foo", filename="foo.py", enable_patching=False, **kwargs
):
    kwargs.update(__name__=name, __file__=filename)
    return StrictModule(kwargs, enable_patching)


def strict_module_from_module(mod, enable_patching=False):
    return StrictModule(dict(mod.__dict__), enable_patching)


class CinderTest(unittest.TestCase):
    def test_type_cache(self):
        class C:
            x = 42

        a = C()
        self.assertEqual(a.x, 42)
        sys._clear_type_cache()
        C.x = 100
        self.assertEqual(a.x, 100)

    def test_recompute_func_entry_for_defaults(self):
        """Update function __defaults__ *after* creation

        Function entry point should be re-computed
        """

        # sanity check
        def foofunc(a, b):
            return a + b

        self.assertEqual(foofunc(1, 2), 3)

        # error due to missing positional arguments
        with self.assertRaises(TypeError):
            foofunc()

        foofunc.__defaults__ = (3, 4)
        self.assertEqual(foofunc(), 7)

    def test_recompute_func_entry_for_kwonly(self):
        """Change function __code__ after creation, adding kwonly args

        Function entry point should be re-computed
        """

        def f():
            return "f"

        def kwonly(*, a, b):
            return "kwonly"

        self.assertEqual(f(), "f")

        f.__code__ = kwonly.__code__

        with self.assertRaises(TypeError):
            f()
        with self.assertRaises(TypeError):
            f(1, 2)
        self.assertEqual(f(a=1, b=2), "kwonly")

    @unittest.skip("re-enable once shadowcode is ported")
    def test_knob(self):
        try:
            knobs = cinder.getknobs()
            original = knobs["shadowcode"]

            cinder.setknobs({"shadowcode": not original})

            knobs = cinder.getknobs()
            self.assertEqual(knobs["shadowcode"], not original)

        finally:
            cinder.setknobs({"shadowcode": original})
            knobs = cinder.getknobs()
            self.assertEqual(knobs["shadowcode"], original)

    @requires_type_collecting
    def test_global_del_SystemExit(self):
        code = """if 1:
            import cinder
            cinder.setknobs({'skipfinalcleanup': True})
            class ClassWithDel:
                def __del__(self):
                    print('__del__ called')
            a = ClassWithDel()
            a.link = a
            raise SystemExit(0)"""
        self.addCleanup(unlink, TESTFN)
        with open(TESTFN, "w") as script:
            script.write(code)
        rc, out, err = assert_python_ok(TESTFN)
        self.assertEqual(out.strip(), b"")

    def test_type_freeze(self):
        class C:
            pass

        cinder.freeze_type(C)

        with self.assertRaisesRegex(
            TypeError, "type 'C' has been frozen and cannot be modified"
        ):
            C.foo = 42

        class D:
            x = 42

        cinder.freeze_type(D)

        with self.assertRaisesRegex(
            TypeError, "type 'D' has been frozen and cannot be modified"
        ):
            D.foo = 42

        with self.assertRaisesRegex(
            TypeError, "type 'D' has been frozen and cannot be modified"
        ):
            del D.foo

    def test_type_freeze_bad_arg(self):
        with self.assertRaisesRegex(TypeError, "freeze_type requires a type"):
            cinder.freeze_type(42)

    def test_cached_class_prop(self):
        class C:
            @cached_classproperty
            def f(self):
                return 42

        self.assertEqual(C.f, 42)

    def test_cached_class_prop_subtype(self):
        class ST(cached_classproperty):
            pass

        class C:
            @ST
            def f(self):
                return 42

        self.assertEqual(C.f, 42)

    def test_cached_class_prop_called_once(self):
        class C:
            calls = 0

            @cached_classproperty
            def f(cls):
                cls.calls += 1
                return 42

        self.assertEqual(C.f, 42)
        self.assertEqual(C.f, 42)
        self.assertEqual(C.calls, 1)

    def test_cached_class_prop_descr(self):
        """verifies the descriptor protocol isn't invoked on the cached value"""

        class classproperty:
            def __get__(self, inst, ctx):
                return 42

        clsprop = classproperty()

        class C:
            @cached_classproperty
            def f(cls):
                return clsprop

        self.assertEqual(C.f, clsprop)
        self.assertEqual(C.f, clsprop)

    def test_cached_class_prop_descr_raises(self):
        class classproperty(LeakDetector):
            def __get__(self, inst, ctx):
                raise NoWayError()

        finalized = [False]

        class C:
            @cached_classproperty
            def f(cls):
                return classproperty(finalized)

        x = C.f

        # descriptor is cached in the type...
        self.assertEqual(finalized, [False])

        # and we can still invoke it
        x = C.f
        self.assertEqual(type(C.__dict__["f"]), cached_classproperty)
        del C.f
        del x
        self.assertEqual(finalized, [True])

    def test_cached_class_prop_inst_method(self):
        """verifies the descriptor protocol isn't invoked on the cached value"""

        class C:
            def __init__(self, value):
                self.value = value

            @cached_classproperty
            def f(cls):
                return lambda self: self.value

        self.assertEqual(C(42).f(C(100)), 100)

    def test_cached_class_prop_inheritance(self):
        class C:
            @cached_classproperty
            def f(cls):
                return cls.__name__

        class D(C):
            pass

        self.assertEqual(C.f, "C")
        self.assertEqual(D.f, "C")

    def test_cached_class_prop_inheritance_reversed(self):
        class C:
            @cached_classproperty
            def f(cls):
                return cls.__name__

        class D(C):
            pass

        self.assertEqual(D.f, "D")
        self.assertEqual(C.f, "D")

    def test_cached_class_prop_recursion(self):
        depth = 0

        class C:
            @cached_classproperty
            def f(cls):
                nonlocal depth
                depth += 1
                if depth == 2:
                    return 2
                x = C.f
                return 1

        self.assertEqual(C.f, 2)

    def test_cached_class_prop_inst_method_no_inst(self):
        class C:
            def __init__(self, value):
                self.value = value

            @cached_classproperty
            def f(cls):
                return lambda self: self.value

        self.assertEqual(type(C.f), FunctionType)

    def test_cached_class_prop_inst(self):
        class C:
            @cached_classproperty
            def f(cls):
                return 42

        self.assertEqual(C().f, 42)

    def test_cached_class_prop_frozen_type(self):
        class C:
            @cached_classproperty
            def f(cls):
                return 42

        cinder.freeze_type(C)
        self.assertEqual(C.f, 42)

    def test_cached_class_prop_frozen_type_inst(self):
        class C:
            @cached_classproperty
            def f(cls):
                return 42

        cinder.freeze_type(C)
        self.assertEqual(C().f, 42)

    def test_cached_class_prop_setattr_fails(self):
        class metatype(type):
            def __setattr__(self, name, value):
                if name == "f":
                    raise NoWayError()

        class C(metaclass=metatype):
            @cached_classproperty
            def f(self):
                return 42

        self.assertEqual(C.f, 42)

    def test_cached_class_prop_doc(self):
        class C:
            @cached_classproperty
            def f(cls):
                "hi"
                return 42

        self.assertEqual(C.__dict__["f"].__doc__, "hi")
        self.assertEqual(C.__dict__["f"].name, "f")
        self.assertEqual(type(C.__dict__["f"].func), FunctionType)

    def test_warn_on_type_dict_non_type(self):
        with self.assertRaises(TypeError):
            cinder.warn_on_inst_dict(42)

    def test_warn_on_type_dict_no_callback(self):
        class C:
            pass

        cinder.warn_on_inst_dict(C)

        a = C()
        a.foo = 42
        self.assertEqual(a.foo, 42)

    def test_warn_on_type_dict(self):
        class C:
            pass

        msg = type = attr = None

        def cb(*args):
            nonlocal msg, type, attr
            msg = args[0]
            type = args[1]
            attr = args[2]

        cinder.cinder_set_warn_handler(cb)
        try:
            cinder.warn_on_inst_dict(C)

            a = C()
            a.foo = 42

            self.assertEqual(msg, "WARN001: Dictionary created for flagged instance")
            self.assertEqual(type, C)
            self.assertEqual(attr, "foo")
            self.assertEqual(a.foo, 42)

            a.bar = 42

            self.assertEqual(msg, "WARN001: Dictionary created for flagged instance")
            self.assertEqual(type, C)
            self.assertEqual(attr, "foo")
        finally:
            cinder.cinder_set_warn_handler(None)

    def test_warn_on_type_dict_non_split_keys(self):
        class C:
            pass

        msg = type = attr = None

        def cb(*args):
            nonlocal msg, type, attr
            msg = args[0]
            type = args[1]
            attr = args[2]

        cinder.cinder_set_warn_handler(cb)
        try:
            cinder.warn_on_inst_dict(C)

            a = C()
            a.foo = 42
            a.bar = 100

            a = C()
            a.baz = 100

            a = C()
            a.quox = 100
            self.assertEqual(msg, "WARN001: Dictionary created for flagged instance")
            self.assertEqual(type, C)
            self.assertEqual(attr, "quox")
            self.assertEqual(a.quox, 100)
        finally:
            cinder.cinder_set_warn_handler(None)

    def test_cached_property(self):
        class C:
            def __init__(self):
                self.calls = 0

            @cached_property
            def f(self):
                self.calls += 1
                return 42

        a = C()
        self.assertEqual(a.f, 42)
        self.assertEqual(a.calls, 1)

        self.assertEqual(a.f, 42)
        self.assertEqual(a.calls, 1)

    def test_cached_property_subtype(self):
        class ST(cached_property):
            pass

        class C:
            def __init__(self):
                self.calls = 0

            @ST
            def f(self):
                self.calls += 1
                return 42

        a = C()
        self.assertEqual(a.f, 42)
        self.assertEqual(a.calls, 1)

        self.assertEqual(a.f, 42)
        self.assertEqual(a.calls, 1)

    def test_cached_property_loop(self):
        val = object()

        class C:
            @cached_property
            def f(self):
                return val

        a = C()
        for i in range(1000):
            x = a.f
            self.assertEqual(x, val)

    def test_cached_property_raises(self):
        class C:
            @cached_property
            def f(self):
                raise NoWayError()

        with self.assertRaises(NoWayError):
            C().f

    def test_cached_property_raising_set(self):
        class C:
            @cached_property
            def f(self):
                raise NoWayError()

        a = C()
        a.f = 42
        self.assertEqual(a.f, 42)

    def test_cached_property_no_dict(self):
        class C:
            __slots__ = ()

            @cached_property
            def f(self):
                return 42

        with self.assertRaises(AttributeError):
            a = C().f

        with self.assertRaises(AttributeError):
            C().f = 42

    def test_cached_property_name(self):
        class C:
            @cached_property
            def f(self):
                return 42

        self.assertEqual(C.f.name, "f")

    def test_cached_property_func(self):
        class C:
            pass

        def f(self):
            return 42

        C.f = cached_property(f)
        self.assertEqual(C.f.fget, f)

    def test_cached_property_doc(self):
        class C:
            @cached_property
            def f(self):
                return 42

        self.assertEqual(C.f.__doc__, None)

        class D:
            @cached_property
            def f(self):
                "hi there"
                return 42

        self.assertEqual(D.f.__doc__, "hi there")

        D.f.fget.__doc__ = "updated"
        self.assertEqual(D.f.__doc__, "updated")

    def test_cached_property_slot(self):
        class C:
            __slots__ = ("f", "calls")

            def __init__(self):
                self.calls = 0

        def f(self):
            self.calls += 1
            return 42

        C.f = cached_property(f, C.f)
        a = C()
        self.assertEqual(a.f, 42)
        self.assertEqual(a.calls, 1)

        self.assertEqual(a.f, 42)
        self.assertEqual(a.calls, 1)

    def test_cached_property_slot_set_del(self):
        class C:
            __slots__ = ("f", "calls")

            def __init__(self):
                self.calls = 0

        def f(self):
            self.calls += 1
            return 42

        C.f = cached_property(f, C.f)
        a = C()
        a.f = 100
        self.assertEqual(a.f, 100)
        self.assertEqual(a.calls, 0)
        del a.f
        with self.assertRaises(AttributeError):
            del a.f
        self.assertEqual(a.f, 42)
        self.assertEqual(a.calls, 1)

    def test_cached_property_slot_subtype(self):
        class C:
            __slots__ = ("f",)

        def f(self):
            return 42

        class my_cached_prop(cached_property):
            pass

        with self.assertRaises(TypeError):
            C.f = my_cached_prop(f, C.f)

    def test_cached_property_slot_raises(self):
        class C:
            __slots__ = ("f",)

        def f(self):
            raise NoWayError()

        C.f = cached_property(f, C.f)

        with self.assertRaises(NoWayError):
            C().f

    def test_cached_property_slot_wrong_type(self):
        """apply a cached property from one type to another"""

        class C:
            __slots__ = ("abc",)

        class D:
            pass

        D.abc = cached_property(lambda self: 42, C.abc)

        a = D()
        with self.assertRaises(TypeError):
            x = a.abc

    def test_cached_property_slot_wrong_type_set(self):
        """apply a cached property from one type to another"""

        class C:
            __slots__ = ("abc",)

        class D:
            pass

        D.abc = cached_property(lambda self: 42, C.abc)

        a = D()
        with self.assertRaises(TypeError):
            print(a.abc)

    def test_cached_property_slot_name(self):
        class C:
            __slots__ = ("f",)

        C.f = cached_property(lambda self: 42, C.f)

        self.assertEqual(C.f.name, "f")

    def test_cached_property_slot_property(self):
        class C:
            __slots__ = ("f",)

        prev_f = C.f
        C.f = cached_property(lambda self: 42, C.f)
        self.assertEqual(C.f.slot, prev_f)

    def test_cached_property_no_slot_property(self):
        class C:
            @cached_property
            def f(self):
                return 42

        self.assertEqual(C.f.slot, None)

    def test_cached_property_non_descriptor(self):
        with self.assertRaises(TypeError):
            cached_property(lambda self: 42, 42)

    def test_cached_property_incompatible_descriptor(self):
        with self.assertRaises(TypeError):
            cached_property(lambda self: 42, GeneratorType.gi_frame)

    def test_cached_property_readonly_descriptor(self):
        with self.assertRaises(TypeError):
            cached_property(lambda self: 42, range.start)

    def test_warn_on_type(self):
        class C:
            pass

        msg = type = attr = None

        def cb(*args):
            nonlocal msg, type, attr
            msg = args[0]
            type = args[1]
            attr = args[2]

        cinder.warn_on_inst_dict(C)
        cinder.freeze_type(C)

        cinder.cinder_set_warn_handler(cb)
        C.foo = 42

        self.assertEqual(
            msg, "WARN002: Type modified that was flagged for immutability"
        )
        self.assertEqual(type, C)
        self.assertEqual(attr, "foo")

    def test_get_warn(self):
        class C:
            pass

        def cb(*args):
            pass

        cinder.set_warn_handler(cb)
        self.assertEqual(cinder.get_warn_handler(), cb)
        cinder.set_warn_handler(None)
        self.assertEqual(cinder.get_warn_handler(), None)

    def test_warn_on_frozen_type(self):
        class C:
            pass

        cinder.freeze_type(C)

        with self.assertRaisesRegex(
            TypeError, "can't call warn_on_inst_dict on a frozen type"
        ):
            cinder.warn_on_inst_dict(C)

    def test_gen_free_list(self):
        knobs = cinder.getknobs()
        self.assertEqual(knobs["genfreelist"], False)

        cinder.setknobs({"genfreelist": True})
        knobs = cinder.getknobs()
        self.assertEqual(knobs["genfreelist"], True)

        def f():
            yield 42

        a = f()
        id1 = id(a)
        del a
        a = f()

        id2 = id(a)
        self.assertEqual(id1, id2)

        cinder.setknobs({"genfreelist": False})
        knobs = cinder.getknobs()
        self.assertEqual(knobs["genfreelist"], False)

    # def test_polymorphic_cache(self):
    #     knobs = cinder.getknobs()
    #     self.assertEqual(knobs["polymorphiccache"], False)

    #     cinder.setknobs({"polymorphiccache": True})
    #     knobs = cinder.getknobs()
    #     self.assertEqual(knobs["polymorphiccache"], True)

    #     cinder.setknobs({"polymorphiccache": False})
    #     knobs = cinder.getknobs()
    #     self.assertEqual(knobs["polymorphiccache"], False)

    def test_strictmodule_type(self):
        foo = strict_module_from_module(ModuleType("foo"))
        self.assertTrue(type(foo) is StrictModule)

    def test_strictmodule_uninitialized(self):
        # An uninitialized module has no __dict__ or __name__,
        # and __doc__ is None
        foo = StrictModule.__new__(StrictModule)
        self.assertTrue(foo.__dict__ == None)
        self.assertRaises(SystemError, dir, foo)
        try:
            s = foo.__name__
            self.fail("__name__ = %s" % repr(s))
        except AttributeError:
            pass
        self.assertEqual(foo.__doc__, StrictModule.__doc__)

    def test_strictmodule_uninitialized_missing_getattr(self):
        foo = StrictModule.__new__(StrictModule)
        self.assertRaisesRegex(
            AttributeError,
            "module has no attribute 'not_here'",
            getattr,
            foo,
            "not_here",
        )

    def test_strictmodule_missing_getattr(self):
        foo = strict_module_from_module(ModuleType("foo"))
        self.assertRaisesRegex(
            AttributeError,
            "module 'foo' has no attribute 'not_here'",
            getattr,
            foo,
            "not_here",
        )

    def test_strictmodule_no_docstring(self):
        # Regularly initialized module, no docstring
        foo = strict_module_from_module(ModuleType("foo"))
        self.assertEqual(foo.__name__, "foo")
        self.assertEqual(foo.__doc__, None)
        self.assertIs(foo.__loader__, None)
        self.assertIs(foo.__package__, None)
        self.assertIs(foo.__spec__, None)
        self.assertEqual(
            foo.__dict__,
            {
                "__name__": "foo",
                "__doc__": None,
                "__loader__": None,
                "__package__": None,
                "__spec__": None,
            },
        )

    def test_strictmodule_ascii_docstring(self):
        # ASCII docstring
        foo = strict_module_from_module(ModuleType("foo", "foodoc"))
        self.assertEqual(foo.__name__, "foo")
        self.assertEqual(foo.__doc__, "foodoc")
        self.assertEqual(
            foo.__dict__,
            {
                "__name__": "foo",
                "__doc__": "foodoc",
                "__loader__": None,
                "__package__": None,
                "__spec__": None,
            },
        )

    def test_strictmodule_unicode_docstring(self):
        # Unicode docstring
        foo = strict_module_from_module(ModuleType("foo", "foodoc\u1234"))
        self.assertEqual(foo.__name__, "foo")
        self.assertEqual(foo.__doc__, "foodoc\u1234")
        self.assertEqual(
            foo.__dict__,
            {
                "__name__": "foo",
                "__doc__": "foodoc\u1234",
                "__loader__": None,
                "__package__": None,
                "__spec__": None,
            },
        )

    def test_strictmodule_weakref(self):
        m = strict_module_from_module(ModuleType("foo"))
        wr = weakref.ref(m)
        self.assertIs(wr(), m)
        del m
        gc_collect()
        self.assertIs(wr(), None)

    def test_strictmodule_getattr(self):
        foo = create_strict_module(x=1)
        self.assertEqual(foo.x, 1)

    def test_strictmodule_setattr(self):
        foo = create_strict_module(x=1)
        with self.assertRaises(AttributeError):
            foo.x = 2

    def test_strictmodule_delattr(self):
        foo = create_strict_module(x=1)
        with self.assertRaises(AttributeError):
            del foo.x

    def test_strictmodule_setattr_with_patch_enabled(self):
        foo = create_strict_module(x=1, enable_patching=True)
        with self.assertRaises(AttributeError):
            foo.x = 2

    def test_strictmodule_patch_disabled(self):
        foo = create_strict_module(x=1)
        with self.assertRaises(AttributeError):
            strict_module_patch(foo, "x", 2)

    def test_strictmodule_patch_enabled(self):
        foo = create_strict_module(x=1, enable_patching=True)
        strict_module_patch(foo, "x", 2)
        self.assertEqual(foo.x, 2)

    def test_strictmodule_patch_enabled(self):
        foo = strict_module_from_module(ModuleType("a"), enable_patching=True)
        strict_module_patch(foo, "__dir__", 2)
        self.assertEqual(foo.__dir__, 2)

    def test_strictmodule_patch_enabled_2(self):
        m = ModuleType("a")
        d = m.__dict__
        foo = StrictModule(m.__dict__, False)
        d["__dir__"] = 2
        self.assertEqual(foo.__dir__, 2)

    def test_strictmodule_getattr_errors(self):
        import test.bad_getattr as bga
        from test import bad_getattr2

        bga = strict_module_from_module(bga)
        bad_getattr2 = strict_module_from_module(bad_getattr2)
        self.assertEqual(bga.x, 1)
        self.assertEqual(bad_getattr2.x, 1)
        # we are not respecting module __getattr__ here
        with self.assertRaises(TypeError):
            bga.nope
        with self.assertRaises(TypeError):
            bad_getattr2.nope
        del sys.modules["test.bad_getattr"]
        if "test.bad_getattr2" in sys.modules:
            del sys.modules["test.bad_getattr2"]

    def test_strictmodule_dir(self):
        import test.good_getattr as gga

        gga = strict_module_from_module(gga)
        self.assertEqual(dir(gga), ["a", "b", "c"])
        del sys.modules["test.good_getattr"]

    def test_strictmodule_dir_errors(self):
        import test.bad_getattr as bga
        from test import bad_getattr2

        bga = strict_module_from_module(bga)
        bad_getattr2 = strict_module_from_module(bad_getattr2)
        with self.assertRaises(TypeError):
            dir(bga)
        with self.assertRaises(TypeError):
            dir(bad_getattr2)
        del sys.modules["test.bad_getattr"]
        if "test.bad_getattr2" in sys.modules:
            del sys.modules["test.bad_getattr2"]

    def test_strictmodule_getattr_tricky(self):
        from test import bad_getattr3

        bad_getattr3 = strict_module_from_module(bad_getattr3)
        # these lookups should not crash
        with self.assertRaises(AttributeError):
            bad_getattr3.one
        with self.assertRaises(AttributeError):
            bad_getattr3.delgetattr
        if "test.bad_getattr3" in sys.modules:
            del sys.modules["test.bad_getattr3"]

    def test_strictmodule_repr_minimal(self):
        # reprs when modules have no __file__, __name__, or __loader__
        m = ModuleType("foo")
        del m.__name__
        m = strict_module_from_module(m)
        self.assertEqual(repr(m), "<module '?'>")

    def test_strictmodule_repr_with_name(self):
        m = ModuleType("foo")
        m = strict_module_from_module(m)
        self.assertEqual(repr(m), "<module 'foo'>")

    def test_strictmodule_repr_with_name_and_filename(self):
        m = ModuleType("foo")
        m.__file__ = "/tmp/foo.py"
        m = strict_module_from_module(m)
        self.assertEqual(repr(m), "<module 'foo' from '/tmp/foo.py'>")

    def test_strictmodule_repr_with_filename_only(self):
        m = ModuleType("foo")
        del m.__name__
        m.__file__ = "/tmp/foo.py"
        m = strict_module_from_module(m)
        self.assertEqual(repr(m), "<module '?' from '/tmp/foo.py'>")

    def test_strictmodule_modify_dict_patch_disabled(self):
        foo = create_strict_module(x=1, enable_patching=False)
        foo.__dict__["x"] = 2
        self.assertEqual(foo.x, 1)

    def test_strictmodule_modify_dict_patch_enabled(self):
        foo = create_strict_module(x=1, enable_patching=True)
        foo.__dict__["x"] = 2
        self.assertEqual(foo.x, 1)

    def test_strictmodule_unassigned_field(self):
        d = {"<assigned:x>": False, "x": 1}
        foo = StrictModule(d, False)
        self.assertNotIn("x", foo.__dict__)

    def test_const_object(self):
        class MyObj:
            magic = [42]

        a = MyObj
        b = const(a)
        c = [b]
        self.assertEqual(c[0], b)
        self.assertEqual(b.magic, const([42]))

        d = c[0]
        e = d.magic
        with self.assertRaises(AttributeError):
            e.magic = 33


def async_test(f):
    assert inspect.iscoroutinefunction(f)

    @wraps(f)
    def impl(*args, **kwargs):
        asyncio.run(f(*args, **kwargs))

    return impl


class AsyncCinderTest(unittest.TestCase):
    def setUp(self) -> None:
        loop = asyncio.new_event_loop()
        asyncio.set_event_loop(loop)
        self.loop = loop

    def tearDown(self):
        self.loop.close()
        asyncio.set_event_loop_policy(None)

    @async_test
    async def test_cached_property(self):
        class C:
            def __init__(self):
                self.calls = 0

            @async_cached_property
            async def f(self):
                self.calls += 1
                return 42

        a = C()
        self.assertEqual(await a.f, 42)
        self.assertEqual(a.calls, 1)

        self.assertEqual(await a.f, 42)
        self.assertEqual(a.calls, 1)

    @async_test
    async def test_cached_property_loop(self):
        val = object()

        class C:
            @async_cached_property
            async def f(self):
                return val

        a = C()
        for i in range(1000):
            x = await a.f
            self.assertEqual(x, val)

    @async_test
    async def test_cached_property_raises(self):
        class C:
            @async_cached_property
            async def f(self):
                raise NoWayError()

        with self.assertRaises(NoWayError):
            await C().f

    @async_test
    async def test_cached_property_no_dict(self):
        class C:
            __slots__ = ()

            @async_cached_property
            async def f(self):
                return 42

        with self.assertRaises(AttributeError):
            a = await C().f

    @async_test
    async def test_cached_property_name(self):
        class C:
            @async_cached_property
            async def f(self):
                return 42

        self.assertEqual(C.f.name, "f")

    @async_test
    async def test_cached_property_func(self):
        class C:
            pass

        async def f(self):
            return 42

        C.f = async_cached_property(f)
        self.assertEqual(C.f.func, f)

    @async_test
    async def test_cached_property_doc(self):
        class C:
            @async_cached_property
            async def f(self):
                return 42

        self.assertEqual(C.f.__doc__, None)

        class D:
            @async_cached_property
            async def f(self):
                "hi there"
                return 42

        self.assertEqual(D.f.__doc__, "hi there")

        D.f.func.__doc__ = "updated"
        self.assertEqual(D.f.__doc__, "updated")

    @async_test
    async def test_cached_property_slot(self):
        class C:
            __slots__ = ("f", "calls")

            def __init__(self):
                self.calls = 0

        async def f(self):
            self.calls += 1
            return 42

        C.f = async_cached_property(f, C.f)
        a = C()
        self.assertEqual(await a.f, 42)
        self.assertEqual(a.calls, 1)

        self.assertEqual(await a.f, 42)
        self.assertEqual(a.calls, 1)

    @async_test
    async def test_cached_property_slot_raises(self):
        class C:
            __slots__ = ("f",)

        async def f(self):
            raise NoWayError()

        C.f = async_cached_property(f, C.f)

        with self.assertRaises(NoWayError):
            await C().f

    @async_test
    async def test_cached_property_slot_wrong_type(self):
        """apply a cached property from one type to another"""

        class C:
            __slots__ = ("abc",)

        class D:
            pass

        async def f(self):
            return 42

        D.abc = async_cached_property(f, C.abc)

        a = D()
        with self.assertRaises(TypeError):
            x = await a.abc

    @async_test
    async def test_cached_property_slot_name(self):
        class C:
            __slots__ = ("f",)

        async def f(self):
            return 42

        C.f = async_cached_property(f, C.f)

        self.assertEqual(C.f.name, "f")

    @async_test
    async def test_cached_property_slot_property(self):
        class C:
            __slots__ = ("f",)

        async def f(self):
            return 42

        prev_f = C.f
        C.f = async_cached_property(f, C.f)
        self.assertEqual(C.f.slot, prev_f)

    @async_test
    async def test_cached_property_no_slot_property(self):
        class C:
            @async_cached_property
            async def f(self):
                return 42

        self.assertEqual(C.f.slot, None)

    @async_test
    async def test_cached_property_non_descriptor(self):
        async def f(self):
            return 42

        with self.assertRaises(TypeError):
            async_cached_property(f, 42)

    @async_test
    async def test_cached_property_incompatible_descriptor(self):
        async def f(self):
            return 42

        with self.assertRaises(TypeError):
            async_cached_property(f, GeneratorType.gi_frame)

    @async_test
    async def test_cached_property_readonly_descriptor(self):
        async def f(self):
            return 42

        with self.assertRaises(TypeError):
            async_cached_property(f, range.start)

    @async_test
    async def test_cached_class_prop(self):
        class C:
            @async_cached_classproperty
            async def f(self):
                return 42

        self.assertEqual(await C.f, 42)

    @async_test
    async def test_cached_class_prop_called_once(self):
        class C:
            calls = 0

            @async_cached_classproperty
            async def f(cls):
                cls.calls += 1
                return 42

        self.assertEqual(await C.f, 42)
        self.assertEqual(await C.f, 42)
        self.assertEqual(C.calls, 1)

    @async_test
    async def test_cached_class_prop_descr(self):
        """verifies the descriptor protocol isn't invoked on the cached value"""

        class classproperty:
            def __get__(self, inst, ctx):
                return 42

        clsprop = classproperty()

        class C:
            @async_cached_classproperty
            async def f(cls):
                return clsprop

        self.assertEqual(await C.f, clsprop)
        self.assertEqual(await C.f, clsprop)

    @async_test
    async def test_cached_class_prop_inheritance(self):
        class C:
            @async_cached_classproperty
            async def f(cls):
                return cls.__name__

        class D(C):
            pass

        self.assertEqual(await C.f, "C")
        self.assertEqual(await D.f, "C")

    @async_test
    async def test_cached_class_prop_inheritance_reversed(self):
        class C:
            @async_cached_classproperty
            async def f(cls):
                return cls.__name__

        class D(C):
            pass

        self.assertEqual(await D.f, "D")
        self.assertEqual(await C.f, "D")

    @async_test
    async def test_cached_class_prop_inst_method_no_inst(self):
        class C:
            def __init__(self, value):
                self.value = value

            @async_cached_classproperty
            async def f(cls):
                return lambda self: self.value

        self.assertEqual(type(await C.f), FunctionType)

    @async_test
    async def test_cached_class_prop_inst(self):
        class C:
            @async_cached_classproperty
            async def f(cls):
                return 42

        self.assertEqual(await C().f, 42)

    @async_test
    async def test_cached_class_prop_frozen_type(self):
        class C:
            @async_cached_classproperty
            async def f(cls):
                return 42

        cinder.freeze_type(C)
        self.assertEqual(await C.f, 42)

    @async_test
    async def test_cached_class_prop_frozen_type_inst(self):
        class C:
            @async_cached_classproperty
            async def f(cls):
                return 42

        cinder.freeze_type(C)
        self.assertEqual(await C().f, 42)

    @async_test
    async def test_cached_class_prop_setattr_fails(self):
        class metatype(type):
            def __setattr__(self, name, value):
                if name == "f":
                    raise NoWayError()

        class C(metaclass=metatype):
            @async_cached_classproperty
            async def f(self):
                return 42

        self.assertEqual(await C.f, 42)

    @async_test
    async def test_cached_class_prop_doc(self):
        class C:
            @async_cached_classproperty
            async def f(cls):
                "hi"
                return 42

        self.assertEqual(C.__dict__["f"].__doc__, "hi")
        self.assertEqual(C.__dict__["f"].name, "f")
        self.assertEqual(type(C.__dict__["f"].func), FunctionType)

    @async_test
    async def test_cached_property_awaiter(self):
        class C:
            def __init__(self, coro):
                self.coro = coro

            @async_cached_property
            async def f(self):
                return await self.coro

        coro = None
        await_stack = None

        async def g():
            nonlocal coro, await_stack
            # Force suspension. Otherwise the entire execution is eager and
            # awaiter is never set.
            await asyncio.sleep(0)
            await_stack = get_await_stack(coro)
            return 100

        async def h(c):
            return await c.f

        coro = g()
        h_coro = h(C(coro))
        res = await h_coro
        self.assertEqual(res, 100)
        # awaiter of g is the coroutine running C.f. That's created by the
        # AsyncLazyValue machinery, so we can't check the awaiter's identity
        # directly, only that it corresponds to C.f.
        self.assertIs(await_stack[0].cr_code, C.f.func.__code__)
        self.assertIs(await_stack[1], h_coro)

    @async_test
    async def test_cached_property_gathered_awaiter(self):
        class C:
            def __init__(self, coro):
                self.coro = coro

            @async_cached_property
            async def f(self):
                return await self.coro

        coros = [None, None]
        await_stacks = [None, None]

        async def g(res, idx):
            nonlocal coros, await_stacks
            # Force suspension. Otherwise the entire execution is eager and
            # awaiter is never set.
            await asyncio.sleep(0)
            await_stacks[idx] = get_await_stack(coros[idx])
            return res

        async def gatherer(c0, c1):
            return await asyncio.gather(c0.f, c1.f)

        coros[0] = g(10, 0)
        coros[1] = g(20, 1)
        gatherer_coro = gatherer(C(coros[0]), C(coros[1]))
        results = await gatherer_coro
        self.assertEqual(results[0], 10)
        self.assertEqual(results[1], 20)
        # awaiter of g is the coroutine running C.f. That's created by the
        # AsyncLazyValue machinery, so we can't check the awaiter's identity
        # directly, only that it corresponds to C.f.
        self.assertIs(await_stacks[0][0].cr_code, C.f.func.__code__)
        self.assertIs(await_stacks[0][1], gatherer_coro)
        self.assertIs(await_stacks[1][0].cr_code, C.f.func.__code__)
        self.assertIs(await_stacks[1][1], gatherer_coro)


def f():
    pass


class C:
    def x(self):
        pass

    @staticmethod
    def sm():
        pass

    @classmethod
    def cm():
        pass

    def f(self):
        class G:
            def y(self):
                pass

        return G.y


class CodeObjectQualnameTest(unittest.TestCase):
    def test_qualnames(self):
        self.assertEqual(cinder._get_qualname(f.__code__), "f")

        self.assertEqual(cinder._get_qualname(C.x.__code__), "C.x")
        self.assertEqual(cinder._get_qualname(C.sm.__code__), "C.sm")
        self.assertEqual(cinder._get_qualname(C.cm.__code__), "C.cm")

        self.assertEqual(cinder._get_qualname(C().f().__code__), "C.f.<locals>.G.y")

        c = f.__code__
        co = CodeType(
            c.co_argcount,
            c.co_posonlyargcount,
            c.co_kwonlyargcount,
            c.co_nlocals,
            c.co_stacksize,
            c.co_flags,
            c.co_code,
            c.co_consts,
            c.co_names,
            c.co_varnames,
            c.co_filename,
            c.co_name,
            c.co_firstlineno,
            c.co_lnotab,
            c.co_freevars,
            c.co_cellvars,
        )
        self.assertIsNone(cinder._get_qualname(co))

        co = c.replace(co_flags=c.co_flags)
        self.assertEqual(cinder._get_qualname(co), "f")

        src = """\
        import sys
        import cinder
        modname = cinder._get_qualname(sys._getframe(0).f_code)
        clsname = None
        class C:
            global clsname
            clsname = cinder._get_qualname(sys._getframe(0).f_code)
        """
        g = {}
        exec(dedent(src), g)
        self.assertEqual(g["modname"], "<module>")
        self.assertEqual(g["clsname"], "C")


class TestNoShadowingInstances(unittest.TestCase):
    def check_no_shadowing(self, typ, expected):
        got = cinder._has_no_shadowing_instances(typ)
        self.assertEqual(got, expected)

    def test_dict_retrieved(self):
        class Foo:
            def test(self):
                return 1234

        obj = Foo()
        self.check_no_shadowing(Foo, True)
        obj.__dict__
        self.check_no_shadowing(Foo, False)

    def test_dict_set(self):
        class Foo:
            def test(self):
                return 1234

        obj = Foo()
        self.check_no_shadowing(Foo, True)
        obj.__dict__ = {"testing": "123"}
        self.check_no_shadowing(Foo, False)

    def test_shadowing_method(self):
        class Foo:
            def test(self):
                return 1234

        obj = Foo()
        self.check_no_shadowing(Foo, True)
        obj.test = 1234
        self.check_no_shadowing(Foo, False)

    def test_shadowing_classvar(self):
        class Foo:
            test = 1234

        obj = Foo()
        self.check_no_shadowing(Foo, True)
        obj.test = 1234
        self.check_no_shadowing(Foo, True)

    def test_method_added_on_class(self):
        class Foo:
            pass

        self.check_no_shadowing(Foo, True)

        def test(self):
            return 1234

        Foo.test = test
        self.check_no_shadowing(Foo, False)

    def test_method_added_on_base(self):
        class Foo:
            pass

        class Bar(Foo):
            pass

        class Baz(Bar):
            pass

        self.check_no_shadowing(Foo, True)
        self.check_no_shadowing(Bar, True)
        self.check_no_shadowing(Baz, True)

        def test(self):
            return 1234

        Foo.test = test
        self.check_no_shadowing(Foo, False)
        self.check_no_shadowing(Bar, False)
        self.check_no_shadowing(Baz, False)

    def test_custom_metaclass(self):
        class MyMeta(type):
            pass

        class Foo(metaclass=MyMeta):
            pass

        self.check_no_shadowing(Foo, True)

    def test_custom_metaclass_with_setattr(self):
        class MyMeta(type):
            def __setattr__(cls, name, value):
                return super().__setattr__(name, value)

        class Foo(metaclass=MyMeta):
            pass

        self.check_no_shadowing(Foo, True)

        Foo.notamethod = 1
        self.check_no_shadowing(Foo, True)

        def amethod(self):
            return 1234

        Foo.amethod = amethod
        self.check_no_shadowing(Foo, False)

    def test_init_subclass(self):
        class Base:
            def amethod(self):
                return 1234

            def __init_subclass__(cls, /, **kwargs):
                cls.new_meth = Base.amethod

        class Derived(Base):
            pass

        self.check_no_shadowing(Derived, True)

    def test_init_subclass_that_creates_instance(self):
        import sys

        outer = None

        class Base:
            def amethod(self):
                return 1234

            def __init_subclass__(cls, /, **kwargs):
                nonlocal outer
                cls.new_meth = Base.amethod
                outer = cls()

        class Derived(Base):
            pass

        self.check_no_shadowing(Derived, False)


class GetCallStackTest(unittest.TestCase):
    def a(self):
        return self.b()

    def b(self):
        return self.c()

    def c(self):
        return self.d()

    def d(self):
        return cinder._get_call_stack()

    def test_get_call_stack(self):
        stack = self.a()
        self.assertGreater(len(stack), 5)
        expected = [
            self.test_get_call_stack.__code__,
            self.a.__code__,
            self.b.__code__,
            self.c.__code__,
            self.d.__code__,
        ]
        self.assertEqual(stack[-5:], expected)


@unittest.skipUnderCinderJIT("Profiling only works under interpreter")
class TestInterpProfiling(unittest.TestCase):
    def tearDown(self):
        cinder.set_profile_interp(False)

    def test_profiles_instrs(self):
        def workload(a, b, c):
            r = 0.0
            for i in range(c):
                r += a * b

        was_enabled_before = cinder.set_profile_interp(True)
        repetitions = 101
        result = workload(1, 2, repetitions)
        was_enabled_after = cinder.set_profile_interp(False)
        profiles = cinder.get_and_clear_type_profiles()

        self.assertFalse(was_enabled_before)
        self.assertTrue(was_enabled_after)

        profile_by_op = {}
        for item in profiles:
            if (
                item["normal"]["func_qualname"].endswith("<locals>.workload")
                and "opname" in item["normal"]
            ):
                opname = item["normal"]["opname"]
                self.assertNotIn(opname, profile_by_op)
                profile_by_op[opname] = item

        # We don't want to overfit to the current shape of the bytecode, so do
        # a quick sanity check of a few key instructions.
        self.assertIn("FOR_ITER", profile_by_op)
        item = profile_by_op["FOR_ITER"]
        self.assertEqual(item["int"]["count"], repetitions + 1)
        self.assertEqual(item["normvector"]["types"], ["range_iterator"])

        self.assertIn("BINARY_MULTIPLY", profile_by_op)
        item = profile_by_op["BINARY_MULTIPLY"]
        self.assertEqual(item["int"]["count"], repetitions)
        self.assertEqual(item["normvector"]["types"], ["int", "int"])

        self.assertIn("INPLACE_ADD", profile_by_op)
        item = profile_by_op["INPLACE_ADD"]
        self.assertEqual(item["int"]["count"], repetitions)
        self.assertEqual(item["normvector"]["types"], ["float", "int"])


class TestWaitForAwaiter(unittest.TestCase):
    def setUp(self) -> None:
        loop = asyncio.new_event_loop()
        asyncio.set_event_loop(loop)
        self.loop = loop

    def tearDown(self):
        self.loop.close()
        asyncio.set_event_loop_policy(None)

    @async_test
    async def test_get_awaiter_wait_for(self):
        coro = None

        async def sleeper():
            nonlocal coro
            # Force suspension
            await asyncio.sleep(0.1)
            return get_await_stack(coro)

        async def waiter(c):
            return await asyncio.tasks.wait_for(c, 10)

        coro = sleeper()
        await_stack = await waiter(coro)
        self.assertIs(
            await_stack[0].cr_code, asyncio.tasks._wait_until_complete.__code__
        )
        self.assertIs(await_stack[1].cr_code, asyncio.tasks.wait_for.__code__)
        self.assertIs(await_stack[2].cr_code, waiter.__code__)

    @async_test
    async def test_get_awaiter_wait_for_gather(self):
        coros = [None, None]

        async def sleeper(idx):
            nonlocal coros
            # Force suspension
            await asyncio.sleep(0.1)
            return get_await_stack(coros[idx])

        async def waiter(c0, c1):
            return await asyncio.tasks.wait_for(asyncio.gather(c0, c1), 10)

        coros[0] = sleeper(0)
        coros[1] = sleeper(1)
        await_stacks = await waiter(coros[0], coros[1])
        self.assertIs(
            await_stacks[0][0].cr_code, asyncio.tasks._wait_until_complete.__code__
        )
        self.assertIs(await_stacks[0][1].cr_code, asyncio.tasks.wait_for.__code__)
        self.assertIs(await_stacks[0][2].cr_code, waiter.__code__)
        self.assertIs(
            await_stacks[1][0].cr_code, asyncio.tasks._wait_until_complete.__code__
        )
        self.assertIs(await_stacks[1][1].cr_code, asyncio.tasks.wait_for.__code__)
        self.assertIs(await_stacks[1][2].cr_code, waiter.__code__)


if __name__ == "__main__":
    unittest.main()
