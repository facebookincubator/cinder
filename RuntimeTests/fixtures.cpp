#include "RuntimeTests/fixtures.h"

#include "cinder/exports.h"

#include "Jit/profile_data.h"

#include <sstream>

void RuntimeTest::runCodeAndCollectProfile(
    const char* src,
    std::string& output) {
  // Run the test body once with profiling enabled, save the resulting
  // profile data, then tear down and set up again for a normal HIR test with
  // the profile data.
  int jit_enabled = _PyJIT_IsEnabled();
  _PyJIT_Disable();
  Ci_ThreadState_SetProfileInterpAll(1);
  if (compile_static_) {
    ASSERT_TRUE(runStaticCode(src));
  } else {
    ASSERT_TRUE(runCode(src));
  }
  std::stringstream data;
  ASSERT_TRUE(jit::writeProfileData(data));
  output = data.str();

  TearDown();
  SetUp();
  if (jit_enabled) {
    _PyJIT_Enable();
  }
}

void HIRTest::TestBody() {
  using namespace jit::hir;

  std::string test_name = "<unknown>";
  const testing::TestInfo* info =
      testing::UnitTest::GetInstance()->current_test_info();
  if (info != nullptr) {
    test_name = fmt::format("{}:{}", info->test_suite_name(), info->name());
  }

  if (use_profile_data_) {
    std::string data;
    ASSERT_NO_FATAL_FAILURE(runCodeAndCollectProfile(src_.c_str(), data));
    std::istringstream stream(data);
    ASSERT_TRUE(jit::readProfileData(stream));
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

  if (jit::g_dump_hir) {
    JIT_LOG("Initial HIR for %s:\n%s", test_name, *irfunc);
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
