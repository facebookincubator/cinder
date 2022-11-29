#!/usr/bin/python3
# Copyright (c) Meta Platforms, Inc. and affiliates. (http://www.meta.com)
# facebook begin t39538061

import builtins
import gc
import sys
import unittest
import weakref
from collections import UserDict
from test.support.script_helper import assert_python_ok, run_python_until_end
from unittest import skipIf
from unittest.case import CINDERJIT_ENABLED
import cinder
from cinder import (
    cached_property,
    StrictModule,
    strict_module_patch,
)

# Sets the number of repetitions required in order to hit caching
REPETITION = 100


class ShadowError(Exception):
    pass


knobs = cinder.getknobs()
if "shadowcode" in knobs:
    cinder.setknobs({"shadowcode":True})
    cinder.setknobs({"polymorphiccache":True})


# Tests with immortalizaiton will cause expected leaks
# This function skips return code check for ASAN build
def skip_ret_code_check_for_leaking_test_in_asan_mode(*args, **env_vars):
    if cinder._built_with_asan:
        res, _ = run_python_until_end(*args, **env_vars)
        return res
    else:
        return assert_python_ok(*args, **env_vars)


class ShadowCodeTests(unittest.TestCase):
    def test_type_error(self):
        class Desc:
            def __init__(self):
                self.error = False

            def __get__(self, inst, ctx):
                if self.error:
                    raise ShadowError()
                return 42

        desc = Desc()

        class C:
            prop = desc

        def f(x):
            return x.prop

        for _ in range(REPETITION):
            self.assertEqual(f(C), 42)
        desc.error = True
        for _ in range(REPETITION):
            self.assertRaises(ShadowError, f, C)

    def test_module_error(self):
        sys.prop = 42

        def f(x):
            return x.prop

        for _ in range(REPETITION):
            self.assertEqual(f(sys), 42)
        del sys.prop
        for _ in range(REPETITION):
            self.assertRaises(AttributeError, f, sys)

    def test_load_attr_no_dict_descr_error(self):
        class Desc:
            def __init__(self):
                self.error = False

            def __get__(self, inst, ctx):
                if self.error:
                    raise ShadowError()
                return 42

        desc = Desc()

        class C:
            __slots__ = ()
            prop = desc

        def f(x):
            return x.prop

        a = C()
        for _ in range(REPETITION):
            self.assertEqual(f(a), 42)
        desc.error = True
        for _ in range(REPETITION):
            self.assertRaises(ShadowError, f, a)

    def test_load_attr_dict_descr_error(self):
        class Desc:
            def __init__(self):
                self.error = False

            def __get__(self, inst, ctx):
                if self.error:
                    raise ShadowError()
                return 42
        desc = Desc()

        class C:
            prop = desc

        def f(x):
            return x.prop

        a = C()
        a.foo = 100
        a.bar = 200
        b = C()
        b.quox = 100
        c = C()
        c.blah = 300
        for _ in range(REPETITION):
            self.assertEqual(f(c), 42)
        desc.error = True
        for _ in range(REPETITION):
            self.assertRaises(ShadowError, f, c)

    def test_load_attr_dict_no_item(self):
        class C:
            pass

        def f(x):
            return x.prop

        a = C()
        a.foo = 100
        a.bar = 200
        b = C()
        b.quox = 100
        c = C()
        c.prop = 42
        for _ in range(REPETITION):
            self.assertEqual(f(c), 42)
        for _ in range(REPETITION):
            self.assertRaises(AttributeError, f, b)

    def test_split_dict_append(self):
        '''add a property to a split dictionary that aliases is a descriptor
property after we've already cached the non-existance of the split property'''
        class C:
            def __init__(self):
                self.x = 1
            prop = 42

        def f(x):
            return x.prop
        a = C()

        for _ in range(REPETITION):
            self.assertEqual(f(a), 42)

        a.prop = 100

        for _ in range(REPETITION):
            self.assertEqual(f(a), 100)

    def test_class_overflow(self):
        def make_class():
            class C:
                def __init__(self):
                    self.prop = 1
            return C

        def f(x):
            return x.prop
        a = make_class()()
        for _ in range(REPETITION):
            self.assertEqual(f(a), 1)
        for _ in range(300):
            a = make_class()()
            self.assertEqual(f(a), 1)

    def test_dict(self):
        class C:
            pass

        a = C()
        a.foo = 1
        a.bar = 2
        b = C()
        b.bar = 1
        b.baz = 2

        def f(x):
            return x.bar

        for _i in range(REPETITION):
            self.assertEqual(f(b), 1)

        C.bar = property(lambda self: 42)
        for _i in range(REPETITION):
            self.assertEqual(f(b), 42)

    def test_split_dict_no_descr(self):
        class C:
            def __init__(self):
                self.foo = 1
                self.bar = 2
                self.baz = 3
                self.quox = 3
                self.foo1 = 1
                self.bar2 = 2
                self.baz3 = 3
                self.quox4 = 3
        a = C()
        b = C()

        def f(x):
            return x.foo

        for _i in range(REPETITION):
            self.assertEqual(f(a), 1)
        a.foo = 2
        for _i in range(REPETITION):
            self.assertEqual(f(a), 2)
        for _i in range(REPETITION):
            self.assertEqual(f(b), 1)

        # split_dict -> no_dict_descr
        C.foo = property(lambda self: 100)

        for _i in range(REPETITION):
            self.assertEqual(f(b), 100)

        # no_dict_descr -> split_dict_descr
        C.foo = 100
        for _i in range(REPETITION):
            self.assertEqual(f(b), 1)

    def test_split_dict_descr(self):
        class C:
            foo = 100

            def __init__(self, foo=True):
                self.bar = 2
                self.baz = 3
                self.quox = 3
                self.foo1 = 1
                self.bar2 = 2
                self.baz3 = 3
                self.quox4 = 3
                if foo:
                    self.foo = 1

        a = C()
        b = C(False)

        def f(x):
            return x.foo

        for _i in range(REPETITION):
            self.assertEqual(f(a), 1)

        for _i in range(REPETITION):
            self.assertEqual(f(b), 100)

        # split_dict -> no_dict
        C.foo = property(lambda self: 100)
        for _i in range(REPETITION):
            self.assertEqual(f(b), 100)

    def test_module(self):
        version = sys.version

        def f():
            return sys.version

        for _i in range(REPETITION):
            self.assertEqual(f(), version)
        sys.version = '2.8'
        try:
            for _i in range(REPETITION):
                self.assertEqual(f(), '2.8')
        finally:
            sys.version = version

    def test_type_attr_metaattr(self):
        class MC(type):
            x = 100

        class C(metaclass=MC):
            x = 42

        def f(x):
            return x.x

        for _i in range(REPETITION):
            self.assertEqual(f(C), 42)

    def test_type_attr_no_double_invoke(self):
        '''verify that a descriptor only gets invoked once when it raises'''
        class Desc:
            def __init__(self):
                self.error = False
                self.calls = 0

            def __get__(self, inst, ctx):
                self.calls += 1
                if self.error:
                    raise ShadowError()
                return 42

        desc = Desc()
        class C:
            prop = desc

        def f(x):
            return x.prop

        for _i in range(REPETITION):
            self.assertEqual(f(C), 42)
        self.assertEqual(desc.calls, REPETITION)
        desc.error = True
        self.assertRaises(ShadowError, f, C)
        self.assertEqual(desc.calls, REPETITION + 1)

    def test_no_dict_descr_builtin(self):
        x = 42

        def f(x):
            return x.real

        for _i in range(REPETITION):
            self.assertEqual(f(x), 42)

    def test_no_dict_descr_user(self):
        class C:
            __slots__ = ()

            @property
            def abc(self):
                return 42

        x = C()

        def f(x):
            return x.abc

        for _i in range(REPETITION):
            self.assertEqual(f(x), 42)
        C.abc = property(lambda self: 100)

        for _i in range(REPETITION):
            self.assertEqual(f(x), 100)

    def test_no_dict(self):
        class C:
            __slots__ = ()
            abc = 42

        x = C()

        def f(x):
            return x.abc

        for _i in range(REPETITION):
            self.assertEqual(f(x), 42)
        C.abc = 100
        for _i in range(REPETITION):
            self.assertEqual(f(x), 100)

    def test_dict_descr(self):
        '''shadowing a class member should give the instance'''
        class C:
            def x(self):
                return 1

            def __init__(self):
                self.x = 1

        a = C()

        def f(x):
            self.assertEqual(x.x, 1)

        for _i in range(REPETITION):
            f(a)

    def test_dict_descr_2(self):
        '''getting a descriptor should return a new instance'''
        class C:
            def x(self):
                return 1

        a = C()

        def f(x):
            return x.x

        items = []
        for _i in range(REPETITION):
            items.append(f(a))

        self.assertEqual(len({id(item) for item in items}), REPETITION)

    def test_dict_descr_3(self):
        class C:
            def __init__(self, order):
                if order:
                        self.x = 1
                        self.y = 2
                else:
                        self.y = 1
                        self.x = 2

            def z(self):
                return 42

        a = C(0)
        a = C(1)

        def f(x):
            self.assertEqual(a.z(), 42)

        for _ in range(100):
            f(a)

    def test_type_attr(self):
        class C:
            x = 1

        def f(x, expected):
            self.assertEqual(x.x, expected)

        for _ in range(REPETITION):
            f(C, 1)
        C.x = 2
        for _ in range(REPETITION):
            f(C, 2)

    def test_instance_attr(self):
        '''LOAD_ATTR_DICT_DESCR -> LOAD_ATTR_NO_DICT
We generate a cached opcode that handles a dict, then transition over to one
that doesn't need a dict lookup'''
        class C:
            def f(self):
                return 42

        a = C()

        def f(x):
            return x.f

        def g(x):
            return x.f

        for _ in range(REPETITION):
            self.assertEqual(f(a)(), 42)
        C.f = property(lambda x: 100)
        for _ in range(REPETITION):
            self.assertEqual(g(a), 100)
        f(a)

    def test_megamorphic(self):
        class C:
            x = 0

        def f(x): return x.x
        a = C()
        for i in range(REPETITION):
            self.assertEqual(f(a), i)
            C.x += 1

    def test_modify_class(self):
        for i in range(REPETITION):
            class lazy_classproperty(object):
                def __init__(self, fget):
                    self._fget = fget
                    self.__doc__ = fget.__doc__
                    self.__name__ = fget.__name__
                    self.count = 0

                def __get__(self, obj, obj_cls_type):
                    value = self._fget(obj_cls_type)
                    self.count += 1
                    if self.count == i:
                        setattr(obj_cls_type, self.__name__, value)

                    return value

            class C:
                @lazy_classproperty
                def f(cls):
                    return 42

            a = C()
            exec('''
def f(x):
    z = x.f
    if z != 42: self.fail('err')
        ''', locals(), globals())
            for _ in range(REPETITION*2):
                f(C)  # noqa

    def test_extended_arg(self):
        '''tests patching an opcode with EXTENDED_ARG and inserting a nop in
place of the extended arg opcode'''
        class C:
            def __init__(self):
                self.ext = 0
                for i in range(256):
                    setattr(self, 'x' + hex(i)[2:], i)

        f = self.make_large_func(args='x, false = False', add='x.x{}',
                                 size=256, skip=8)

        a = C()

        for _ in range(REPETITION):
            self.assertEqual(f(a), 32612)

    def test_cache_global_reuse(self):
        def f(a, b):
            return min(a, b) + min(a, b) + min(a, b)

        for i in range(REPETITION):
            self.assertEqual(f(i, i + 1), i*3)

    def make_large_func(self, globals=None, args='', add='x{}', start=0,
                        size=300, skip=None):
        code = 'def f(' + args + '):\n    res = 0\n'
        if skip:
            code += '    if false:\n'
        for i in range(start, size):
            indent = '    '
            if skip and i < skip:
                indent += '    '
            code += indent + 'res += ' + add.format(hex(i)[2:]) + '\n'

        code += '    return res'
        locals = {}
        exec(code, globals if globals is not None else {}, locals)
        return locals['f']

    def test_global_cache_exhausted(self):
        globals = {}
        for i in range(300):
            globals['x' + hex(i)[2:]] = i
        f = self.make_large_func(globals)
        for _ in range(REPETITION):
            self.assertEqual(f(), 44850)

    def test_global_invalidate_builtins(self):
        global X
        X = 1
        def f():
            return X
        for i in range(REPETITION):
            self.assertEqual(f(), 1)
        try:
            builtins.__dict__[42] = 42
        finally:
            del builtins.__dict__[42]

    def test_reoptimize_no_caches(self):
        """we limit caches to 256 per method.  If we take a EXTENDED_ARG cache, optimize it,
        and then don't have any other spaces for caches, we fail to replace the cache.  We
        should maintain the ref count on the previous cache correctly."""
        COUNT = 400
        TOTAL = 79800
        klass = ("""
class C:
    def __init__(self, flag=False):
        if flag:
            self.foo = 42
""" +
            '\n'.join(f'        self.x{i} = {i}' for i in range(COUNT)))
        d = {}
        exec(klass, globals(), d)

        accesses = "\n".join(f"        if min < {i} < max: res += inst.x{i}" for i in range(COUNT))
        func = f"""
def f(min, max, inst, path=False):
    res = 0
    if path:
{accesses}
    else:
{accesses}
    return res
"""
        exec(func, globals(), d)
        C = d["C"]
        a = C()
        f = d["f"]
        for i in range(REPETITION):
            self.assertEqual(f(260, 270, a), 2385)
            self.assertEqual(f(260, 270, a, True), 2385)

        self.assertEqual(f(0, COUNT, a), TOTAL)
        self.assertEqual(f(0, COUNT, a, True), TOTAL)

        a = C(True)
        for i in range(REPETITION):
            self.assertEqual(f(260, 262, a), 261)
        self.assertEqual(f(0, COUNT, a), TOTAL)

    def test_cache_exhausted(self):
        '''tests running out of cache instances'''
        class C:
            def __init__(self):
                self.ext = 0
                for i in range(256):
                    setattr(self, 'x' + hex(i)[2:], i)

        f = self.make_large_func(args='x', add='x.x{}', size=256)
        a = C()
        for _ in range(REPETITION):
            self.assertEqual(f(a), 32640)

    def test_l2_cache_hit_afer_exhaustion(self):
        '''tests running out of cache instances, and then having another
function grab those instances from the L2 cache'''
        class C:
            def __init__(self):
                self.ext = 0
                for i in range(300):
                    setattr(self, 'x' + hex(i)[2:], i)

        f = self.make_large_func(args='x', add='x.x{}', size=300)
        a = C()
        for _ in range(REPETITION):
            self.assertEqual(f(a), 44850)

        f = self.make_large_func(args='x', add='x.x{}', start=256, size=300)
        for _ in range(REPETITION):
            self.assertEqual(f(a), 12210)

    def test_modify_descriptor(self):
        '''changing a descriptor into a plain old value shouldn't crash'''
        class mydesc(object):
            def __get__(self, inst, ctx):
                return 42
            def __repr__(self):
                return 'mydesc'

        class myobj:
            __slots__ = []
            desc = mydesc()


        def f(x):
            return x.desc


        for i in range(REPETITION):
            self.assertEqual(42, f(myobj()))

        del mydesc.__get__
        self.assertEqual(repr(f(myobj())), 'mydesc')

    def test_type_resurrection(self):
        class metafin(type):
            def __del__(self):
                nonlocal C
                C = self

        class C(metaclass=metafin):
            def __init__(self):
                self.abc = 42
                self.foo = 200

        def f(x):
            return x.abc
        def g(x):
            return x.foo

        a = C()
        for _ in range(REPETITION):
            self.assertEqual(f(a), 42)

        if not CINDERJIT_ENABLED:
            self.assertNotEqual(len(weakref.getweakrefs(C)), 0)
        del a, C, metafin

        gc.collect()
        self.assertEqual(len(weakref.getweakrefs(C)), 0)

        a = C()
        C.abc = property(lambda x:100)
        self.assertEqual(f(a), 100)
        for _ in range(REPETITION):
            self.assertEqual(g(a), 200)
        if not CINDERJIT_ENABLED:
            self.assertNotEqual(len(weakref.getweakrefs(C)), 0)

    def test_type_resurrection_2(self):
        class metafin(type):
            def __del__(self):
                nonlocal C
                C = self

        class C(metaclass=metafin):
            abc = 42

        def f(x):
            return x.abc

        a = C()
        for _ in range(REPETITION):
            self.assertEqual(f(a), 42)
        del a, C, metafin

        gc.collect()

        a = C()
        C.abc = 100
        self.assertEqual(f(a), 100)

    def test_descriptor_ends_split_dict(self):
        for x in range(REPETITION):
            mutating = False
            class myprop:
                def __init__(self, func):
                    self.func = func

                def __get__(self, inst, ctx):
                    return self.func(inst)

            class myclass(object):
                def __init__(self):
                    self.quox = 100
                    self.baz = 200

                @myprop
                def abc(self):
                    if mutating and 'quox' in self.__dict__:
                        # Force migration off of shared dict
                        del self.quox
                    return self.baz
            l = g = {}
            exec('''
def f(x):
    return x.abc''', l, g)
            f = l['f']
            inst = myclass()
            for i in range(REPETITION):
                if i == x:
                    mutating = True
                self.assertEqual(f(inst), 200)

    def test_eq_side_effects(self):
        '''dict key which overrides __eq__ and mutates the class during a get'''
        for x in range(REPETITION):
            mutating = False
            class funkyattr:
                def __init__(self, name):
                    self.name = name
                    self.hash = hash(name)

                def __eq__(self, other):
                    if mutating:
                        if hasattr(myobj, 'foo'):
                            del myobj.foo
                        return False

                    if isinstance(other, str):
                        return other == self.name
                    return self is other

                def __hash__(self):
                    return self.hash

            class myobj:
                foo = 2000

            inst = myobj()
            inst.__dict__[funkyattr('foo')] = 42
            def f(x):
                return x.foo

            for i in range(REPETITION):
                if i == x:
                    mutating = True

                res = f(inst)
                if i == x:
                    # If we removed the instance we should get the descriptor
                    # before it was assigned
                    self.assertEqual(res, 2000, repr(i))
                    mutating = False
                else:
                    self.assertEqual(res, 42)
                res = f(inst)

                self.assertEqual(res, 42)

    def test_knob(self):
        # Its on because we enabled it for the test
        try:
            knobs = cinder.getknobs()
            self.assertEqual(knobs['shadowcode'], True)

            cinder.setknobs({'shadowcode':False})

            knobs = cinder.getknobs()
            self.assertEqual(knobs['shadowcode'], False)

        finally:
            cinder.setknobs({'shadowcode':True})
            knobs = cinder.getknobs()
            self.assertEqual(knobs['shadowcode'], True)

    def test_store_attr_dict(self):
        class C:
            def __init__(self, x):
                if x is True:
                    self.x = 1
                    self.y = 2
                elif x is False:
                    self.y = 2
                    self.x = 1

        def f(x):
            x.z = 100

        class D:
            pass

        for _ in range(REPETITION):
            a = C(True)
            f(a)
            self.assertEqual(a.z, 100)
            b = C(True)
            f(b)
            self.assertEqual(b.z, 100)
            # Force creation of dict on call site
            c = C(None)
            f(c)
            self.assertEqual(c.z, 100)

        # Force invalidation due to wrong type
        x = D()
        f(x)
        self.assertEqual(x.z, 100)

    def test_store_attr_dict_type_change(self):
        class C:
            def __init__(self):
                self.x = 42

        def f(x):
            x.z = 100
        for _ in range(REPETITION):
            x = C()
            f(x)
            self.assertEqual(x.z, 100)
        # Force invalidation of due to type mutation
        C.foo = 100
        x = C()
        f(x)
        self.assertEqual(x.z, 100)

    def test_store_attr_descr(self):
        class C:
            def __init__(self):
                self.x = 42

            @property
            def f(self):
                return 42

            @f.setter
            def f(self, value):
                self.x = value

        def f(x):
            x.f = 100
        for _ in range(REPETITION):
            x = C()
            f(x)
            self.assertEqual(x.x, 100)

    def test_store_attr_descr_type_change(self):
        class C:
            def __init__(self):
                self.x = 42

            @property
            def f(self):
                return 42

            @f.setter
            def f(self, value):
                self.x = value

        def f(x):
            x.f = 100
        for _ in range(REPETITION):
            x = C()
            f(x)
            self.assertEqual(x.x, 100)
        # Mutate the type with a new descriptor
        def setter(self, value):
            self.y = value
        C.f = property(None, setter)
        x = C()
        f(x)
        self.assertEqual(x.y, 100)

    def test_store_attr_descr_error(self):
        should_raise = False
        class C:
            def __init__(self):
                self.x = 42

            @property
            def f(self):
                return 42

            @f.setter
            def f(self, value):
                if should_raise:
                    raise ValueError('no way')
                self.x = value

        def f(x):
            x.f = 100
        for _ in range(REPETITION):
            x = C()
            f(x)
            self.assertEqual(x.x, 100)
        should_raise = True
        with self.assertRaisesRegex(ValueError, "no way"):
            f(C())

    def test_no_attr(self):
        def f(x):
            x.foo = 42

        for _ in range(REPETITION):
            with self.assertRaisesRegex(AttributeError, "'object' object has no attribute 'foo'"):
                f(object())

    def test_read_only_attr(self):
        def f(x):
            x.__str__ = 42

        for _ in range(REPETITION):
            with self.assertRaisesRegex(AttributeError, "'object' object attribute '__str__' is read-only"):
                f(object())

    def test_split_dict_creation(self):
        class C:
            def __init__(self, init):
                if init:
                    self.a = 1
                    self.b = 2
                    self.c = 3

        def f(x):
            x.a = 100

        for _ in range(REPETITION):
            x = C(True)
            f(x)
            self.assertEqual(x.a, 100)

        # force creation of the split dict
        x = C(False)
        f(x)
        self.assertEqual(x.a, 100)

    def test_split_dict_not_split(self):
        class C:
            def __init__(self, init):
                if init:
                    self.a = 1
                    self.b = 2
                    self.c = 3

        def f(x):
            x.a = 100

        for _ in range(REPETITION):
            x = C(True)
            f(x)
            self.assertEqual(x.a, 100)

        x = C(False)
        x.other = 42
        f(x)
        self.assertEqual(x.a, 100)

    def test_split_replace_existing_attr(self):
        class C:
            def __init__(self):
                self.a = 1
                self.b = 2
                self.c = 3

        def f(x):
            x.a = 100

        for _ in range(REPETITION):
            x = C()
            # always replaces an attribute already assigned
            f(x)
            self.assertEqual(x.a, 100)

    def test_split_dict_next_attr(self):
        class C:
            def __init__(self, init):
                self.a = 1
                self.b = 2
                if init:
                    self.c = 3

        def f(x):
            x.c = 100

        for _ in range(REPETITION):
            x = C(True)
            f(x)
            self.assertEqual(x.c, 100)
        # replaces the next to-be-assigned attribute in the split dict
        x = C(False)
        f(x)
        self.assertEqual(x.c, 100)

    def test_split_dict_start_tracking(self):
        dels = 0
        class C:
            def __init__(self):
                self.a = 1
            def __del__(self):
                nonlocal dels
                dels += 1

        def f(x, v):
            x.b = v

        for _ in range(REPETITION):
            x = C()
            # setup circular reference, tracking should have flipped on
            f(x, x)
            self.assertEqual(x.b, x)
            del x

        gc.collect()
        self.assertEqual(dels, REPETITION)

    def test_load_method_builtin(self):
        """INVOKE_METHOD on a builtin object w/o a dictionary"""
        x = 'abc'
        def f(x):
            return x.upper()
        for _ in range(REPETITION):
            self.assertEqual(f(x), 'ABC')

    def test_load_method_no_dict(self):
        """INVOKE_METHOD on a user defined object w/o a dictionary"""
        class C:
            __slots__ = ()
            def f(self): return 42

        a = C()
        def f(x):
            return x.f()

        for _ in range(REPETITION):
            self.assertEqual(f(a), 42)

    def test_load_method_no_dict_invalidate(self):
        class C:
            __slots__ = ()
            def f(self): return 42

        a = C()
        def f(x):
            return x.f()

        for _ in range(REPETITION):
            self.assertEqual(f(a), 42)

        C.f = lambda self: 100
        for _ in range(REPETITION):
            self.assertEqual(f(a), 100)

    def test_load_method_no_dict_invalidate_to_prop(self):
        """switch from a method to a descriptor"""
        class C:
            __slots__ = ()
            def f(self): return 42

        a = C()
        def f(x):
            return x.f()

        for _ in range(REPETITION):
            self.assertEqual(f(a), 42)

        C.f = property(lambda *args: lambda: 100)
        for _ in range(REPETITION):
            self.assertEqual(f(a), 100)

    def test_load_method_non_desc(self):
        """INVOKE_METHOD on a user defined object which isn't a descriptor"""
        class callable:
            def __call__(self, *args):
                return 42
        class C:
            __slots__ = ()
            f = callable()

        a = C()
        def f(x):
            return x.f()

        for _ in range(REPETITION):
            self.assertEqual(f(a), 42)

    def test_load_method_non_desc_invalidate(self):
        """INVOKE_METHOD on a user defined object which isn't a descriptor
        and then modify the type"""
        class callable:
            def __init__(self, value):
                self.value = value

            def __call__(self, *args):
                return self.value

        class C:
            __slots__ = ()
            f = callable(42)

        a = C()
        def f(x):
            return x.f()

        for _ in range(REPETITION):
            self.assertEqual(f(a), 42)

        C.f = callable(100)
        for _ in range(REPETITION):
            self.assertEqual(f(a), 100)

    def test_load_method_non_desc_invalidate_to_method(self):
        """INVOKE_METHOD on a user defined object which isn't a descriptor
        and then modify the type"""
        class callable:
            def __call__(self, *args):
                return 42

        class C:
            __slots__ = ()
            f = callable()

        a = C()
        def f(x):
            return x.f()

        for _ in range(REPETITION):
            self.assertEqual(f(a), 42)

        C.f = lambda self: 100
        for _ in range(REPETITION):
            self.assertEqual(f(a), 100)

    def test_load_method_with_dict(self):
        class C:
            def f(self): return 42

        a = C()
        def f(x):
            return x.f()

        for _ in range(REPETITION):
            self.assertEqual(f(a), 42)

    def test_load_method_with_dict_set_value(self):
        class C:
            def f(self): return 42

        a = C()
        def f(x):
            return x.f()

        for _ in range(REPETITION):
            self.assertEqual(f(a), 42)

        a.f = lambda: 100
        for _ in range(REPETITION):
            self.assertEqual(f(a), 100)

    def test_load_method_with_dict_value_set_initially(self):
        class C:
            def f(self): return 42

        a = C()
        def f(x):
            return x.f()
        a.f = lambda: 100

        for _ in range(REPETITION):
            self.assertEqual(f(a), 100)

    def test_load_method_with_dict_desc_replace_value(self):
        hit_count = 0
        class desc:
            def __get__(self, inst, cls):
                nonlocal hit_count
                hit_count += 1
                return lambda: 42

        class C:
            f = desc()

        a = C()
        def f(x):
            return x.f()

        for _ in range(REPETITION):
            self.assertEqual(f(a), 42)
        self.assertEqual(hit_count, REPETITION)

        a.__dict__['f'] = lambda: 100

        for _ in range(REPETITION):
            self.assertEqual(f(a), 100)
        self.assertEqual(hit_count, REPETITION)


    def test_load_method_with_dict_desc_initial_value(self):
        hit_count = 0
        class desc:
            def __get__(self, inst, cls):
                nonlocal hit_count
                hit_count += 1
                return lambda: 42

        class C:
            def __init__(self):
                self.f = lambda: 100
            f = desc()

        a = C()
        def f(x):
            return x.f()

        for _ in range(REPETITION):
            self.assertEqual(f(a), 100)

        self.assertEqual(hit_count, 0)

    def test_load_method_non_desc_with_dict(self):
        class callable:
            def __call__(self, *args):
                return 42

        class C:
            f = callable()

        a = C()
        def f(x):
            return x.f()

        for _ in range(REPETITION):
            self.assertEqual(f(a), 42)

    def test_load_method_descr_builtin(self):
        """INVOKE_METHOD on a descriptor w/o a dictionary"""
        x = 42
        def f(x):
            try:
                return x.imag()
            except Exception as e:
                return type(e)
        for _ in range(REPETITION):
            self.assertEqual(f(x), TypeError)

    def test_descr_modifies_type(self):
        for x in range(REPETITION):
            mutating = False
            class C:
                @property
                def f(self):
                    if mutating:
                        C.f = lambda self: 42
                    return lambda: 100

            a = C()
            d = {}
            # get a fresh code object for each test...
            exec('def f(x): return x.f()', d)
            f = d['f']

            for i in range(REPETITION):
                if i == x:
                    mutating = True
                if i <= x:
                    self.assertEqual(f(a), 100)
                else:
                    self.assertEqual(f(a), 42)

    def test_polymorphic_method(self):
        outer = self
        class C:
            def f(self):
                outer.assertEqual(type(self).__name__, 'C')
                return 'C'
        class D:
            def f(self):
                outer.assertEqual(type(self).__name__, 'D')
                return 'D'

        def f(x):
            return x.f()

        c = C()
        d = D()
        for i in range(REPETITION):
            self.assertEqual(f(c), 'C')
            self.assertEqual(f(d), 'D')

    def test_polymorphic_exhaust_cache(self):
        outer = self
        class C:
            def f(self):
                outer.assertEqual(type(self).__name__, 'C')
                return 'C'

        def f(x):
            return x.f()

        c = C()
        for i in range(REPETITION):
            self.assertEqual(f(c), 'C')

        l = []
        for i in range(500):
            class X:
                x = i
                def f(self):
                    return self.x
            self.assertEqual(f(X()), i)

        for i in range(REPETITION):
            self.assertEqual(f(c), 'C')

    def test_polymorphic_method_mutating(self):
        outer = self
        class C:
            name = 42
            def f(self):
                outer.assertEqual(type(self).__name__, 'C')
                return C.name
        class D:
            def f(self):
                outer.assertEqual(type(self).__name__, 'D')
                return 'D'

        def f(x):
            return x.f()

        c = C()
        d = D()
        for i in range(REPETITION):
            name = c.name
            self.assertEqual(f(c), name)
            C.name += 1
            self.assertEqual(f(d), 'D')

    def test_polymorphic_method_no_dict(self):
        outer = self
        class C:
            __slots__ = ()
            def f(self):
                outer.assertEqual(type(self).__name__, 'C')
                return 'C'
        class D:
            __slots__ = ()
            def f(self):
                outer.assertEqual(type(self).__name__, 'D')
                return 'D'

        def f(x):
            return x.f()

        c = C()
        d = D()
        for i in range(REPETITION):
            self.assertEqual(f(c), 'C')
            self.assertEqual(f(d), 'D')

    def test_polymorphic_method_mutating_no_dict(self):
        outer = self
        class C:
            __slots__ = ()
            name = 42
            def f(self):
                outer.assertEqual(type(self).__name__, 'C')
                return C.name
        class D:
            __slots__ = ()
            def f(self):
                outer.assertEqual(type(self).__name__, 'D')
                return 'D'

        def f(x):
            return x.f()

        c = C()
        d = D()
        for i in range(REPETITION):
            name = c.name
            self.assertEqual(f(c), name)
            C.name += 1
            self.assertEqual(f(d), 'D')

    def test_polymorphic_method_mixed_dict(self):
        outer = self
        class C:
            __slots__ = ()
            def f(self):
                outer.assertEqual(type(self).__name__, 'C')
                return 'C'
        class D:
            def f(self):
                outer.assertEqual(type(self).__name__, 'D')
                return 'D'

        def f(x):
            return x.f()

        c = C()
        d = D()
        for i in range(REPETITION):
            self.assertEqual(f(c), 'C')
            self.assertEqual(f(d), 'D')

    def test_polymorphic_method_mutating_mixed_dict(self):
        outer = self
        class C:
            __slots__ = ()
            name = 42
            def f(self):
                outer.assertEqual(type(self).__name__, 'C')
                return C.name
        class D:
            def f(self):
                outer.assertEqual(type(self).__name__, 'D')
                return 'D'

        def f(x):
            return x.f()

        c = C()
        d = D()
        for i in range(REPETITION):
            name = c.name
            self.assertEqual(f(c), name)
            C.name += 1
            self.assertEqual(f(d), 'D')

    def test_invoke_method_inst_only_split_dict(self):
        class C:
            pass

        a = C()
        a.f = lambda: 42

        def f(x):
            return x.f()

        for i in range(REPETITION):
            self.assertEqual(f(a), 42)

        del a.f
        with self.assertRaises(AttributeError):
            f(a)

    def test_invoke_method_inst_only(self):
        class C:
            pass

        a = C()
        a.foo = 42

        b = C()
        b.bar = 42
        b.f = lambda: 42

        def f(x):
            return x.f()

        for i in range(REPETITION):
            self.assertEqual(f(b), 42)

        del b.f
        with self.assertRaises(AttributeError):
            f(b)

    def test_instance_dir_mutates_with_custom_hash(self):
        class C:
            def f(self):
                return 42

        def f(x):
            return x.f()

        x = C()
        for i in range(REPETITION):
            self.assertEqual(f(x), 42)

        class mystr(str):
            def __eq__(self, other):
                del C.f
                return super().__eq__(other)

            def __hash__(self):
                return str.__hash__(self)

        x.__dict__[mystr('f')] = lambda: 100
        self.assertEqual(f(x), 100)

    def test_instance_dir_mutates_with_custom_hash_different_attr(self):
        class C:
            def f(self):
                return 42

        def f(x):
            return x.f()

        x = C()
        for i in range(REPETITION):
            self.assertEqual(f(x), 42)

        class mystr(str):
            def __eq__(self, other):
                del C.f
                return super().__eq__(self, other)

            def __hash__(self):
                return hash('f')

        x.__dict__[mystr('g')] = lambda: 100
        self.assertEqual(f(x), 42)

    def test_instance_dir_mutates_with_custom_hash_descr(self):
        class descr:
            def __get__(self, inst, ctx):
                return lambda: 42

        class C:
            f = descr()

        def f(x):
            return x.f()

        x = C()
        for i in range(REPETITION):
            self.assertEqual(f(x), 42)

        class mystr(str):
            def __eq__(self, other):
                del C.f
                return super().__eq__(other)

            def __hash__(self):
                return str.__hash__(self)

        x.__dict__[mystr('f')] = lambda: 100
        self.assertEqual(f(x), 100)

    def test_instance_dir_mutates_with_custom_hash_different_attr_descr(self):
        class descr:
            def __get__(self, inst, ctx):
                return lambda: 42

        class C:
            f = descr()

        def f(x):
            return x.f()

        x = C()
        for i in range(REPETITION):
            self.assertEqual(f(x), 42)

        class mystr(str):
            def __eq__(self, other):
                del C.f
                return super().__eq__(self, other)

            def __hash__(self):
                return hash('f')

        x.__dict__[mystr('g')] = lambda: 100
        self.assertEqual(f(x), 42)

    def test_loadmethod_cachelines(self):
        class C:
            def f(self):
                return 42

        def f1(x):
            return x.f()

        def f2(x):
            return x.f()

        x = C()
        for i in range(REPETITION):
            self.assertEqual(f1(x), 42)


        class descr:
            def __get__(self, inst, ctx):
                return lambda: 100

        C.f = descr()
        for i in range(REPETITION):
            self.assertEqual(f2(x), 100)

        self.assertEqual(f1(x), 100)

    def test_exhaust_invalidation(self):
        class C: pass

        def f(x): return x.f()
        def g(x): return x.f()

        x = C()
        for i in range(2000):
            def maker(i):
                return lambda self: i
            C.f = maker(i)
            self.assertEqual(f(x), i)
            self.assertEqual(g(x), i)

    def test_type_call(self):
        class C:
            def f(self):
                return 42

        a = C()
        def f(x, inst):
            return x.f(inst)
        for _ in range(REPETITION):
            self.assertEqual(f(C, a), 42)

    def test_type_call_metatype(self):
        class MC(type):
            pass
        class C(metaclass=MC):
            def f(self):
                return 42

        a = C()
        def f(x, inst):
            return x.f(inst)
        for _ in range(REPETITION):
            self.assertEqual(f(C, a), 42)

    def test_type_call_metatype_add_getattr(self):
        class MC(type):
            pass
        class C(metaclass=MC):
            def f(self):
                return 42

        a = C()
        def f(x, inst):
            return x.f(inst)
        for _ in range(REPETITION):
            self.assertEqual(f(C, a), 42)

        MC.__getattribute__ = lambda self, name: lambda self: 100
        self.assertEqual(f(C, a), 100)

    def test_metatype_getattr(self):
        class MC(type):
            def __getattribute__(self, name):
                return 100
        class C(metaclass=MC):
            x = 42

        def f(inst):
            return inst.x
        for _ in range(REPETITION):
            self.assertEqual(f(C), 100)

    def test_metatype_add_getattr(self):
        class MC(type):
            pass
        class C(metaclass=MC):
            x = 42

        def f(inst):
            return inst.x
        for _ in range(REPETITION):
            self.assertEqual(f(C), 42)

        MC.__getattribute__ = lambda self, name: 100
        self.assertEqual(f(C), 100)

    def test_metatype_add_getattr_no_leak(self):
        class MC(type):
            pass
        class C(metaclass=MC):
            x = 42

        wr = weakref.ref(C)

        def f(inst):
            return inst.x
        for _ in range(REPETITION):
            self.assertEqual(f(C), 42)
        import gc
        del C
        gc.collect()
        self.assertEqual(wr(), None)

    def test_metatype_change(self):
        class MC(type):
            pass
        class MC2(type):
            def __getattribute__(self, name):
                return 100

        class C(metaclass=MC):
            x = 42

        def f(inst):
            return inst.x
        for _ in range(REPETITION):
            self.assertEqual(f(C), 42)
        C.__class__ = MC2
        self.assertEqual(f(C), 100)

    def test_type_call_invalidate(self):
        class C:
            def f(self):
                return 42

        a = C()
        def f(x, inst):
            return x.f(inst)
        for _ in range(REPETITION):
            self.assertEqual(f(C, a), 42)

        C.f = lambda self: 100
        self.assertEqual(f(C, a), 100)

    def test_type_call_descr(self):
        test = self
        class descr:
            def __get__(self, inst, ctx):
                test.assertEqual(inst, None)
                test.assertEqual(ctx, C)
                return lambda: 42

        class C:
            f = descr()

        a = C()
        def f(x):
            return x.f()

        for _ in range(REPETITION):
            self.assertEqual(f(C), 42)

    def test_type_call_non_descr(self):
        test = self
        class descr:
            def __call__(self):
                return 42

        class C:
            f = descr()

        a = C()
        def f(x):
            return x.f()

        for _ in range(REPETITION):
            self.assertEqual(f(C), 42)

    def test_load_slot(self):
        class C:
            __slots__ = ('value')
            def __init__(self, value):
                self.value = value

        def f(x):
            return x.value

        for i in range(REPETITION):
            x = C(i)
            self.assertEqual(f(x), i)

    def test_load_slot_cache_miss(self):
        class C:
            __slots__ = ('value')
            def __init__(self, value):
                self.value = value

        def f(x):
            return x.value

        for i in range(REPETITION):
            x = C(i)
            self.assertEqual(f(x), i)

        class D:
            value = 100

        self.assertEqual(f(D), 100)

    def test_load_slot_unset(self):
        class C:
            __slots__ = ('value')

        def f(x):
            return x.value

        for i in range(REPETITION):
            x = C()
            x.value = i
            self.assertEqual(f(x), i)

        with self.assertRaises(AttributeError):
            f(C())

    def test_store_slot(self):
        class C:
            __slots__ = ('value')

        def f(x, i):
            x.value = i

        for i in range(REPETITION):
            x = C()
            f(x, i)
            self.assertEqual(x.value, i)

    def test_store_slot_cache_miss(self):
        class C:
            __slots__ = ('value')

        def f(x, i):
            x.value = i

        for i in range(REPETITION):
            x = C()
            f(x, i)
            self.assertEqual(x.value, i)

        class D:
            pass

        x = D()
        f(x, 100)
        self.assertEqual(x.value, 100)

    @skipIf(cached_property is None, "no cached_property")
    def test_cached_property(self):
        class C:
            def __init__(self, value = 42):
                self.value = value
                self.calls = 0

            @cached_property
            def f(self):
                self.calls += 1
                return self.value

        def f(x):
            return x.f

        # creating value in eval loop...
        for i in range(REPETITION):
            inst = C(i)
            self.assertEqual(f(inst), i)

        # accessing existing value in eval loop...
        inst = C(42)
        v = inst.f
        for _ in range(REPETITION):
            self.assertEqual(f(inst), 42)

        # value is successfully cached
        x = C(42)
        f(x)
        f(x)
        self.assertEqual(x.calls, 1)

    @skipIf(cached_property is None, "no cached_property")
    def test_cached_property_raises(self):
        class C:
            def __init__(self, raises = False):
                self.raises = raises

            @cached_property
            def f(self):
                if self.raises:
                    raise ShadowError()
                return 42

        def f(x):
            return x.f

        for _ in range(REPETITION):
            inst = C()
            self.assertEqual(f(inst), 42)

        inst = C(True)
        with self.assertRaises(ShadowError):
            f(inst)

    @skipIf(cached_property is None, "no cached_property")
    def test_cached_property_slots(self):
        class C:
            __slots__ = ('f', 'value', 'calls')
            def __init__(self, value = 42):
                self.value = value
                self.calls = 0

        def f(self):
            self.calls += 1
            return self.value

        C.f = cached_property(f, C.f)

        def f(x):
            return x.f

        # accessing existing value in eval loop...
        inst = C(42)
        v = inst.f
        for _ in range(REPETITION):
            self.assertEqual(f(inst), 42)

        # value is successfully cached
        x = C(42)
        f(x)
        f(x)
        self.assertEqual(x.calls, 1)

    @skipIf(cached_property is None, "no cached_property")
    def test_cached_property_slots_raises(self):
        class C:
            __slots__ = ('raises', 'f')
            def __init__(self, raises = False):
                self.raises = raises

        def f(self):
            if self.raises:
                raise ShadowError()
            return 42

        C.f = cached_property(f, C.f)

        def f(x):
            return x.f

        for _ in range(REPETITION):
            inst = C()
            self.assertEqual(f(inst), 42)

        inst = C(True)
        with self.assertRaises(ShadowError):
            f(inst)

    def test_module_attr(self):
        mod = type(sys)('foo')
        mod.x = 42

        def f(x):
            return x.x

        for _ in range(REPETITION):
            self.assertEqual(f(mod), 42)

        mod.x = 100
        for _ in range(REPETITION):
            self.assertEqual(f(mod), 100)

    def test_module_descr_conflict(self):
        mod = type(sys)('foo')
        func = mod.__dir__

        def f(x):
            return x.__dir__

        for _ in range(REPETITION):
            self.assertEqual(f(mod), func)

        mod.__dir__ = 100
        self.assertEqual(f(mod), 100)

    def test_multi_cache(self):
        class C:
            x = 1

        class D:
            x = 2

        def f(a, c):
            if c:
                return a.x
            else:
                return a.x

        c = C()
        d = D()
        for _i in range(REPETITION):
            self.assertEqual(f(c, True), 1)
            self.assertEqual(f(c, False), 1)
        C.x = 3

        self.assertEqual(f(d, True), 2)
        self.assertEqual(f(c, False), 3)

    def test_multi_cache_module(self):
        m1 = type(sys)('m1')
        m1.x = 1
        m1.y = 2
        m2 = type(sys)('m2')
        m2.x = 3
        m2.y = 4

        def f(a, c):
            if c == 1:
                return a.x
            elif c == 2:
                return a.y
            elif c == 3:
                return a.x

        for _i in range(REPETITION):
            # Init all 3 caches to be m1 at initial version, we'll end up
            # with 2 cache entries
            self.assertEqual(f(m1, 1), 1)
            self.assertEqual(f(m1, 2), 2)
            self.assertEqual(f(m1, 3), 1)

        # invalidate the module
        m1.x = 5
        # replace the 1st cache entry for x with one for m2.y
        self.assertEqual(f(m2, 2), 4)
        # 3rd cache entry shares an entry
        self.assertEqual(f(m1, 1), 5)

    def test_module_method(self):
        mymod = type(sys)('foo')
        def mod_meth():
            return 42

        mymod.mod_meth = mod_meth

        def f(x):
            return x.mod_meth()

        for _i in range(REPETITION):
            self.assertEqual(f(mymod), 42)

    def test_module_method_invalidate(self):
        mymod = type(sys)('foo')
        def mod_meth():
            return 42

        mymod.mod_meth = mod_meth

        def f(x):
            return x.mod_meth()

        for _i in range(REPETITION):
            self.assertEqual(f(mymod), 42)

        for _i in range(REPETITION):
            mymod.mod_meth = lambda: _i
            self.assertEqual(f(mymod), _i)

    def test_module_method_miss(self):
        mymod = type(sys)('foo')
        def mod_meth():
            return 42

        mymod.mod_meth = mod_meth

        def f(x):
            return x.mod_meth()

        class C:
            def mod_meth(self):
                return 'abc'

        for _i in range(REPETITION):
            self.assertEqual(f(mymod), 42)

        self.assertEqual(f(C()), 'abc')

    def test_module_getattr(self):
        mymod = type(sys)('foo')
        def mod_getattr(name):
            if name == "attr":
                return "abc"

            raise AttributeError(name)

        mymod.attr = 42
        mymod.__getattr__ = mod_getattr

        def f(x):
            return x.attr

        for _i in range(REPETITION):
            self.assertEqual(f(mymod), 42)

        del mymod.attr
        self.assertEqual(f(mymod), 'abc')

    def test_module_method_getattr(self):
        mymod = type(sys)('foo')
        def mod_meth():
            return 42
        def mod_getattr(name):
            if name == "mod_meth":
                return lambda: "abc"

            raise AttributeError(name)

        mymod.mod_meth = mod_meth
        mymod.__getattr__ = mod_getattr

        def f(x):
            return x.mod_meth()

        for _i in range(REPETITION):
            self.assertEqual(f(mymod), 42)

        del mymod.mod_meth
        self.assertEqual(f(mymod), 'abc')

    def test_type_error_every_access(self):
        runcount = 0
        class Raises:
            def __get__(self, instance, owner):
                nonlocal runcount
                runcount += 1
                raise NotImplementedError


        class C:
            prop = Raises()

        def f(c):
            try:
                return c.prop
            except NotImplementedError:
                return 42

        for i in range(200):
            runcount = 0
            self.assertEqual(f(C), 42)
            self.assertEqual(runcount, 1)

    def test_module_error_every_access(self):
        m = type(sys)('test')
        runcount = 0
        class mystr(str):
            def __eq__(self, other):
                nonlocal runcount
                runcount += 1
                raise NotImplementedError
            def __hash__(self):
                return str.__hash__(self)

        m.__dict__[mystr('foo')] = 42

        def f(c):
            try:
                return c.foo
            except AttributeError:
                return 42

        for i in range(200):
            runcount = 0
            self.assertRaises(NotImplementedError, f, m)
            self.assertEqual(runcount, 1)

    def test_module_error_getattr(self):
        m = type(sys)('test')
        runcount = 0
        class mystr(str):
            def __eq__(self, other):
                nonlocal runcount
                runcount += 1
                raise NotImplementedError
            def __hash__(self):
                return str.__hash__(self)

        m.__dict__[mystr('foo')] = 100
        m.__getattr__ = lambda *args: 42

        def f(c):
            return c.foo

        for i in range(200):
            runcount = 0
            self.assertRaises(NotImplementedError, f, m)
            self.assertEqual(runcount, 1)

    def test_dict_subscr(self):
        # key not specialized in shadow code
        key = (1, 2)
        value = 1
        d = {key: value}
        def f():
            return d[key]
        for __ in range(REPETITION):
            self.assertEqual(f(), value)

    def test_list_subscr(self):
        d = list(range(5))
        def f(i):
            return d[i]
        for __ in range(REPETITION):
            for i in range(5):
                self.assertEqual(f(i), i)

    def test_tuple_subscr(self):
        t = (1, 2, 3, 4, 5)
        ans = (2, 3)
        def f():
            return t[1:3]
        for __ in range(REPETITION):
            self.assertEqual(f(), ans)

    def test_dict_str_key(self):
        key = "mykey"
        value = 1
        d = {key: value}
        def f():
            return d[key]
        for __ in range(REPETITION):
            self.assertEqual(f(), value)

    def test_tuple_int_const_key(self):
        t = (1, 2, 3)
        def f():
            return t[0]
        for __ in range(REPETITION):
            self.assertEqual(f(), 1)

    def test_dict_subscr_keyerror(self):
        # key not specialized in shadow code
        key = (1, 2)
        value = 1
        d = {key: value}
        wrong_key = (1, 3)
        def f(k):
            return d[k]
        for __ in range(REPETITION):
            self.assertEqual(f(key), value)
        for __ in range(REPETITION):
            self.assertRaises(KeyError, f, wrong_key)

    def test_dict_subscr_to_non_dict(self):
        key = 1
        value = 1
        d = {key: value}
        t = (1, 2, 3, 4)
        value2 = t[key]
        def f(d, k):
            return d[k]
        for __ in range(REPETITION):
            self.assertEqual(f(d, key), value)
        for __ in range(REPETITION):
            self.assertEqual(f(t, key), value2)

    def test_list_subscr_to_non_list(self):
        l = [1, 2, 3, 4]
        t = (5, 6, 7)
        def f(d, k):
            return d[k]
        for __ in range(REPETITION):
            self.assertEqual(f(l, 0), 1)
        for __ in range(REPETITION):
            self.assertEqual(f(t, 1), 6)

    def test_tuple_subscr_indexerror(self):
        t = (1, 2, 3, 4, 5)
        def f(i):
            return t[i]
        for __ in range(REPETITION):
            self.assertRaises(IndexError, f, 6)

    def test_tuple_subscr_to_non_tuple(self):
        l = [1, 2, 3, 4]
        t = (5, 6, 7)
        def f(d, k):
            return d[k]
        for __ in range(REPETITION):
            self.assertEqual(f(t, 0), 5)
        for __ in range(REPETITION):
            self.assertEqual(f(l, 1), 2)

    def test_dict_str_key_to_nonstr_key(self):
        key = "mykey"
        value = 1
        key2 = 3
        value2 = 4
        d = {key: value, key2: value2}
        def f(k):
            return d[k]
        for __ in range(REPETITION):
            self.assertEqual(f(key), value)
        for __ in range(REPETITION):
            self.assertEqual(f(key2), value2)

    def test_dict_str_key_to_non_dict(self):
        key = "mykey"
        value = 1
        d = {key: value}
        l = [1, 2, 3]
        def f(c, k):
            return c[k]
        for __ in range(REPETITION):
            self.assertEqual(f(d, key), value)
        for __ in range(REPETITION):
            self.assertEqual(f(l, 1), 2)

    def test_tuple_int_const_key_two_tuples(self):
        t = (1, 2, 3)
        t2 = (3, 4, 5)
        def f(t):
            return t[0]
        for __ in range(REPETITION):
            self.assertEqual(f(t), 1)
        for __ in range(REPETITION):
            self.assertEqual(f(t2), 3)

    def test_tuple_int_const_key_indexerror(self):
        t = (0, 1, 2, 3, 4, 5, 6)
        t2 = (0, 1, 2)
        def g(t):
            return t[6]
        for __ in range(REPETITION):
            self.assertEqual(g(t), 6)
        for __ in range(REPETITION):
            self.assertRaises(IndexError, g, t2)

    def test_tuple_int_const_key_too_long(self):
        t = (1, 2, 3)
        def g():
            # 2 ** 100 is out of range of Py_Ssize_t
            return t[1267650600228229401496703205376]
        for __ in range(REPETITION):
            self.assertRaises(IndexError, g)

    def test_tuple_int_const_negative_key(self):
        t1 = (1, 2, 3)
        t2 = (-1, -2, -3, -4, -5)
        def f(t):
            return t[-1]
        for __ in range(REPETITION):
            self.assertEqual(f(t1), 3)
        for __ in range(REPETITION):
            self.assertEqual(f(t2), -5)

    def test_tuple_const_int_not_tuple(self):
        t = (1, 2, 3)
        d = {0: "x"}
        def f(t):
            return t[0]
        for __ in range(REPETITION):
            self.assertEqual(f(t), 1)
        for __ in range(REPETITION):
            self.assertEqual(f(d), "x")

    def test_polymorphic(self):
        class C:
            def __init__(self):
                self.value = 42

        class D:
            def __init__(self):
                self.value = 100

        def f(x):
            return x.value

        c = C()
        d = D()
        for _ in range(REPETITION):
            self.assertEqual(f(c), 42)

        self.assertEqual(f(d), 100)
        self.assertEqual(f(d), 100)
        self.assertEqual(f(d), 100)
        self.assertEqual(f(c), 42)

    def test_polymorphic_type_mutation(self):
        class C:
            value = 42

        class D:
            def __init__(self):
                self.value = 100

        c = C()
        d = D()

        def f(x):
            return x.value

        def poly(x):
            return x.value

        for _ in range(REPETITION):
            self.assertEqual(f(c), 42)
            self.assertEqual(poly(c), 42)

        poly(d)

        # Disable caching on instances of C
        for i in range(2000):
            C.x = i
            f(c)

        self.assertEqual(poly(c), 42)
        C.value = 100
        self.assertEqual(poly(c), 100)

    def test_globals_remove_promote_to_builtin(self):
        global filter
        orig_filter = filter
        filter = 42
        try:
            # We only watch our __dict__
            def f():
                return filter
            for _ in range(REPETITION):
                self.assertEqual(f(), 42)
        finally:
            # now we should start watching builtins
            del filter

        self.assertIs(f(), orig_filter)
        try:
            builtins.filter = 43
            self.assertEqual(f(), 43)
        finally:
            builtins.filter = orig_filter

    def test_loadmethod_meta_getattr(self):
        class MC(type):
            def __getattribute__(self, name):
                return lambda x: x + 1

        class C(metaclass=MC):
            @staticmethod
            def f(x):
                return x

        def f(i):
            return C.f(i)

        for i in range(REPETITION):

            self.assertEqual(f(i), i + 1)

    def test_loadmethod_setattr(self):
        class C:
            pass

        def f(a, i):
            object.__setattr__(a, "foo", i)

        a = C()
        for i in range(REPETITION):
            f(a, i)
            self.assertEqual(a.foo, i)

    def test_loadattr_setattr(self):
        class C:
            pass

        def f(a, i):
            z = object.__setattr__
            z(a, "foo", i)

        a = C()
        for i in range(REPETITION):
            f(a, i)
            self.assertEqual(a.foo, i)

    def test_module_invalidate(self):
        mod = type(sys)('foo')
        mod.foo = 42
        def f1(x):
            return x.foo

        def f2(x):
            return x.foo

        # setup the first function with a cache for the module
        for i in range(REPETITION):
            self.assertEqual(f1(mod), 42)


        # now modify the function, and setup the second version
        # with a new cache.  The old l2 cache entry will be replaced
        mod.foo = 100
        for i in range(REPETITION):
            self.assertEqual(f2(mod), 100)
        del mod

        # now try calling the first function with the old cache
        # entry, it should be invalidated and we shouldn't
        # crash
        mod = type(sys)('foo')
        mod.foo = 300
        self.assertEqual(f1(mod), 300)

    def test_object_field(self):
        # OSError.filename is a T_OBJECT field, which defaults to None.
        # We should always store the value in the descriptor, never
        # in the dictionary
        class C(OSError):
            def __init__(self):
                self.filename = 'abc'

        for i in range(REPETITION):
            x = C()
            self.assertEqual(x.__dict__, {})
            self.assertEqual(x.filename, 'abc')

    def test_readonly_field(self):
        # Shadow byte code shouldn't allow writing to readonly fields
        class C:
            pass

        def f(x):
            x.start = 1

        for i in range(REPETITION):
            f(C())

        with self.assertRaises(AttributeError):
            f(range(5))

    @skipIf(StrictModule is None, "no StrictModule")
    def test_strictmodule(self):
        mod = type(sys)('foo')
        mod.x = 100
        # cheat here by keeping the underlying dict
        d = mod.__dict__
        m = StrictModule(d, False)

        def f():
            return m.x

        for _i in range(REPETITION):
            self.assertEqual(f(), 100)
        d["x"] = 200
        for _i in range(REPETITION):
            self.assertEqual(f(), 200)

    @skipIf(StrictModule is None, "no StrictModule")
    def test_strictmodule_descr_conflict(self):
        mod = type(sys)('foo')
        d = mod.__dict__
        m = StrictModule(d, False)
        func = m.__dir__

        def f(x):
            return x.__dir__

        for i in range(REPETITION):
            self.assertEqual(f(m), func)

        d["__dir__"] = 100
        self.assertEqual(f(m), 100)

    @skipIf(StrictModule is None, "no StrictModule")
    def test_strictmodule_descr_conflict_with_patch(self):
        mod = type(sys)('foo')
        d = mod.__dict__
        m = StrictModule(d, True)
        func = m.__dir__

        def f(x):
            return x.__dir__

        for i in range(REPETITION):
            self.assertEqual(f(m), func)
        strict_module_patch(m, "__dir__", 100)
        self.assertEqual(f(m), 100)

    @skipIf(StrictModule is None, "no StrictModule")
    def test_strictmodule_method(self):
        mod = type(sys)('foo')
        def mod_meth():
            return 42

        mod.mod_meth = mod_meth
        d = mod.__dict__
        m = StrictModule(d, False)

        def f(x):
            return x.mod_meth()

        for _i in range(REPETITION):
            self.assertEqual(f(m), 42)

    @skipIf(StrictModule is None, "no StrictModule")
    def test_strcitmodule_method_invalidate(self):
        mod = type(sys)('foo')
        def mod_meth():
            return 42

        mod.mod_meth = mod_meth
        d = mod.__dict__
        m = StrictModule(d, False)

        def f(x):
            return x.mod_meth()

        for _i in range(REPETITION):
            self.assertEqual(f(m), 42)

        for _i in range(REPETITION):
            d["mod_meth"] = lambda: _i
            self.assertEqual(f(m), _i)

    @skipIf(StrictModule is None, "no StrictModule")
    def test_strictmodule_method_miss(self):
        mod = type(sys)('foo')
        def mod_meth():
            return 42

        mod.mod_meth = mod_meth
        d = mod.__dict__
        m = StrictModule(d, False)

        def f(x):
            return x.mod_meth()

        class C:
            def mod_meth(self):
                return 'abc'

        for _i in range(REPETITION):
            self.assertEqual(f(m), 42)

        self.assertEqual(f(C()), 'abc')

    def test_loadattr_descr_changed_to_data_descr(self):
        class NonDataDescr:
            def __init__(self):
                self.invoked_count = 0

            def __get__(self, obj, typ):
                self.invoked_count += 1
                obj.__dict__["foo"] = "testing 123"
                return "testing 123"

        descr = NonDataDescr()
        class TestObj:
            foo = descr

        def get_foo(obj):
            return obj.foo

        # non-data descriptor should be invoked the first time we look up the
        # attribute; there is nothing in the instance dictionary
        obj = TestObj()
        self.assertEqual(get_foo(obj), "testing 123")
        self.assertEqual(descr.invoked_count, 1)

        # non-data descriptor should no longer be invoked; there is an entry
        # in the instance dictionary that shadows it
        for _ in range(REPETITION):
            self.assertEqual(get_foo(obj), "testing 123")
            self.assertEqual(descr.invoked_count, 1)

        # Convert non-data descr into a data descr
        def setter(self, obj, val):
            pass

        descr.__class__.__set__ = setter

        # data descriptors take priority over entries in the instance's
        # __dict__
        self.assertEqual(get_foo(obj), "testing 123")
        self.assertEqual(descr.invoked_count, 2)

    def test_reassign_split_dict(self):
        class Foo:
            def __init__(self):
                self.attr = 100

        class Bar:
            def __init__(self):
                self.a0 = 0
                self.attr = 200


        def f(x):
            return x.attr

        # Prime the cache
        obj = Foo()
        for _ in range(REPETITION):
            self.assertEqual(f(obj), 100)

        # Update obj using a split dictionary with a different location
        # for attr.
        obj2 = Bar()
        obj.__dict__ = obj2.__dict__
        self.assertEqual(f(obj), 200)

    def test_reassign_class_with_different_split_dict(self):
        class Foo:
            def __init__(self):
                self.attr = 100

        class Bar:
            def __init__(self):
                self.a0 = 0
                self.attr = 200


        def f(x):
            return x.attr

        # Prime the cache
        obj = Foo()
        for _ in range(REPETITION):
            self.assertEqual(f(obj), 100)

        # obj2 has type Foo, but if it uses a split dictionary for attribute
        # storage the dict keys object will be shared across instances of Bar,
        # not Foo.
        obj2 = Bar()
        obj2.__class__ = Foo
        self.assertEqual(f(obj2), 200)

    def test_load_immortal_classmethod(self):
        code = f"""if 1:
            class Foo:
                @classmethod
                def identity(cls, x):
                    return x

            import gc
            gc.immortalize_heap()

            def f(x):
                return Foo.identity(x)

            # Prime the cache
            for _ in range({REPETITION}):
                f(100)

            print(f(100))
            """
        rc, out, err = skip_ret_code_check_for_leaking_test_in_asan_mode('-c', code)
        self.assertEqual(out.strip(), b'100')

    def test_load_immortal_staticmethod(self):
        code = f"""if 1:
            class Foo:
                @staticmethod
                def identity(x):
                    return x

            import gc
            gc.immortalize_heap()

            def f(x):
                return Foo.identity(x)

            # Prime the cache
            for _ in range({REPETITION}):
                f(100)

            print(f(100))
            """
        rc, out, err = skip_ret_code_check_for_leaking_test_in_asan_mode('-c', code)
        self.assertEqual(out.strip(), b'100')

    def test_load_immortal_wrapper_descr(self):
        code = f"""if 1:
            class Foo:
                def __repr__(self):
                    return 12345

            import gc
            gc.immortalize_heap()

            def f():
                return str.__repr__('hello')

            # Prime the cache
            for _ in range({REPETITION}):
                f()

            print(f())
            """
        rc, out, err = skip_ret_code_check_for_leaking_test_in_asan_mode('-c', code)
        self.assertEqual(out.strip(), b"'hello'")

    def test_load_immortal_function(self):
        code = f"""if 1:
            class Oracle:
                def speak():
                    return 42

            import gc
            gc.immortalize_heap()

            def f():
                return Oracle.speak()

            # Prime the cache
            for _ in range({REPETITION}):
                f()

            print(f())
            """
        rc, out, err = skip_ret_code_check_for_leaking_test_in_asan_mode('-c', code)
        self.assertEqual(out.strip(), b"42")

    def test_load_immortal_method_descriptor(self):
        code = f"""if 1:
            import gc
            gc.immortalize_heap()

            def f(l):
                return list.pop(l)

            # Prime the cache
            for _ in range({REPETITION}):
                f([42])

            print(f([42]))
            """
        rc, out, err = skip_ret_code_check_for_leaking_test_in_asan_mode('-c', code)
        self.assertEqual(out.strip(), b"42")

    def test_load_immortal_builtin_function(self):
        code = f"""if 1:
            class Foo:
                pass

            import gc
            gc.immortalize_heap()

            def f():
                return object.__new__(Foo)

            # Prime the cache
            for _ in range({REPETITION}):
                f()

            print(isinstance(f(), Foo))
            """
        rc, out, err = skip_ret_code_check_for_leaking_test_in_asan_mode('-c', code)
        self.assertEqual(out.strip(), b"True")

    def test_load_unshadowed_immortal_method_split_dict(self):
        code = f"""if 1:
            class Oracle:
                def __init__(self):
                    self.answer = 42

                def speak(self):
                    return self.answer

            import gc
            gc.immortalize_heap()

            def f(x):
                return x.speak()

            # Prime the cache
            for _ in range({REPETITION}):
                f(Oracle())

            print(f(Oracle()))
            """
        rc, out, err = skip_ret_code_check_for_leaking_test_in_asan_mode('-c', code)
        self.assertEqual(out.strip(), b'42')

    def test_load_shadowed_immortal_method_split_dict(self):
        code = f"""if 1:
            class Oracle:
                def __init__(self):
                    self.answer = 42

                def speak(self):
                    return self.answer

            import gc
            gc.immortalize_heap()

            def f(x):
                return x.speak()

            # Prime the cache
            for _ in range({REPETITION}):
                f(Oracle())

            # Shadow the method
            obj = Oracle()
            obj.speak = 12345

            print(f(Oracle()))
            """
        rc, out, err = skip_ret_code_check_for_leaking_test_in_asan_mode('-c', code)
        self.assertEqual(out.strip(), b'42')

    def test_load_unshadowed_immortal_method_combineddict(self):
        code = f"""if 1:
            class Oracle:
                def __init__(self):
                    self.answer = 42

                def speak(self):
                    return self.answer

            import gc
            gc.immortalize_heap()

            obj = Oracle()
            obj.foo = 1
            # Force the class to use combined dictionaries
            del obj.foo

            def f(x):
                return x.speak()

            # Prime the cache
            for _ in range({REPETITION}):
                f(Oracle())

            print(f(Oracle()))
            """
        rc, out, err = assert_python_ok('-c', code)
        self.assertEqual(out.strip(), b'42')

    def test_load_shadowed_immortal_method_combineddict(self):
        code = f"""if 1:
            class Oracle:
                def __init__(self):
                    self.answer = 42

                def speak(self):
                    return self.answer

            import gc
            gc.immortalize_heap()

            obj = Oracle()
            obj.foo = 1
            # Force the class to use combined dictionaries
            del obj.foo

            def f(x):
                return x.speak()

            # Prime the cache
            for _ in range({REPETITION}):
                f(Oracle())

            # Shadow the method
            obj = Oracle()
            obj.speak = 12345

            print(f(Oracle()))
            """
        rc, out, err = skip_ret_code_check_for_leaking_test_in_asan_mode('-c', code)
        self.assertEqual(out.strip(), b'42')

    def test_load_unshadowed_immortal_method_no_dict(self):
        code = f"""if 1:
            import gc
            gc.immortalize_heap()

            def f(x):
                return x.count(1)

            l = [1, 2, 3, 1]
            # Prime the cache
            for _ in range({REPETITION}):
                f(l)

            print(f(l))
            """
        rc, out, err = skip_ret_code_check_for_leaking_test_in_asan_mode('-c', code)
        self.assertEqual(out.strip(), b'2')

    def test_instance_to_type(self):
        def f(obj, use_type):
            # we want one initialized cache for the instance
            # variable and one uninitialized cache that we'll
            # hit on the first time with the type, so we pass
            # this extra use_type flag
            if use_type:
                return obj.foo
            else:
                return obj.foo

        class C:
            def __init__(self):
                self.foo = 42

        x = C()
        for i in range(REPETITION):
            f(x, False)

        with self.assertRaises(AttributeError):
            f(C, True)

if __name__ == "__main__":
    unittest.main()
