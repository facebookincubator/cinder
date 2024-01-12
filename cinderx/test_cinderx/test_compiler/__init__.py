import sys

from .test_api import ApiTests
from .test_code_sbs import CodeTests
from .test_corpus import SbsCorpusCompileTests
from .test_errors import ErrorTests, ErrorTestsBuiltin
from .test_flags import FlagTests
from .test_graph import GraphTests
from .test_optimizer import AstOptimizerTests
from .test_py310 import Python310Tests
from .test_py37 import Python37Tests
from .test_py38 import Python38Tests
from .test_pysourceloader import PySourceLoaderTest
from .test_symbols import SymbolVisitorTests
from .test_unparse import UnparseTests
from .test_visitor import VisitorTests

if "cinder" in sys.version:
    from .test_static import *
    from .test_strict import *
