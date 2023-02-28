// Copyright (c) Facebook, Inc. and its affiliates. (http://www.facebook.com)

#include "StrictModules/Tests/test.h"
#include "StrictModules/parser_util.h"

class ParserTest : public PythonTest {};

TEST_F(ParserTest, CanParseByFilename) {
  const char* name = "StrictModules/Tests/python_tests/simple_assign.py";
  PyArena* arena = _PyArena_New();

  std::optional<strictmod::AstAndSymbols> result =
      strictmod::readFromFile(name, arena, {});
  ASSERT_NE(result, std::nullopt);
  EXPECT_NE(result.value().ast, nullptr);
  EXPECT_NE(result.value().symbols, nullptr);
  EXPECT_EQ(result.value().futureAnnotations, false);
  if (arena != nullptr) {
    _PyArena_Free(arena);
  }
}

TEST_F(ParserTest, CanHandleNonExist) {
  const char* name = "non exist file";
  PyArena* arena = _PyArena_New();

  std::optional<strictmod::AstAndSymbols> result =
      strictmod::readFromFile(name, arena, {});
  ASSERT_EQ(result, std::nullopt);
  if (arena != nullptr) {
    _PyArena_Free(arena);
  }
}

TEST_F(ParserTest, CanParseSource) {
  const char* s = "import foo\nx=1";
  PyArena* arena = _PyArena_New();

  std::optional<strictmod::AstAndSymbols> result =
      strictmod::readFromSource(s, "<string>", Py_file_input, arena);
  ASSERT_NE(result, std::nullopt);
  EXPECT_NE(result.value().ast, nullptr);
  EXPECT_NE(result.value().symbols, nullptr);
  EXPECT_EQ(result.value().futureAnnotations, false);
  if (arena != nullptr) {
    _PyArena_Free(arena);
  }
}

TEST_F(ParserTest, CanParseFuture) {
  const char* s = "from __future__ import annotations\nx: int = 1";
  PyArena* arena = _PyArena_New();

  std::optional<strictmod::AstAndSymbols> result =
      strictmod::readFromSource(s, "<string>", Py_file_input, arena);
  ASSERT_NE(result, std::nullopt);
  EXPECT_NE(result.value().ast, nullptr);
  EXPECT_NE(result.value().symbols, nullptr);
  EXPECT_EQ(result.value().futureAnnotations, true);
  if (arena != nullptr) {
    _PyArena_Free(arena);
  }
}
