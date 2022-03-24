#include "RuntimeTests/fixtures.h"

#include "Jit/profile_data.h"

#include <sstream>

void HIRTest::TestBody() {
  using namespace jit::hir;

  if (use_profile_data_) {
    // Run the test body once with profiling enabled, save the resulting
    // profile data, then tear down and set up again for a normal HIR test with
    // the profile data.
    _PyJIT_Disable();
    _PyThreadState_SetProfileInterpAll(1);
    if (compile_static_) {
      ASSERT_TRUE(runStaticCode(src_.c_str()));
    } else {
      ASSERT_TRUE(runCode(src_.c_str()));
    }
    std::stringstream data;
    ASSERT_TRUE(jit::writeProfileData(data));
    data.seekg(0);

    TearDown();
    SetUp();
    jit::readProfileData(data);
  }

  std::unique_ptr<Function> irfunc;
  if (src_is_hir_) {
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

  if (!passes_.empty()) {
    if (!src_is_hir_) {
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
