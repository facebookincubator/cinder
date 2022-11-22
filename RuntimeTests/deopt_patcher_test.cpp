// Copyright (c) Facebook, Inc. and its affiliates. (http://www.facebook.com)
#include <gtest/gtest.h>

#include "Jit/codegen/gen_asm.h"
#include "Jit/compiler.h"
#include "Jit/hir/builder.h"
#include "Jit/hir/hir.h"
#include "Jit/hir/optimization.h"
#include "Jit/hir/printer.h"

#include "RuntimeTests/fixtures.h"
#include "RuntimeTests/testutil.h"

class DeoptPatcherTest : public RuntimeTest {
 public:
  std::unique_ptr<jit::CompiledFunction> generateCode(
      jit::codegen::NativeGenerator& ngen) {
    auto entry = ngen.getVectorcallEntry();
    if (entry == nullptr) {
      return nullptr;
    }
    int func_size = ngen.GetCompiledFunctionSize();
    int stack_size = ngen.GetCompiledFunctionStackSize();
    int spill_stack_size = ngen.GetCompiledFunctionSpillStackSize();
    return std::make_unique<jit::CompiledFunction>(
        reinterpret_cast<vectorcallfunc>(entry),
        ngen.getStaticEntry(),
        ngen.codeRuntime(),
        func_size,
        stack_size,
        spill_stack_size,
        /*num_inlined_functions=*/0);
  }

 protected:
  asmjit::JitRuntime rt_;
};

class MyDeoptPatcher : public jit::DeoptPatcher {
 public:
  explicit MyDeoptPatcher(int id) : id_(id) {}

  void init() override {
    initialized_ = true;
  }

  bool isInitialized() const {
    return initialized_;
  }

  int id() const {
    return id_;
  }

 private:
  int id_{-1};
  bool initialized_{false};
};

TEST_F(DeoptPatcherTest, Patch) {
  const char* pycode = R"(
def func():
  a = 314159
  return a
)";

  Ref<PyFunctionObject> pyfunc(compileAndGet(pycode, "func"));
  ASSERT_NE(pyfunc, nullptr);

  auto irfunc = jit::hir::buildHIR(pyfunc);
  ASSERT_NE(irfunc, nullptr);

  jit::hir::Instr* term = irfunc->cfg.entry_block->GetTerminator();
  ASSERT_NE(term, nullptr);
  ASSERT_TRUE(term->IsReturn());

  // Insert a patchpoint immediately before the return
  jit::Runtime* jit_rt = jit::Runtime::get();
  auto patcher = jit_rt->allocateDeoptPatcher<MyDeoptPatcher>(123);
  EXPECT_EQ(patcher->id(), 123);
  auto patchpoint = jit::hir::DeoptPatchpoint::create(patcher);
  patchpoint->InsertBefore(*term);

  // Generate machine code and link the patcher
  jit::Compiler::runPasses(*irfunc, jit::PassConfig::kDefault);
  jit::codegen::NativeGenerator ngen(irfunc.get());
  auto jitfunc = generateCode(ngen);
  ASSERT_NE(jitfunc, nullptr);
  EXPECT_TRUE(patcher->isInitialized());

  // Make sure things work in the nominal case
  bool did_deopt = false;
  auto callback = [&did_deopt](const jit::DeoptMetadata&) { did_deopt = true; };
  jit_rt->setGuardFailureCallback(callback);
  auto res = Ref<>::steal(jitfunc->Invoke(pyfunc, nullptr, 0));
  ASSERT_NE(res, nullptr);
  ASSERT_EQ(PyLong_AsLong(res), 314159);
  EXPECT_FALSE(did_deopt);

  // Patch and verify that a deopt occurred
  patcher->patch();
  auto res2 = Ref<>::steal(jitfunc->Invoke(pyfunc, nullptr, 0));
  jit_rt->clearGuardFailureCallback();
  ASSERT_NE(res2, nullptr);
  ASSERT_EQ(PyLong_AsLong(res2), 314159);
  EXPECT_TRUE(did_deopt);
}
