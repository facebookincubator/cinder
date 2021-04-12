import sys

from .test_api import ApiTests
from .test_code_sbs import CodeTests
from .test_corpus import SbsCorpusCompileTests
from .test_errors import ErrorTests, ErrorTestsBuiltin
from .test_flags import FlagTests
from .test_graph import GraphTests
from .test_optimizer import AstOptimizerTests
from .test_peephole import PeepHoleTests
from .test_sbs_stdlib import SbsCompileTests
from .test_symbols import SymbolVisitorTests
from .test_unparse import UnparseTests
from .test_visitor import VisitorTests

try:
    import cinder

    from .test_static import StaticCompilationTests, StaticRuntimeTests
except ImportError:
    pass


if sys.version_info >= (3, 7):
    from .test_py37 import Python37Tests

if sys.version_info >= (3, 8):
    from .test_py38 import Python38Tests
