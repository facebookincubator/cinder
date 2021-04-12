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

from .pycodegen import compile, compileFile
from .visitor import walk

__all__ = ("compile", "compileFile", "walk")
