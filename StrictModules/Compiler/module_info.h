// Copyright (c) Facebook, Inc. and its affiliates. (http://www.facebook.com)
#pragma once

#include "StrictModules/error_sink.h"
#include "StrictModules/exceptions.h"
#include "StrictModules/py_headers.h"
#include "StrictModules/pystrictmodule.h"
#include "StrictModules/symbol_table.h"

#include <memory>
#include <optional>
#include <string>
#include <vector>
namespace strictmod::compiler {

class ModuleInfo;

class StubKind {
  int kind_;

 public:
  StubKind(int kind) : kind_(kind) {}

  static StubKind getStubKind(const std::string& filename, bool isAllowListed);

  bool isForcedStrict() const {
    return kind_ & 1;
  }

  bool isAllowListed() const {
    return kind_ & 0b10;
  }

  bool isTyping() const {
    return kind_ == Ci_STUB_KIND_MASK_TYPING;
  }

  int getValue() const {
    return kind_;
  }
};

class FlagError {
 public:
  FlagError(int lineno, int col, std::string filename, std::string msg)
      : lineno_(lineno), col_(col), filename_(filename), msg_(std::move(msg)) {}

  void raise(BaseErrorSink* errorSink) {
    errorSink->error<BadStrictFlagException>(
        lineno_, col_, filename_, "", msg_);
  }

 private:
  int lineno_;
  int col_;
  std::string filename_;
  std::string msg_;
};

class ModuleInfo {
 public:
  ModuleInfo(
      std::string modName,
      std::string filename,
      mod_ty modAst,
      bool futureAnnotations,
      std::unique_ptr<PySymtable, PySymtableDeleter> st,
      StubKind stubKind,
      std::vector<std::string> submoduleSearchLocations = {})
      : modName_(std::move(modName)),
        filename_(std::move(filename)),
        modAst_(modAst),
        futureAnnotations_(futureAnnotations),
        st_(std::move(st)),
        submoduleSearchLocations_(std::move(submoduleSearchLocations)),
        stubKind_(stubKind),
        flagError_() {}

  const std::string& getModName() const {
    return modName_;
  }
  void setModName(std::string name) {
    modName_ = std::move(name);
  }

  const std::string& getFilename() const {
    return filename_;
  }
  void setFilename(std::string name) {
    filename_ = std::move(name);
  }

  mod_ty getAst() const {
    return modAst_;
  }

  bool getFutureAnnotations() const {
    return futureAnnotations_;
  }

  void setFutureAnnotations(bool useFutureAnnotation) {
    futureAnnotations_ = useFutureAnnotation;
  }

  const StubKind& getStubKind() const {
    return stubKind_;
  }
  void setStubKind(StubKind kind) {
    stubKind_ = std::move(kind);
  }

  std::shared_ptr<PySymtable> getSymtable() const {
    return st_;
  }

  const std::vector<std::string>& getSubmoduleSearchLocations() const {
    return submoduleSearchLocations_;
  }
  void setSubmoduleSearchLocations(std::vector<std::string> locations) {
    submoduleSearchLocations_ = std::move(locations);
  }

  void setFlagError(int line, int col, const std::string& msg) {
    flagError_ = std::make_unique<FlagError>(line, col, filename_, msg);
  }

  void raiseAnyFlagError(BaseErrorSink* errorSink) {
    if (flagError_) {
      flagError_->raise(errorSink);
    }
  }

 private:
  std::string modName_;
  std::string filename_;
  mod_ty modAst_;
  bool futureAnnotations_;
  std::shared_ptr<PySymtable> st_;
  std::vector<std::string> submoduleSearchLocations_;
  StubKind stubKind_;
  std::unique_ptr<FlagError> flagError_;
};

} // namespace strictmod::compiler
