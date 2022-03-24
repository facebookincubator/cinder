CPython Runtime Tests
=====================

This directory contains tests for CPython that are written in C/C++.

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
