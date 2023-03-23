import self
if not self._lazy_imports:
    self.skipTest("Test relevant only when running with lazy imports enabled")

from .elements import elements_function

def __go(lcls):
    global __all__

    __all__ = sorted(
        name
        for name, obj in lcls.items()
        if not name.startswith("_")
    )

self.assertNotIn("elements_sub", globals())

__go(locals())

self.assertIn("elements_sub", globals())

# This test should not raise an exception
