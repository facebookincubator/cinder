#include <cstdlib>
#include <memory>
#include <string>

#include "gtest/gtest.h"

#include "fixtures.h"
#include "testutil.h"

#include "Jit/pyjit.h"

static const char* PYTHONJIT = "PYTHONJIT";

class PyJITTest : public RuntimeTest {
 public:
  void SetUp() override {
    RuntimeTest::SetUp();

    if (_PyJIT_IsEnabled()) {
      is_enabled = 1;
      _PyJIT_Finalize();
    }

    auto tmp_env = getenv(PYTHONJIT);
    if (tmp_env != nullptr) {
      _pythonjit_env = std::make_unique<std::string>(tmp_env);
    }
  }

  void TearDown() override {
    setenv(
        PYTHONJIT,
        _pythonjit_env == nullptr ? nullptr : _pythonjit_env->c_str(),
        1);

    if (is_enabled) {
      _PyJIT_Initialize();
    }

    RuntimeTest::TearDown();
  }

  int is_enabled;
  std::unique_ptr<std::string> _pythonjit_env;
};

TEST_F(PyJITTest, PyInitialization) {
  // test without the environment variable
  unsetenv(PYTHONJIT);
  auto result = _PyJIT_Initialize();
  ASSERT_EQ(result, 0);
  ASSERT_EQ(_PyJIT_IsEnabled(), 0);

  // test without the environment variable
  setenv(PYTHONJIT, "1", 1);
  result = _PyJIT_Initialize();
  ASSERT_EQ(result, 0);
  ASSERT_EQ(_PyJIT_IsEnabled(), 1);

  _PyJIT_Finalize();
}
