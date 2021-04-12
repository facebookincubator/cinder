#include "gtest/gtest.h"

#include "fixtures.h"
#include "testutil.h"

#include "Jit/code_allocator.h"

class CodeAllocatorTest : public RuntimeTest {
 public:
  void SetUp() override {
    RuntimeTest::SetUp();
    code_allocator_ = CodeAllocator_New(4096);
    ASSERT_NE(code_allocator_, nullptr);
  }

  void TearDown() override {
    CodeAllocator_Free(code_allocator_);
    RuntimeTest::TearDown();
  }

  CodeAllocator* code_allocator_;
};

TEST_F(CodeAllocatorTest, SizeBeforeCode) {
  size_t s = 1;
  char* result;
  while ((result = (char*)CodeAllocator_Allocate(code_allocator_, s)) !=
         nullptr) {
    ASSERT_EQ(*((size_t*)(result - sizeof(size_t))), s);
    s++;
  }
}
