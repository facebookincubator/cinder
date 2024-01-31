// Copyright (c) Meta Platforms, Inc. and affiliates.
#include "cinderx/StrictModules/Tests/test.h"
TEST_F(ModuleLoaderTest, GetLoader) {
  auto mod = getLoader(nullptr, nullptr);
  ASSERT_NE(mod.get(), nullptr);
}

TEST_F(ModuleLoaderTest, FindModuleEmpty) {
  auto modInfo = findModule("empty");
  ASSERT_NE(modInfo.get(), nullptr);
}

TEST_F(ModuleLoaderTest, FindModuleMissing) {
  auto modInfo = findModule("non existent file");
  ASSERT_EQ(modInfo.get(), nullptr);
}

TEST_F(ModuleLoaderTest, LoadSingleModuleEmpty) {
  auto mod = loadSingleFile("empty");
  ASSERT_NE(mod.get(), nullptr);
}

TEST_F(ModuleLoaderTest, LoadSingleModuleStub) {
  auto mod = loadSingleFile("simple_func");
  ASSERT_NE(mod.get(), nullptr);
}

TEST_F(ModuleLoaderTest, LoadSingleModuleMissing) {
  auto mod = loadSingleFile("non existent file");
  ASSERT_EQ(mod.get(), nullptr);
}

TEST_F(ModuleLoaderTest, LoadModuleEmpty) {
  auto mod = loadFile("empty");
  ASSERT_NE(mod.get(), nullptr);
}

TEST_F(ModuleLoaderTest, LoadModuleMISSING) {
  auto mod = loadFile("non existent file");
  ASSERT_EQ(mod.get(), nullptr);
}

TEST_F(ModuleLoaderTest, LoadModuleImport) {
  auto mod = loadFile("simple_import");
  ASSERT_NE(mod.get(), nullptr);
}
