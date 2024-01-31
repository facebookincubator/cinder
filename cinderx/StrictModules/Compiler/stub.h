// Copyright (c) Meta Platforms, Inc. and affiliates.
#pragma once
#include "cinderx/StrictModules/Compiler/module_info.h"
namespace strictmod::compiler {
class ModuleLoader;

/** Get module info of a stub file with all implicit markers expanded

e.g.
# in stub file x.pys
y = 1
@implicit
def f(): pass

# in source file x.py
def f(x):
    return x + 1

The module info will contain:
y = 1
def f(x):
    return x + 1


If module info cannot be found, return nullptr
*/
std::unique_ptr<ModuleInfo> getStubModuleInfo(
    std::unique_ptr<ModuleInfo> info,
    ModuleLoader* loader);
} // namespace strictmod::compiler
