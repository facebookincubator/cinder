import pathlib
import subprocess
import sys
import sysconfig
import unittest

from test.support.os_helper import temp_dir
from test.support.script_helper import assert_python_ok, make_script

try:
    from cinder import _is_compile_perf_trampoline_pre_fork_enabled
except:
    raise unittest.SkipTest("pre-fork perf-trampoline compilation is not enabled")


def supports_trampoline_profiling():
    perf_trampoline = sysconfig.get_config_var("PY_HAVE_PERF_TRAMPOLINE")
    if not perf_trampoline:
        return False
    return int(perf_trampoline) == 1


if not supports_trampoline_profiling():
    raise unittest.SkipTest("perf trampoline profiling not supported")

if not _is_compile_perf_trampoline_pre_fork_enabled():
    raise unittest.SkipTest("pre-fork perf-trampoline compilation is not enabled")


class TestPerfTrampolinePreCompile(unittest.TestCase):
    def setUp(self):
        super().setUp()
        self.perf_files = set(pathlib.Path("/tmp/").glob("perf-*.map"))

    def tearDown(self) -> None:
        super().tearDown()
        files_to_delete = (
            set(pathlib.Path("/tmp/").glob("perf-*.map")) - self.perf_files
        )
        for file in files_to_delete:
            file.unlink()

    def test_trampoline_works(self):
        code = """if 1:
                import sys
                import os
                import sysconfig
                from cinder import _compile_perf_trampoline_pre_fork

                def foo_fork():
                    pass
                def bar_fork():
                    foo_fork()
                def baz_fork():
                    bar_fork()


                def foo():
                    pass
                def bar():
                    foo()
                def baz():
                    bar()

                if __name__ == "__main__":
                    _compile_perf_trampoline_pre_fork()
                    pid = os.fork()
                    if pid == 0:
                        print(os.getpid())
                        baz_fork()
                    else:
                        baz()
                """
        rc, out, err = assert_python_ok("-c", code)
        with temp_dir() as script_dir:
            script = make_script(script_dir, "perftest", code)
            with subprocess.Popen(
                [
                    sys.executable,
                    "-X",
                    "perf-trampoline-prefork-compilation",
                    "-X",
                    "perf",
                    script,
                ],
                universal_newlines=True,
                stderr=subprocess.PIPE,
                stdout=subprocess.PIPE,
            ) as process:
                stdout, stderr = process.communicate()
        self.assertNotIn("Error:", stderr)
        child_pid = int(stdout.strip())
        perf_file = pathlib.Path(f"/tmp/perf-{process.pid}.map")
        perf_child_file = pathlib.Path(f"/tmp/perf-{child_pid}.map")
        self.assertTrue(perf_file.exists())
        self.assertTrue(perf_child_file.exists())

        perf_file_contents = perf_file.read_text()
        self.assertIn(f"py::foo:{script}", perf_file_contents)
        self.assertIn(f"py::bar:{script}", perf_file_contents)
        self.assertIn(f"py::baz:{script}", perf_file_contents)
        self.assertIn(f"py::foo_fork:{script}", perf_file_contents)
        self.assertIn(f"py::bar_fork:{script}", perf_file_contents)
        self.assertIn(f"py::baz_fork:{script}", perf_file_contents)

        child_perf_file_contents = perf_child_file.read_text()
        self.assertIn(f"py::foo_fork:{script}", child_perf_file_contents)
        self.assertIn(f"py::bar_fork:{script}", child_perf_file_contents)
        self.assertIn(f"py::baz_fork:{script}", child_perf_file_contents)

        # For pre-compiled entries, the entries of a forked process should
        # appear exactly the same in both the parent and child processes.
        perf_file_lines = perf_file_contents.split("\n")
        for line in perf_file_lines:
            if (
                f"py::foo_fork:{script}" in line
                or f"py::bar_fork:{script}" in line
                or f"py::baz_fork:{script}" in line
            ):
                self.assertIn(line, child_perf_file_contents)

    def test_trampoline_works_with_gced_functions(self):
        # This tests that functions which are GC'd before the fork get cleared from
        # the list of functions to compile a trampoline for.

        code = """if 1:
                import os
                import gc
                from cinder import _compile_perf_trampoline_pre_fork

                def baz_fork():
                    pass

                def baz():
                    pass

                if __name__ == "__main__":

                    def tmp_fn():
                        pass

                    # ensure this is registered with the JIT
                    tmp_fn()

                    # ensure it's GC'd
                    del tmp_fn
                    gc.collect()

                    _compile_perf_trampoline_pre_fork()
                    pid = os.fork()
                    if pid == 0:
                        print(os.getpid())
                        baz_fork()
                    else:
                        baz()
                """
        rc, out, err = assert_python_ok("-c", code)
        with temp_dir() as script_dir:
            script = make_script(script_dir, "perftest", code)
            with subprocess.Popen(
                [
                    sys.executable,
                    "-X",
                    "perf-trampoline-prefork-compilation",
                    "-X",
                    "perf",
                    script,
                ],
                universal_newlines=True,
                stderr=subprocess.PIPE,
                stdout=subprocess.PIPE,
            ) as process:
                stdout, stderr = process.communicate()
                self.assertNotIn("Error:", stderr)
                self.assertEqual(process.returncode, 0)
