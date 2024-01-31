# Verify that gdb can pretty-print the various PyObject* types
#
# The code for testing gdb was adapted from similar work in Unladen Swallow's
# Lib/test/test_jit_gdb.py

import os
import platform
import re
import subprocess
import sys
import sysconfig
import textwrap
import unittest

from test import support
from test.support import findfile, python_is_optimized

def get_gdb_version():
    try:
        cmd = ["gdb", "-nx", "--version"]
        proc = subprocess.Popen(cmd,
                                stdout=subprocess.PIPE,
                                stderr=subprocess.PIPE,
                                universal_newlines=True)
        with proc:
            version, stderr = proc.communicate()

        if proc.returncode:
            raise Exception(f"Command {' '.join(cmd)!r} failed "
                            f"with exit code {proc.returncode}: "
                            f"stdout={version!r} stderr={stderr!r}")
    except OSError:
        # This is what "no gdb" looks like.  There may, however, be other
        # errors that manifest this way too.
        raise unittest.SkipTest("Couldn't find gdb on the path")

    # Regex to parse:
    # 'GNU gdb (GDB; SUSE Linux Enterprise 12) 7.7\n' -> 7.7
    # 'GNU gdb (GDB) Fedora 7.9.1-17.fc22\n' -> 7.9
    # 'GNU gdb 6.1.1 [FreeBSD]\n' -> 6.1
    # 'GNU gdb (GDB) Fedora (7.5.1-37.fc18)\n' -> 7.5
    # 'HP gdb 6.7 for HP Itanium (32 or 64 bit) and target HP-UX 11iv2 and 11iv3.\n' -> 6.7
    match = re.search(r"^(?:GNU|HP) gdb.*?\b(\d+)\.(\d+)", version)
    if match is None:
        raise Exception("unable to parse GDB version: %r" % version)
    return (version, int(match.group(1)), int(match.group(2)))

gdb_version, gdb_major_version, gdb_minor_version = get_gdb_version()
if gdb_major_version < 7:
    raise unittest.SkipTest("gdb versions before 7.0 didn't support python "
                            "embedding. Saw %s.%s:\n%s"
                            % (gdb_major_version, gdb_minor_version,
                               gdb_version))

if not sysconfig.is_python_build():
    raise unittest.SkipTest("test_gdb only works on source builds at the moment.")

if 'Clang' in platform.python_compiler() and sys.platform == 'darwin':
    raise unittest.SkipTest("test_gdb doesn't work correctly when python is"
                            " built with LLVM clang")

if ((sysconfig.get_config_var('PGO_PROF_USE_FLAG') or 'xxx') in
    (sysconfig.get_config_var('PY_CORE_CFLAGS') or '')):
    raise unittest.SkipTest("test_gdb is not reliable on PGO builds")

# Location of custom hooks file in a repository checkout.
checkout_hook_path = os.path.join(os.path.dirname(sys.executable),
                                  'python-gdb.py')

PYTHONHASHSEED = '123'


def cet_protection():
    cflags = sysconfig.get_config_var('CFLAGS')
    if not cflags:
        return False
    flags = cflags.split()
    # True if "-mcet -fcf-protection" options are found, but false
    # if "-fcf-protection=none" or "-fcf-protection=return" is found.
    return (('-mcet' in flags)
            and any((flag.startswith('-fcf-protection')
                     and not flag.endswith(("=none", "=return")))
                    for flag in flags))

# Control-flow enforcement technology
CET_PROTECTION = cet_protection()


def run_gdb(*args, **env_vars):
    """Runs gdb in --batch mode with the additional arguments given by *args.

    Returns its (stdout, stderr) decoded from utf-8 using the replace handler.
    """
    if env_vars:
        env = os.environ.copy()
        env.update(env_vars)
    else:
        env = None
    # -nx: Do not execute commands from any .gdbinit initialization files
    #      (issue #22188)
    base_cmd = ('gdb', '--batch', '-nx')
    if (gdb_major_version, gdb_minor_version) >= (7, 4):
        base_cmd += ('-iex', 'add-auto-load-safe-path ' + checkout_hook_path)
    proc = subprocess.Popen(base_cmd + args,
                            # Redirect stdin to prevent GDB from messing with
                            # the terminal settings
                            stdin=subprocess.PIPE,
                            stdout=subprocess.PIPE,
                            stderr=subprocess.PIPE,
                            env=env)
    with proc:
        out, err = proc.communicate()
    return out.decode('utf-8', 'replace'), err.decode('utf-8', 'replace')

# Verify that "gdb" was built with the embedded python support enabled:
gdbpy_version, _ = run_gdb("--eval-command=python import sys; print(sys.version_info)")
if not gdbpy_version:
    raise unittest.SkipTest("gdb not built with embedded python support")

# Verify that "gdb" can load our custom hooks, as OS security settings may
# disallow this without a customized .gdbinit.
_, gdbpy_errors = run_gdb('--args', sys.executable)
if "auto-loading has been declined" in gdbpy_errors:
    msg = "gdb security settings prevent use of custom hooks: "
    raise unittest.SkipTest(msg + gdbpy_errors.rstrip())

BREAKPOINT_FN='builtin_id'

# CinderX: This is unchanged, kept as the parent class of CinderX_PrettyPrintTests
@unittest.skipIf(support.PGO, "not useful for PGO")
class CinderX_DebuggerTests(unittest.TestCase):
    """Test that the debugger can debug Python."""

    def get_stack_trace(self, source=None, script=None,
                        breakpoint=BREAKPOINT_FN,
                        cmds_after_breakpoint=None,
                        import_site=False,
                        ignore_stderr=False):
        '''
        Run 'python -c SOURCE' under gdb with a breakpoint.

        Support injecting commands after the breakpoint is reached

        Returns the stdout from gdb

        cmds_after_breakpoint: if provided, a list of strings: gdb commands
        '''
        # We use "set breakpoint pending yes" to avoid blocking with a:
        #   Function "foo" not defined.
        #   Make breakpoint pending on future shared library load? (y or [n])
        # error, which typically happens python is dynamically linked (the
        # breakpoints of interest are to be found in the shared library)
        # When this happens, we still get:
        #   Function "textiowrapper_write" not defined.
        # emitted to stderr each time, alas.

        # Initially I had "--eval-command=continue" here, but removed it to
        # avoid repeated print breakpoints when traversing hierarchical data
        # structures

        # Generate a list of commands in gdb's language:
        commands = ['set breakpoint pending yes',
                    'break %s' % breakpoint,

                    # The tests assume that the first frame of printed
                    #  backtrace will not contain program counter,
                    #  that is however not guaranteed by gdb
                    #  therefore we need to use 'set print address off' to
                    #  make sure the counter is not there. For example:
                    # #0 in PyObject_Print ...
                    #  is assumed, but sometimes this can be e.g.
                    # #0 0x00003fffb7dd1798 in PyObject_Print ...
                    'set print address off',

                    'run']

        # GDB as of 7.4 onwards can distinguish between the
        # value of a variable at entry vs current value:
        #   http://sourceware.org/gdb/onlinedocs/gdb/Variables.html
        # which leads to the selftests failing with errors like this:
        #   AssertionError: 'v@entry=()' != '()'
        # Disable this:
        if (gdb_major_version, gdb_minor_version) >= (7, 4):
            commands += ['set print entry-values no']

        if cmds_after_breakpoint:
            if CET_PROTECTION:
                # bpo-32962: When Python is compiled with -mcet
                # -fcf-protection, function arguments are unusable before
                # running the first instruction of the function entry point.
                # The 'next' command makes the required first step.
                commands += ['next']
            commands += cmds_after_breakpoint
        else:
            commands += ['backtrace']

        # print commands

        # Use "commands" to generate the arguments with which to invoke "gdb":
        args = ['--eval-command=%s' % cmd for cmd in commands]
        args += ["--args",
                 sys.executable]
        args.extend(subprocess._args_from_interpreter_flags())

        if not import_site:
            # -S suppresses the default 'import site'
            args += ["-S"]

        if source:
            args += ["-c", source]
        elif script:
            args += [script]

        # Use "args" to invoke gdb, capturing stdout, stderr:
        out, err = run_gdb(*args, PYTHONHASHSEED=PYTHONHASHSEED)

        if not ignore_stderr:
            for line in err.splitlines():
                print(line, file=sys.stderr)

        # bpo-34007: Sometimes some versions of the shared libraries that
        # are part of the traceback are compiled in optimised mode and the
        # Program Counter (PC) is not present, not allowing gdb to walk the
        # frames back. When this happens, the Python bindings of gdb raise
        # an exception, making the test impossible to succeed.
        if "PC not saved" in err:
            raise unittest.SkipTest("gdb cannot walk the frame object"
                                    " because the Program Counter is"
                                    " not present")

        # bpo-40019: Skip the test if gdb failed to read debug information
        # because the Python binary is optimized.
        for pattern in (
            '(frame information optimized out)',
            'Unable to read information on python frame',
        ):
            if pattern in out:
                raise unittest.SkipTest(f"{pattern!r} found in gdb output")

        return out


class CinderX_PrettyPrintTests(CinderX_DebuggerTests):
    def test_frames(self):
        gdb_output = self.get_stack_trace('''
def foo(a, b, c):
    pass

foo(3, 4, 5)
id(foo.__code__)''',
                                          breakpoint='builtin_id',
                                          # CinderX: This is changed to have `->co_mutable` in the middle.
                                          cmds_after_breakpoint=['print (PyFrameObject*)(((PyCodeObject*)v)->co_mutable->co_zombieframe)']
                                          )
        self.assertTrue(re.match(r'.*\s+\$1 =\s+Frame 0x-?[0-9a-f]+, for file <string>, line 3, in foo \(\)\s+.*',
                                 gdb_output,
                                 re.DOTALL),
                        'Unexpected gdb representation: %r\n%s' % (gdb_output, gdb_output))

def setUpModule():
    if support.verbose:
        print("GDB version %s.%s:" % (gdb_major_version, gdb_minor_version))
        for line in gdb_version.splitlines():
            print(" " * 4 + line)


if __name__ == "__main__":
    unittest.main()
