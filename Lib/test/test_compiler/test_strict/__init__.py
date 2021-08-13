from .test_compiler import CompilerTests, GetModuleKindTest
from .test_loader import StrictLoaderTest, StrictLoaderInstallTest
from .test_ownership import OwnershipTests
from .test_remove_annotations import AnnotationRemoverTests
from .test_rewriter import (
    ImmutableModuleTestCase,
    RewriterTestCase,
    SlotificationTestCase,
    LazyLoadingTestCases,
)
from .test_strict_codegen import StrictCompilationTests, StrictCheckedCompilationTests
