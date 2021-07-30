// Copyright (c) Facebook, Inc. and its affiliates. (http://www.facebook.com)
#include "StrictModules/Tests/test.h"
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

TEST_F(ModuleLoaderTest, ASTPreprocessLooseSlots) {
  std::unique_ptr<strictmod::compiler::ModuleLoader> loader = getLoader("", "");
  loader->loadStrictModuleModule();
  std::string source =
      "import __strict__\n"
      "from __strict__ import loose_slots\n"
      "@loose_slots\n"
      "class C:\n"
      "    pass\n";

  std::string astStrPreprocessedExpected =
      "Module(body=["
      "Import(names=[alias(name='__strict__', asname=None)]), "
      "ImportFrom(module='__strict__', "
      "names=[alias(name='loose_slots', asname=None)], level=0), "
      "ClassDef(name='C', bases=[], keywords=[], body=[Pass()], "
      "decorator_list=[Name(id='loose_slots', ctx=Load()), "
      "Name(id='<loose_slots>', ctx=Load()), Name(id='<enable_slots>', "
      "ctx=Load())"
      "])], type_ignores=[])";

  Ref<> astStr = getPreprocessedASTDump(source, "m", "m.py");

  std::string preprocessedStr = PyUnicode_AsUTF8(astStr);
  ASSERT_EQ(astStrPreprocessedExpected, preprocessedStr);
}

TEST_F(ModuleLoaderTest, ASTPreprocessMutable) {
  std::unique_ptr<strictmod::compiler::ModuleLoader> loader = getLoader("", "");
  loader->loadStrictModuleModule();
  std::string source =
      "import __strict__\n"
      "from __strict__ import mutable\n"
      "@mutable\n"
      "class C:\n"
      "    pass\n";

  std::string astStrPreprocessedExpected =
      "ClassDef(name='C', bases=[], keywords=[], "
      "body=[Pass()], decorator_list=[Name(id='mutable', ctx=Load()), "
      "Name(id='<mutable>', ctx=Load())])], type_ignores=[])";

  Ref<> astStr = getPreprocessedASTDump(source, "m", "m.py");

  std::string preprocessedStr = PyUnicode_AsUTF8(astStr);
  std::size_t found = preprocessedStr.find(astStrPreprocessedExpected);
  ASSERT_NE(found, std::string::npos);
}

TEST_F(ModuleLoaderTest, ASTPreprocessStrictSlots) {
  std::unique_ptr<strictmod::compiler::ModuleLoader> loader = getLoader("", "");
  loader->loadStrictModuleModule();
  std::string source =
      "import __strict__\n"
      "from __strict__ import strict_slots\n"
      "@strict_slots\n"
      "class C:\n"
      "    pass\n";

  std::string astStrPreprocessedExpected =
      "Module(body=["
      "Import(names=[alias(name='__strict__', asname=None)]), "
      "ImportFrom(module='__strict__', "
      "names=[alias(name='strict_slots', asname=None)], level=0), "
      "ClassDef(name='C', bases=[], keywords=[], body=[Pass()], "
      "decorator_list=[Name(id='strict_slots', ctx=Load()), "
      "Name(id='<enable_slots>', ctx=Load())"
      "])], type_ignores=[])";

  Ref<> astStr = getPreprocessedASTDump(source, "m", "m.py");

  std::string preprocessedStr = PyUnicode_AsUTF8(astStr);
  ASSERT_EQ(astStrPreprocessedExpected, preprocessedStr);
}

TEST_F(ModuleLoaderTest, ASTPreprocessExtraSlots) {
  std::unique_ptr<strictmod::compiler::ModuleLoader> loader = getLoader("", "");
  loader->loadStrictModuleModule();
  std::string source =
      "import __strict__\n"
      "from __strict__ import extra_slot\n"
      "class C:\n"
      "    pass\n"
      "extra_slot(C, 'a')\n"
      "extra_slot(C, 'b')";

  std::string astStrPreprocessedExpected =
      "ClassDef(name='C', bases=[], keywords=[], "
      "body=[Pass()], decorator_list=[Call(func=Name(id='<extra_slots>', "
      "ctx=Load()), args=[Constant(value='a', kind=None), Constant(value='b', "
      "kind=None)], keywords=[]), Name(id='<enable_slots>', ctx=Load())]), ";

  Ref<> astStr = getPreprocessedASTDump(source, "m", "m.py");

  std::string preprocessedStr = PyUnicode_AsUTF8(astStr);
  std::size_t found = preprocessedStr.find(astStrPreprocessedExpected);
  ASSERT_NE(found, std::string::npos);
}

TEST_F(ModuleLoaderTest, ASTPreprocessCachedProperty) {
  std::unique_ptr<strictmod::compiler::ModuleLoader> loader = getLoader("", "");
  loader->loadStrictModuleModule();
  std::string source =
      "import __strict__\n"
      "from __strict__ import strict_slots, _mark_cached_property\n"
      "def dec(f):\n"
      "    _mark_cached_property(f, False, dec)\n"
      "    return f\n"
      "@strict_slots\n"
      "class C:\n"
      "    @dec\n"
      "    def p(self):\n"
      "        return 42";

  std::string astStrPreprocessedExpected =
      "ClassDef(name='C', bases=[], keywords=[], body=[FunctionDef(name='p', "
      "args=arguments(posonlyargs=[], "
      "args=[arg(arg='self', annotation=None, type_comment=None)], "
      "vararg=None, kwonlyargs=[], kw_defaults=[], kwarg=None, defaults=[]), "
      "body=[Return(value=Constant(value=42, kind=None))], "
      "decorator_list=[Call(func=Name(id='<cached_property>', ctx=Load()), "
      "args=[Constant(value=False, kind=None)], keywords=[])], returns=None, "
      "type_comment=None)], decorator_list=[Name(id='strict_slots', "
      "ctx=Load()), Name(id='<enable_slots>', ctx=Load())])], type_ignores=[])";

  Ref<> astStr = getPreprocessedASTDump(source, "m", "m.py");

  std::string preprocessedStr = PyUnicode_AsUTF8(astStr);
  std::size_t found = preprocessedStr.find(astStrPreprocessedExpected);
  ASSERT_NE(found, std::string::npos);
}
