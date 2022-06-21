import unittest

from .common import ReadonlyTestBase


class JumpIfTests(ReadonlyTestBase):
    @unittest.skipUnlessReadonly()
    def test_readonly_bool_error(self) -> None:
        code = """
class A:
    def __bool__(self):
        return True

@readonly_func
def f():
    abcdef = readonly(A())

    if abcdef:
        pass
    return 0
        """
        with self.assertImmutableErrors(
            [
                (
                    13,
                    "Attempted to pass a readonly arguments to an operation that expects mutable parameters.",
                    (),
                )
            ]
        ):
            self._compile_and_run(code, "f")

    @unittest.skipUnlessReadonly()
    def test_readonly_bool_no_error(self) -> None:
        code = """
class A:
    @readonly_func
    def __bool__(self: Readonly[object]):
        return True

@readonly_func
def f():
    abcdef = readonly(A())

    if abcdef:
        pass

    return 0
        """
        with self.assertNoImmutableErrors():
            self._compile_and_run(code, "f")

    def _compile_and_run(
        self, code: str, func: str, future_annotations: bool = True
    ) -> None:
        f = self.compile_and_run(code, future_annotations)[func]
        return f()
