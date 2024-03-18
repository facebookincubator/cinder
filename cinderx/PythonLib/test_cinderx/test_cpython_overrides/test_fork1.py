"""This test checks for correct fork() behavior.
"""

import _imp as imp
import os
import signal
import sys
import threading
import time
import unittest

from test import support

from test.fork_wait import ForkWait


# Skip test if fork does not exist.
support.get_attribute(os, "fork")


class CinderX_ForkTest(ForkWait):
    def test_threaded_import_lock_fork(self):
        """Check fork() in main thread works while a subthread is doing an import"""
        import_started = threading.Event()
        fake_module_name = "fake test module"
        partial_module = "partial"
        complete_module = "complete"
        def importer():
            imp.acquire_lock()

            sys.modules[fake_module_name] = partial_module
            import_started.set()
            # CinderX: This time.sleep is added to improve test reliability.
            time.sleep(0.01)  # Give the other thread time to try and acquire.
            sys.modules[fake_module_name] = complete_module
            imp.release_lock()
        t = threading.Thread(target=importer)
        t.start()
        import_started.wait()

        exitcode = 42
        pid = os.fork()
        try:
            # PyOS_BeforeFork should have waited for the import to complete
            # before forking, so the child can recreate the import lock
            # correctly, but also won't see a partially initialised module
            if not pid:
                m = __import__(fake_module_name)
                if m == complete_module:
                    os._exit(exitcode)
                else:
                    if support.verbose > 1:
                        print("Child encountered partial module")
                    os._exit(1)
            else:
                t.join()
                # Exitcode 1 means the child got a partial module (bad.) No
                # exitcode (but a hang, which manifests as 'got pid 0')
                # means the child deadlocked (also bad.)
                self.wait_impl(pid, exitcode=exitcode)
        finally:
            try:
                os.kill(pid, signal.SIGKILL)
            except OSError:
                pass

def tearDownModule():
    support.reap_children()

if __name__ == "__main__":
    unittest.main()
