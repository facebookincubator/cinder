from __future__ import annotations

import os.path
from contextlib import contextmanager
from typing import Callable, Generator, Mapping, Optional, Type, final

from strict_modules.compiler.compiler import STUB_ROOT, STUB_SUFFIX
from strict_modules.strictified_modules import get_implicit_source
from testing.unittest import UnitTest


@final
class CustomMatch:
    def __init__(self, matcher: Callable[[UnitTest, object], None]) -> None:
        self.matcher = matcher


@final
class ExceptionMatch:
    def __init__(
        self,
        exc_type: Type[Exception],
        cause: Optional[ExceptionMatch] = None,
        **kwargs: object,
    ) -> None:
        self.exc_type = exc_type
        self.attrs: Mapping[str, object] = kwargs
        self.cause = cause

    def check(self, tc: UnitTest, exc: Optional[BaseException]) -> None:
        tc.assertIsInstance(exc, self.exc_type)
        for name, val in self.attrs.items():
            if isinstance(val, CustomMatch):
                val.matcher(tc, getattr(exc, name))
            else:
                tc.assertEqual(getattr(exc, name), val)
        cause = self.cause
        if cause is not None:
            cause.check(tc, exc.__cause__ if exc is not None else None)


class StrictModuleTest(UnitTest):
    Match = ExceptionMatch

    @contextmanager
    def assertExceptionMatch(
        self,
        match: ExceptionMatch | Type[Exception],
        cause: Optional[ExceptionMatch] = None,
        **kwargs: object,
    ) -> Generator[None, None, None]:
        if not isinstance(match, ExceptionMatch):
            match = ExceptionMatch(match, cause, **kwargs)
        elif cause or kwargs:
            raise TypeError(
                "Cannot call assertExceptionMatch with an ExceptionMatch instance and additional arguments."
            )
        with self.assertRaises(match.exc_type) as cm:
            yield
        match.check(self, cm.exception)

    def matchEndswith(self, ending: str) -> CustomMatch:
        return CustomMatch(lambda tc, val: tc.assertTrue(val.endswith(ending)))

    def matchIsInstance(self, val_type: Type[object]) -> CustomMatch:
        return CustomMatch(lambda tc, val: tc.assertIsInstance(val, val_type))


def get_implicit_source_from_file(module_name: str, remove_import: bool = False) -> str:
    file_name = module_name.replace(".", os.path.sep)
    path_nosuffix = os.path.join(STUB_ROOT, file_name)
    path = path_nosuffix + STUB_SUFFIX
    if not os.path.isfile(path):
        path = os.path.join(path_nosuffix, "__init__") + STUB_SUFFIX
    with open(path) as f:
        src = f.read()
    return get_implicit_source(src, module_name, remove_import)
