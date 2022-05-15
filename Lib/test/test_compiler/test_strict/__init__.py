from .test_compiler import CompilerTests, GetModuleKindTest
from .test_definite_assignment import DefiniteAssignmentTests
from .test_loader import StrictLoaderInstallTest, StrictLoaderTest
from .test_ownership import OwnershipTests
from .test_remove_annotations import AnnotationRemoverTests
from .test_rewriter import (
    ImmutableModuleTestCase,
    LazyLoadingTestCases,
    RewriterTestCase,
    SlotificationTestCase,
)
from .test_strict_codegen import (
    StrictBuiltinCompilationTests,
    StrictCheckedCompilationTests,
    StrictCompilationTests,
)
