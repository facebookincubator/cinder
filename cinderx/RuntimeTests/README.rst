CPython Runtime Tests
=====================

This directory contains tests for CPython that are written in C/C++.

Writing tests using googletest
------------------------------
You can look at other files in this directory for examples of how to
write tests. In a nutshell, create a new file named `*_test.cpp` and
add `*_test.o` to the `RUNTIME_TESTS_OBJS` variable in the Makefile.

If you want to write tests that use the C-API, you need to first create
a test fixture class that derives from `RuntimeTest`:

```
class MyPythonTest : public RuntimeTest {
};
```

The `RuntimeTest` base class will handle initializing (tearing down) the
interpreter before (after) each test case. Take care not to run the tests in
multiple threads, as single interpreter must be used for all the tests.

You can then write your test cases like

```
TEST_F(MyPythonTest, TestFoo) {
...
}
```

`sanity_test.cpp` contains a simple example.


Writing HIR tests
-----------------
Tests that manipulate the HIR typically involve the following steps:

1. Write a small Python function
2. Lower the Python into HIR
3. (Optional) Run an optimization pass
4. Convert the HIR to text
5. Check that the result from (4) is what we expect

Such tests can be specified in files with the following format:

    <Test suite name>
    ---
    <Optimization pass name, can be empty>
    ---
    <Test case name>
    ---
    <Python code>
    ---
    <HIR>
    ---

One or more test cases should be supplied. See `RuntimeTests/hir_tests`
for examples.

HIR tests are added in `RuntimeTests/main.cpp` by calling `register_test` with
the path to the test data file.

You can manually disable a test by prepending `@disabled` to the test case name.
