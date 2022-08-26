from executor import run_fuzzer_on_test_file

# Test file list is subject to change
TEST_FILE_LIST = [
    "/Lib/ctypes/test/test_anon.py",
    "/Lib/ctypes/test/test_array_in_pointer.py",
    "/Lib/ctypes/test/test_bitfields.py",
    "/Lib/ctypes/test/test_callbacks.py",
    "/Lib/test/test_fileio.py",
    "/Lib/test/test_itertools.py",
    "/Lib/test/test_memoryview.py",
    "/Tools/fuzzer/tests/verifier_test.py",
    "/Tools/fuzzer/tests/fuzzer_test.py",
    "/Tools/fuzzer/tests/executor_test.py",
]

# Number of subprocesses are also subject to change
SUBPROCESSES = 4


def run_fuzzer_on_test_files():
    f = open("fuzzer_output.txt", "w+")
    for i in TEST_FILE_LIST:
        run_fuzzer_on_test_file(
            __file__.replace("/Tools/fuzzer/fuzzer/run_fuzzer_on_test_files.py", i),
            SUBPROCESSES,
            f,
        )


if __name__ == "__main__":
    run_fuzzer_on_test_files()
