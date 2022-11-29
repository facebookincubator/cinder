# Copyright (c) Meta Platforms, Inc. and affiliates. (http://www.meta.com)

import unittest
import memoize
from functools import update_wrapper

def mock_cache_fetcher():
    return {}

class TestMemoizeFuncWrapper(unittest.TestCase):
    memoize_func_wrapper = memoize.memoize_func_wrapper

    @staticmethod
    def memoize_func_decorator(cache_fetcher):
        def inner(f):
            wrapper = memoize.memoize_func_wrapper(f, cache_fetcher)
            return update_wrapper(wrapper, f)
        return inner

    def test_non_callable_user_func_throws(self):

        with self.assertRaisesRegex(
            TypeError,
            "func must be callable"
        ):
            self.memoize_func_wrapper("test_a", "test_b")

    def test_non_callable_cache_fetcher_throws(self):
        def user_func():
            pass
        with self.assertRaisesRegex(
            TypeError,
            "cache_fetcher must be callable"
        ):
            self.memoize_func_wrapper(user_func, "test_b")

    def test_cache_fetcher_return_no_dict_throws(self):
        def cache_fetcher():
            pass

        @self.memoize_func_decorator(cache_fetcher)
        def test_meth(a, b):
            return a+b

        with self.assertRaisesRegex(
            TypeError,
            "cache_fetcher must return a dictionary"
        ):
            test_meth(1,2)

    def test_unhashable_key_throws(self):
        @self.memoize_func_decorator(mock_cache_fetcher)
        def test_meth(a, b):
            return a+b
        with self.assertRaises(TypeError):
            test_meth([], [])

    def test_get_expected_cache_key_value_for_method(self):
        cache = {}
        def test_cache_fetcher():
                return cache

        def test_meth(a, b):
            return a+b

        wrapped = self.memoize_func_wrapper(test_meth, test_cache_fetcher)
        wrapped(1,1)
        wrapped(1,1)
        wrapped(1,2)
        wrapped(1,2)
        wrapped(1, b=2)
        wrapped(1, b=2)
        wrapped(a=1,b=2)
        wrapped(b=2,a=1)
        self.assertEqual(len(cache), 5)
        # get kwargs delimiter
        kwd_mark = list(cache.keys())[2][3]
        self.assertTrue((test_meth, 1, 1) in cache)
        self.assertTrue((test_meth, 1, 2) in cache)
        self.assertTrue((test_meth, 1, 2, kwd_mark, "b") in cache)
        self.assertTrue((test_meth, 1, 2, kwd_mark, "a", "b") in cache)
        self.assertTrue((test_meth, 2, 1, kwd_mark, "b", "a") in cache)
        self.assertEqual(cache[(test_meth, 1, 1)], 2)
        self.assertEqual(cache[(test_meth, 1, 2)], 3)
        self.assertEqual(cache[(test_meth, 1, 2, kwd_mark, "b")], 3)
        self.assertEqual(cache[(test_meth, 1, 2, kwd_mark, "a", "b")], 3)
        self.assertEqual(cache[(test_meth, 2, 1, kwd_mark, "b", "a")], 3)


    def test_cached_fn_called_once(self):
        cache = {}
        def test_cache_fetcher():
                return cache
        count = 0

        @self.memoize_func_decorator(test_cache_fetcher)
        def test_meth(a, b):
            nonlocal count
            count += 1
            return a+b

        test_meth(1,1)
        self.assertEqual(count, 1)
        test_meth(1,1)
        self.assertEqual(count, 1)
        test_meth(1,2)
        self.assertEqual(count, 2)
        test_meth(1,b=2)
        self.assertEqual(count, 3)
        test_meth(1,b=2)
        self.assertEqual(count, 3)
        test_meth(a=1,b=2)
        self.assertEqual(count, 4)
        test_meth(b=2,a=1)
        self.assertEqual(count, 5)

    def test_diff_instances_cached_differently(self):
        cache = {}
        def test_cache_fetcher():
                return cache

        count = 0
        class A:
            @self.memoize_func_decorator(test_cache_fetcher)
            def test_meth(self, a, b):
                nonlocal count
                count += 1
                return a+b

        ins_1 = A()
        ins_2 = A()
        ins_1.test_meth(1,1)
        ins_2.test_meth(1,1)
        self.assertEqual(len(cache), 2)
        self.assertEqual(count, 2)

    def test_memoize_with_update_wrapper(self):
        def cache_fetcher():
            {}

        @self.memoize_func_decorator(cache_fetcher)
        def test_meth(a, b):
            return a+b

        self.assertEqual(test_meth.__name__, "test_meth")

if __name__ == '__main__':
    unittest.main()
