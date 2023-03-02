# Copyright (c) Meta Platforms, Inc. and affiliates. (http://www.meta.com)

# NOTE: This file is intended to test JIT behavior in the presence of profile
# data. The non-standard filename is so it's not run by default as part of the
# normal test suite; instead, it is run in a special configuration by the
# 'testcinder_jit_profile' make target.

import os
import threading
import unittest
from collections import Counter
from contextlib import contextmanager

from test import cinder_support

try:
    import cinder
except ModuleNotFoundError:
    cinder = None

try:
    import cinderjit
except ModuleNotFoundError:
    cinderjit = None

# Some tests want to make sure the type-specialized code behaves correctly when
# given a type that was not seen during profiling. The simplest and most
# reliable way to do this (given the current system) is to allow the code under
# test to behave differently during profiling and testing.
#
# We expose this within this file through the PROFILING and TESTING globals,
# which will never both be set at the same time (they might both be false, if
# the JIT is disabled). This assumes that calling code will set
# CINDER_JIT_PROFILE_TEST_PROFILING as appropriate.
def check_profiling():
    var = os.getenv("CINDER_JIT_PROFILE_TEST_PROFILING", None)
    return var not in (None, "", "0")


PROFILING = check_profiling()
TESTING = not (PROFILING or cinderjit is None)


class ProfileTest(unittest.TestCase):
    @contextmanager
    def assertDeopts(self, deopt_specs):
        """Assert that the protected region of code has exactly as many deopts as the
        given specs. Specs are given as dict keys in deopt_specs, with the
        values indicating how many deopts of that kind to expect.

        Specs should be tuples of key-value pair tuples, and can reference any
        of the keys and values in
        cinderjit.get_and_clear_runtime_stats()["deopt"]. For example, to match
        two GuardType guard failures, pass:

        {(("reason", "GuardFailure"), ("description", "GuardType)): 2}

        or to assert no deopts, pass an empty dict:

        {}
        """

        cinderjit.get_and_clear_runtime_stats()
        yield
        deopts = cinderjit.get_and_clear_runtime_stats()["deopt"]

        def deopt_matches_spec(deopt, spec):
            for key, val in spec:
                if deopt.get(key) != val:
                    return False
            return True

        found_specs = Counter()
        for deopt in deopts:
            # The data from Cinder is type-segregated for Scuba; flatten it
            # into one dict.
            deopt = {**deopt["normal"], **deopt["int"]}

            # Ignore deopt events not from this file (the unittest machinery
            # often triggers a few).
            if deopt["filename"] != __file__:
                continue

            for spec in deopt_specs:
                if deopt_matches_spec(deopt, spec):
                    found_specs[spec] += deopt["count"]
                    break
            else:
                self.fail(f"Deopt event '{deopt}' doesn't match any given specs")

        for spec, expected_count in deopt_specs.items():
            found_count = found_specs[spec]
            self.assertEqual(found_count, expected_count, f"Deopt spec {spec}")


class BinaryOpTests(ProfileTest):
    def test_long_power(self):
        def do_pow(a, b):
            return a**b

        self.assertEqual(do_pow(2, 2), 4)

        if TESTING:
            with self.assertDeopts({}):
                self.assertEqual(do_pow(2, 8), 256)


class NewThreadTests(ProfileTest):
    def test_create_thread(self):
        x = 5

        def thread_body():
            nonlocal x
            x = 10

        self.assertEqual(x, 5)
        t = threading.Thread(target=thread_body)
        t.start()
        t.join()
        self.assertEqual(x, 10)


@unittest.skipIf(cinder is None, "tests cinder-specific functionality")
class GetProfilesTests(ProfileTest):
    # Run in a subprocess to avoid messing with profile data for the other
    # tests.
    @cinder_support.runInSubprocess
    def test_get_and_clear_type_profiles(self):
        if not PROFILING:
            return

        cinder.get_and_clear_type_profiles()
        result = 0
        for i in range(10):
            result += i
        self.assertEqual(result, 45)

        profiles = cinder.get_and_clear_type_profiles()

        # The main purpose of this test is to make sure
        # get_and_clear_type_profiles() doesn't crash, but let's also sanity
        # check that the data looks as expected.
        for hit in profiles:
            self.assertIn("normal", hit)
            normal = hit["normal"]
            self.assertIn("func_qualname", normal)
            if (
                normal["func_qualname"]
                == "GetProfilesTests.test_get_and_clear_type_profiles"
                and normal.get("opname") == "INPLACE_ADD"
            ):
                break
        else:
            self.fail("Didn't find expected profile hit in results")


class LoadAttrTests(ProfileTest):
    def make_slot_type(caller_name, name, slots, bases=None):
        def init(self, **kwargs):
            for key, val in kwargs.items():
                setattr(self, key, val)

        if bases is None:
            bases = (object,)
            slots = slots + ["__dict__"]

        return type(
            name,
            bases,
            {
                "__init__": init,
                "__slots__": slots,
                "__qualname__": f"LoadAttrTests.{caller_name}.<locals>.{name}",
            },
        )

    BasicSlotAttr = make_slot_type("test_load_from_slot", "BasicSlotAttr", ["b", "c"])
    OtherSlotAttr = make_slot_type(
        "test_load_attr_from_slot", "OtherSlotAttr", ["a", "b", "c", "d"]
    )

    def test_load_attr_from_slot(self):
        def get_a(o):
            return o.a

        def get_b(o):
            return o.b

        def get_c(o):
            return o.c

        def get_d(o):
            return o.d

        o1 = self.BasicSlotAttr(b="bee", c="see")

        self.assertEqual(get_b(o1), "bee")
        self.assertEqual(get_c(o1), "see")

        if TESTING:
            with self.assertDeopts({}):
                self.assertEqual(get_b(o1), "bee")
                self.assertEqual(get_c(o1), "see")

            # Make sure get_b() and get_c() still behave correctly when given
            # a type not seen during profiling, with a different layout.
            o2 = self.OtherSlotAttr(a="aaa", b="bbb", c="ccc", d="ddd")
            with self.assertDeopts(
                {(("reason", "GuardFailure"), ("description", "GuardType")): 2}
            ):
                self.assertEqual(get_a(o2), "aaa")
                self.assertEqual(get_b(o2), "bbb")
                self.assertEqual(get_c(o2), "ccc")
                self.assertEqual(get_d(o2), "ddd")

    ModifiedSlotAttr = make_slot_type(
        "test_modify_type_and_load_attr_from_slot", "ModifiedSlotAttr", ["a", "b"]
    )

    def test_modify_type_and_load_attr_from_slot(self):
        o = self.ModifiedSlotAttr(a=123, b=456)
        o.__dict__ = {"a": "shadowed a"}
        self.assertEqual(o.a, 123)

        def get_attr(o):
            return o.a

        self.assertEqual(get_attr(o), 123)

        if TESTING:
            with self.assertDeopts({}):
                self.assertEqual(get_attr(o), 123)
                o.a = 789
                self.assertEqual(get_attr(o), 789)

            descr_saved = self.ModifiedSlotAttr.a
            del self.ModifiedSlotAttr.a

            with self.assertDeopts(
                {
                    (
                        ("reason", "GuardFailure"),
                        ("description", "DeoptPatchpoint"),
                    ): 3,
                }
            ):
                self.assertEqual(get_attr(o), "shadowed a")
                o.a = "another a"
                self.assertEqual(get_attr(o), "another a")
                self.ModifiedSlotAttr.a = descr_saved
                self.assertEqual(get_attr(o), 789)

    ModifiedOtherAttr = make_slot_type(
        "test_modify_other_type_member", "ModifiedOtherAttr", ["foo", "bar"]
    )

    def test_modify_other_type_member(self):
        o = self.ModifiedOtherAttr(foo="foo", bar="bar")

        def get_foo(o):
            return o.foo

        def get_bar(o):
            return o.bar

        self.assertEqual(get_foo(o), "foo")
        self.assertEqual(get_bar(o), "bar")

        if TESTING:
            self.ModifiedOtherAttr.bar = 5

            with self.assertDeopts({}):
                self.assertEqual(get_foo(o), "foo")

            with self.assertDeopts(
                {(("reason", "GuardFailure"), ("description", "DeoptPatchpoint")): 1}
            ):
                self.assertEqual(get_bar(o), 5)

    class EmptyBase:
        pass

    FakeSlotType = make_slot_type(
        "test_fake_slot_type", "FakeSlotType", ["a", "b"], bases=(EmptyBase,)
    )
    FakeSlotType.c = FakeSlotType.a
    OtherFakeSlotType = make_slot_type(
        "test_fake_slot_type", "OtherFakeSlotType", ["a", "b"], bases=(EmptyBase,)
    )
    OtherFakeSlotType.c = OtherFakeSlotType.b

    def test_fake_slot_type(self):
        """Test __class__ assignment where the new type has a compatible layout but the
        "slot" we specialized on changed anyway, because it was aliasing a real
        slot.
        """
        o1 = self.FakeSlotType(a="a_1", b="b_1")

        def get_attrs(o, do_assign=False):
            a = o.a
            if do_assign:
                o.__class__ = self.OtherFakeSlotType
            c = o.c
            return f"{a}-{c}"

        self.assertEqual(get_attrs(o1), "a_1-a_1")

        if TESTING:
            with self.assertDeopts(
                {
                    (
                        ("reason", "GuardFailure"),
                        ("description", "DeoptPatchpoint"),
                        (
                            "guilty_type",
                            "test.cinderjit_profile_test:OtherFakeSlotType",
                        ),
                    ): 1
                }
            ):
                self.assertEqual(get_attrs(o1, True), "a_1-b_1")
