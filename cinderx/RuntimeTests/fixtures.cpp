#include "cinderx/RuntimeTests/fixtures.h"

#include "cinder/exports.h"

#include "cinderx/Jit/runtime.h"

#include <sstream>

void RuntimeTest::runAndProfileCode(const char* src) {
  // Disable the JIT temporarily so we get maximum coverage from the
  // interpreter.
  int jit_enabled = _PyJIT_IsEnabled();
  _PyJIT_Disable();
  Ci_ThreadState_SetProfileInterpAll(1);
  Ci_RuntimeState_SetProfileInterpPeriod(1);

  ASSERT_TRUE(compile_static_ ? runStaticCode(src) : runCode(src));

  Ci_ThreadState_SetProfileInterpAll(0);
  Ci_RuntimeState_SetProfileInterpPeriod(0);
  if (jit_enabled) {
    _PyJIT_Enable();
  }
}

std::unique_ptr<jit::hir::Function> RuntimeTest::buildHIR(
    BorrowedRef<PyFunctionObject> func) {
  if (!jit::preloadFuncAndDeps(func)) {
    return nullptr;
  }
  jit::hir::Preloader* preloader = jit::lookupPreloader(func);
  JIT_CHECK(preloader != nullptr, "Failed to find just-created preloader");
  return jit::hir::buildHIR(*preloader);
}

void HIRTest::TestBody() {
  using namespace jit::hir;

  std::string test_name = "<unknown>";
  const testing::TestInfo* info =
      testing::UnitTest::GetInstance()->current_test_info();
  if (info != nullptr) {
    test_name = fmt::format("{}:{}", info->test_suite_name(), info->name());
  }

  std::unique_ptr<Function> irfunc;
  if (use_profile_data_) {
    ASSERT_NO_FATAL_FAILURE(runAndProfileCode(src_.c_str()));
    Ref<PyFunctionObject> func = getGlobal("test");
    irfunc = buildHIR(func);
  } else if (src_is_hir_) {
    irfunc = HIRParser{}.ParseHIR(src_.c_str());
    ASSERT_FALSE(passes_.empty())
        << "HIR tests don't make sense without a pass to test";
    ASSERT_NE(irfunc, nullptr);
    ASSERT_TRUE(checkFunc(*irfunc, std::cout));
    reflowTypes(*irfunc);
  } else if (compile_static_) {
    ASSERT_NO_FATAL_FAILURE(CompileToHIRStatic(src_.c_str(), "test", irfunc));
  } else {
    ASSERT_NO_FATAL_FAILURE(CompileToHIR(src_.c_str(), "test", irfunc));
  }

  if (jit::g_dump_hir) {
    JIT_LOG("Initial HIR for {}:\n{}", test_name, *irfunc);
  }

  if (!passes_.empty()) {
    if (!src_is_hir_ &&
        !(passes_.size() == 1 &&
          std::string(passes_.at(0)->name()) == "@AllPasses")) {
      SSAify{}.Run(*irfunc);
      // Perform some straightforward cleanup on Python inputs to make the
      // output more reasonable. This implies that tests for the passes used
      // here are most useful as HIR-only tests.
      Simplify{}.Run(*irfunc);
      PhiElimination{}.Run(*irfunc);
    }
    for (auto& pass : passes_) {
      pass->Run(*irfunc);
    }
    ASSERT_TRUE(checkFunc(*irfunc, std::cout));
  }
  HIRPrinter printer;
  auto hir = printer.ToString(*irfunc.get());
  EXPECT_EQ(hir, expected_hir_);
}

void HIRJSONTest::TestBody() {
  using namespace jit::hir;

  std::unique_ptr<Function> irfunc;
  irfunc = HIRParser{}.ParseHIR(src_.c_str());
  ASSERT_NE(irfunc, nullptr);
  ASSERT_TRUE(checkFunc(*irfunc, std::cout));
  reflowTypes(*irfunc);

  nlohmann::json expected_json_obj;
  try {
    expected_json_obj = nlohmann::json::parse(expected_json_);
  } catch (nlohmann::json::exception&) {
    ASSERT_TRUE(false) << "Could not parse JSON input";
  }

  JSONPrinter printer;
  nlohmann::json result;
  printer.Print(result, *irfunc.get(), "Test", 0);
  EXPECT_EQ(result, expected_json_obj);
}
