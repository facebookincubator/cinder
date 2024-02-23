// Copyright (c) Meta Platforms, Inc. and affiliates.
#include "cinderx/StrictModules/Compiler/stub.h"

#include "cinderx/Common/ref.h"
#include "cinderx/StrictModules/Compiler/abstract_module_loader.h"
#include "cinderx/StrictModules/pycore_dependencies.h"

#include <unordered_map>

namespace strictmod::compiler {
const std::string kImplicitMarker = "implicit";
const std::string kFullImplicitMarker = "__implicit__";

class CannotDecideSourceException {};

bool hasImplicitDecorator(asdl_expr_seq* decorators) {
  int size = asdl_seq_LEN(decorators);
  for (int i = 0; i < size; ++i) {
    expr_ty dec = reinterpret_cast<expr_ty>(asdl_seq_GET(decorators, i));

    switch (dec->kind) {
      case Name_kind: {
        auto name = dec->v.Name;
        std::string nameStr(PyUnicode_AsUTF8(name.id));
        if (nameStr == kImplicitMarker) {
          return true;
        }
        break;
      }
      default: {
        break;
      }
    }
  }
  return false;
}

void buildImplicitToLocHelper(
    stmt_ty s,
    int loc,
    std::unordered_map<std::string, int>& map) {
  std::string nodeName;
  switch (s->kind) {
    case FunctionDef_kind: {
      auto func = s->v.FunctionDef;
      if (hasImplicitDecorator(func.decorator_list)) {
        nodeName = PyUnicode_AsUTF8(func.name);
        map[nodeName] = loc;
      }
      break;
    }

    case ClassDef_kind: {
      auto cls = s->v.ClassDef;
      if (hasImplicitDecorator(cls.decorator_list)) {
        nodeName = PyUnicode_AsUTF8(cls.name);
        map[nodeName] = loc;
      }
      break;
    }
    case AsyncFunctionDef_kind: {
      auto func = s->v.AsyncFunctionDef;
      if (hasImplicitDecorator(func.decorator_list)) {
        nodeName = PyUnicode_AsUTF8(func.name);
        map[nodeName] = loc;
      }
      break;
    }
    default:
      return;
  }
}

/* maps name of the implicit function/class to their index in the
 * statement list
 */
std::unordered_map<std::string, int> getImplicitToLocHelper(mod_ty m) {
  switch (m->kind) {
    case Module_kind: {
      std::unordered_map<std::string, int> map;
      auto mod = m->v.Module;
      int bodySize = asdl_seq_LEN(mod.body);
      for (int i = 0; i < bodySize; ++i) {
        stmt_ty s = reinterpret_cast<stmt_ty>(asdl_seq_GET(mod.body, i));
        buildImplicitToLocHelper(s, i, map);
      }
      return map;
    }

    default:
      return {};
  }
}

void buildLocToSrcHelper(
    const std::string& name,
    stmt_ty s,
    const std::unordered_map<std::string, int>& locationMap,
    std::unordered_map<int, stmt_ty>& SourceMap) {
  auto it = locationMap.find(name);
  if (it != locationMap.end()) {
    auto duplicate = SourceMap.find(it->second);
    if (duplicate != SourceMap.end()) {
      // the same implicit node has duplicate definitions
      throw CannotDecideSourceException();
    }
    SourceMap[it->second] = s;
  }
}

void buildLocToSrcHelper(
    stmt_ty s,
    const std::unordered_map<std::string, int>& locationMap,
    std::unordered_map<int, stmt_ty>& SourceMap) {
  switch (s->kind) {
    case FunctionDef_kind: {
      std::string nodeName = PyUnicode_AsUTF8(s->v.FunctionDef.name);
      buildLocToSrcHelper(nodeName, s, locationMap, SourceMap);
      break;
    }
    case ClassDef_kind: {
      std::string nodeName = PyUnicode_AsUTF8(s->v.ClassDef.name);
      buildLocToSrcHelper(nodeName, s, locationMap, SourceMap);
      break;
    }
    case AsyncFunctionDef_kind: {
      std::string nodeName = PyUnicode_AsUTF8(s->v.AsyncFunctionDef.name);
      buildLocToSrcHelper(nodeName, s, locationMap, SourceMap);
      break;
    }
    case If_kind: {
      // visit both branches. If only one definition of a class/function
      // is found, we will use that one.
      auto ifStmt = s->v.If;
      int bSize = asdl_seq_LEN(ifStmt.body);
      int eSize = asdl_seq_LEN(ifStmt.orelse);
      for (int i = 0; i < bSize; ++i) {
        stmt_ty innerStmt =
            reinterpret_cast<stmt_ty>(asdl_seq_GET(ifStmt.body, i));
        buildLocToSrcHelper(innerStmt, locationMap, SourceMap);
      }
      for (int i = 0; i < eSize; ++i) {
        stmt_ty innerStmt =
            reinterpret_cast<stmt_ty>(asdl_seq_GET(ifStmt.orelse, i));
        buildLocToSrcHelper(innerStmt, locationMap, SourceMap);
      }
      break;
    }
    // if the desired name is assigned to, we give up on locating the source
    case AnnAssign_kind: {
      expr_ty target = s->v.AnnAssign.target;
      if (target->kind == Name_kind) {
        std::string name = PyUnicode_AsUTF8(target->v.Name.id);
        auto it = locationMap.find(name);
        if (it != locationMap.end()) {
          throw CannotDecideSourceException();
        }
      }
      break;
    }
    case Assign_kind: {
      asdl_expr_seq* targets = s->v.Assign.targets;
      int targetSize = asdl_seq_LEN(targets);
      for (int i = 0; i < targetSize; ++i) {
        expr_ty target = reinterpret_cast<expr_ty>(asdl_seq_GET(targets, i));
        if (target->kind == Name_kind) {
          std::string name = PyUnicode_AsUTF8(target->v.Name.id);
          auto it = locationMap.find(name);
          if (it != locationMap.end()) {
            throw CannotDecideSourceException();
          }
        }
      }
      break;
    }

    default:
      return;
  }
}

/* maps name of the implicit function/class to their actual source code
 */
std::unordered_map<int, stmt_ty> getLocToSrcHelper(
    const std::unordered_map<std::string, int>& locationMap,
    mod_ty m) {
  switch (m->kind) {
    case Module_kind: {
      std::unordered_map<int, stmt_ty> map;
      auto mod = m->v.Module;
      int bodySize = asdl_seq_LEN(mod.body);
      for (int i = 0; i < bodySize; ++i) {
        stmt_ty s = reinterpret_cast<stmt_ty>(asdl_seq_GET(mod.body, i));
        buildLocToSrcHelper(s, locationMap, map);
      }
      return map;
    }
    default:
      return {};
  }
}

/* update the stub AST with the actual source code */
void updateStubHelper(
    mod_ty stubMod,
    const std::unordered_map<int, stmt_ty>& srcMap) {
  switch (stubMod->kind) {
    case Module_kind: {
      asdl_stmt_seq* body = stubMod->v.Module.body;
      for (const auto& it : srcMap) {
        asdl_seq_SET(body, it.first, it.second);
      }
      break;
    }
    default:
      return;
  }
}

std::unique_ptr<ModuleInfo> getSourceModuleInfo(
    const std::string& modName,
    FileSuffixKind fileKind,
    ModuleLoader* loader) {
  return loader->findModule(modName, fileKind);
}

bool checkFullImplicitHelper(mod_ty m) {
  switch (m->kind) {
    case Module_kind: {
      auto mod = m->v.Module;
      int bodySize = asdl_seq_LEN(mod.body);
      if (bodySize == 0) {
        return false;
      }
      stmt_ty firstStmt = reinterpret_cast<stmt_ty>(asdl_seq_GET(mod.body, 0));
      if (firstStmt->kind == Expr_kind) {
        auto stmtExpr = firstStmt->v.Expr;
        expr_ty expr = stmtExpr.value;
        if (expr->kind == Name_kind) {
          auto name = expr->v.Name;
          return _PyUnicode_EqualToASCIIString(
              name.id, kFullImplicitMarker.c_str());
        }
      }
      return false;
    }

    default:
      return false;
  }
}

std::unique_ptr<ModuleInfo> getStubModuleInfo(
    std::unique_ptr<ModuleInfo> info,
    ModuleLoader* loader) {
  // analyze AST to check if any @implicit is used
  mod_ty mod = info->getAst();
  bool isFullImplicit = checkFullImplicitHelper(mod);
  if (isFullImplicit) {
    // In the case of fullly implicit stubs, use the original source AST
    std::unique_ptr<ModuleInfo> sourceInfo = getSourceModuleInfo(
        info->getModName(), FileSuffixKind::kPythonFile, loader);
    sourceInfo->setModName(info->getModName());
    sourceInfo->setFilename(info->getFilename());
    sourceInfo->setStubKind(StubKind::getStubKind(info->getFilename(), false));
    sourceInfo->setSubmoduleSearchLocations(
        info->getSubmoduleSearchLocations());
    return sourceInfo;
  }

  std::unordered_map<std::string, int> implicitMapping =
      getImplicitToLocHelper(mod);
  // if not, return original info
  if (implicitMapping.empty()) {
    return info;
  }
  // locate original source code of stubbed file
  // Looking for source in the loader's import path
  std::unique_ptr<ModuleInfo> sourceInfo = getSourceModuleInfo(
      info->getModName(), FileSuffixKind::kPythonFile, loader);
  if (!sourceInfo) {
    return nullptr;
  }

  // get mapping of needed implicit nodes
  std::unordered_map<int, stmt_ty> sourceMapping;
  try {
    sourceMapping = getLocToSrcHelper(implicitMapping, sourceInfo->getAst());
  } catch (const CannotDecideSourceException&) {
    return nullptr;
  }

  // update AST with implicit AST nodes expanded using mapping
  updateStubHelper(mod, sourceMapping);

  // free the original symtable and create a new symtable with
  // new AST
  const char* filename = info->getFilename().c_str();
  Ref<> filenameObj = Ref<>::steal(PyUnicode_FromString(filename));
  auto futureFeatures = _PyFuture_FromAST(mod, filenameObj.get());
  auto symbolTable = _PySymtable_Build(mod, filenameObj.get(), futureFeatures);
  PyObject_Free(futureFeatures);

  return std::make_unique<ModuleInfo>(
      info->getModName(),
      info->getFilename(),
      mod,
      info->getFutureAnnotations(),
      std::unique_ptr<PySymtable, PySymtableDeleter>(symbolTable),
      StubKind::getStubKind(info->getFilename(), false),
      info->getSubmoduleSearchLocations());
}
} // namespace strictmod::compiler
