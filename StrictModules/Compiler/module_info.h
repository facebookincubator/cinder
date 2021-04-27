// Copyright (c) Facebook, Inc. and its affiliates. (http://www.facebook.com)
#ifndef __STRICTM_MODULE_INFO_H__
#define __STRICTM_MODULE_INFO_H__

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "StrictModules/py_headers.h"

#include "StrictModules/symbol_table.h"

namespace strictmod::compiler {
class ModuleInfo {
 public:
  ModuleInfo(
      std::string modName,
      std::string filename,
      mod_ty modAst,
      bool futureAnnotations,
      std::unique_ptr<PySymtable, PySymtableDeleter> st,
      std::vector<std::string> submoduleSearchLocations)
      : modName_(std::move(modName)),
        filename_(std::move(filename)),
        modAst_(modAst),
        futureAnnotations_(futureAnnotations),
        st_(std::move(st)),
        submoduleSearchLocations_(std::move(submoduleSearchLocations)) {}

  ModuleInfo(
      std::string modName,
      std::string filename,
      mod_ty modAst,
      bool futureAnnotations,
      std::unique_ptr<PySymtable, PySymtableDeleter> st)
      : modName_(std::move(modName)),
        filename_(std::move(filename)),
        modAst_(modAst),
        futureAnnotations_(futureAnnotations),
        st_(std::move(st)),
        submoduleSearchLocations_({}) {}

  const std::string& getModName() const {
    return modName_;
  }

  const std::string& getFilename() const {
    return filename_;
  }

  mod_ty getAst() const {
    return modAst_;
  }

  bool getFutureAnnotations() const {
    return futureAnnotations_;
  }

  std::unique_ptr<PySymtable, PySymtableDeleter> passSymtable() {
    return std::move(st_);
  }

  const std::vector<std::string>& getSubmoduleSearchLocations() const {
    return submoduleSearchLocations_;
  }

 private:
  std::string modName_;
  std::string filename_;
  mod_ty modAst_;
  bool futureAnnotations_;
  std::unique_ptr<PySymtable, PySymtableDeleter> st_;
  std::vector<std::string> submoduleSearchLocations_;
};

} // namespace strictmod::compiler

#endif // !__STRICTM_MODULE_INFO_H__
