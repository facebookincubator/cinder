from executor import run_fuzzer_on_test_file

# File directories are relative to cinder directory
# Test file list is subject to change
TEST_FILE_LIST = [
    "Lib/ctypes/test/test_anon.py",
    "Lib/ctypes/test/test_array_in_pointer.py",
    "Lib/ctypes/test/test_bitfields.py",
    "Lib/ctypes/test/test_callbacks.py",
    "Lib/test/test_fileio.py",
    "Lib/test/test_memoryview.py",
    "Tools/fuzzer/tests/verifier_test.py",
]

# Number of subprocesses are also subject to change
SUBPROCESSES = 4


def run_fuzzer_on_test_files():
    with open("fuzzer_output.txt", "w+") as outfile:
        for i in TEST_FILE_LIST:
            run_fuzzer_on_test_file(
                i,
                SUBPROCESSES,
                outfile,
            )
    # stdout and stderr stored in file
    # need to print so its available on skycastle
    with open("fuzzer_output.txt", "r") as outfile:
        print(outfile.read())


if __name__ == "__main__":
    run_fuzzer_on_test_files()
