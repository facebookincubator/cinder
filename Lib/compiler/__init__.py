# Portions copyright (c) Meta Platforms, Inc. and affiliates. (http://www.meta.com)

"""Package for compiling Python source code

There are several functions defined at the top level that are imported
from modules contained in the package.

walk(ast, visitor, verbose=None)
    Does a pre-order walk over the ast using the visitor instance.
    See compiler.visitor for details.

compile(source, filename, mode, flags=None, dont_inherit=None)
    Returns a code object.  A replacement for the builtin compile() function.

compileFile(filename)
    Generates a .pyc file by compiling filename.
"""

from typing import Any, Dict, Union

from .pycodegen import CinderCodeGenerator, compile, compileFile
from .visitor import walk


# pyre-ignore[5]: Globally accessible variable `CodeType` has no type specified.
CodeType = type(compile.__code__)


def exec_cinder(
    source: Union[object, str],
    locals: Dict[str, Any],
    globals: Dict[str, Any],
    modname: str = "<module>",
) -> None:
    if isinstance(source, CodeType):
        code = source
    else:
        code = compile(
            source, "<module>", "exec", compiler=CinderCodeGenerator, modname=modname
        )
    exec(code, locals, globals)


__all__ = ("compile", "compileFile", "exec_cinder", "walk")
