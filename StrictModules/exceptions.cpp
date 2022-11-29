// Copyright (c) Facebook, Inc. and its affiliates. (http://www.facebook.com)
#include "StrictModules/exceptions.h"
namespace strictmod {

// StrictModuleException
StrictModuleException::StrictModuleException(
    int lineno,
    int col,
    std::string filename,
    std::string scopeName,
    std::string msg,
    std::shared_ptr<const StrictModuleException> cause)
    : lineno_(lineno),
      col_(col),
      filename_(std::move(filename)),
      scopeName_(std::move(scopeName)),
      msg_(msg),
      cause_(std::move(cause)) {}

void StrictModuleException::raise() {
  throw *this;
}

std::unique_ptr<StrictModuleException> StrictModuleException::clone() const {
  return std::make_unique<StrictModuleException>(
      lineno_, col_, filename_, scopeName_, msg_, cause_);
}

const char* StrictModuleException::what() const noexcept {
  return msg_.c_str();
}

std::string StrictModuleException::testStringHelper() const {
  return "StrictModuleException";
}

std::string StrictModuleException::displayStringHelper() const {
  return "StrictModuleException";
}

// StrictModuleNotImplementedException
StrictModuleNotImplementedException::StrictModuleNotImplementedException(
    int lineno,
    int col,
    std::string filename,
    std::string scopeName,
    std::unique_ptr<const StrictModuleException> cause)
    : StrictModuleException(
          lineno,
          col,
          std::move(filename),
          std::move(scopeName),
          "feature not implemented",
          std::move(cause)) {}

std::string StrictModuleNotImplementedException::testStringHelper() const {
  return "StrictModuleNotImplementedException";
}

std::string StrictModuleNotImplementedException::displayStringHelper() const {
  return "StrictModuleNotImplementedException";
}

void StrictModuleNotImplementedException::raise() {
  throw *this;
}

// StrictModuleTooManyIterationsException
StrictModuleTooManyIterationsException::StrictModuleTooManyIterationsException(
    int lineno,
    int col,
    std::string filename,
    std::string scopeName)
    : StrictModuleException(
          lineno,
          col,
          std::move(filename),
          std::move(scopeName),
          "too many iterations") {}

std::string StrictModuleTooManyIterationsException::testStringHelper() const {
  return "StrictModuleTooManyIterationsException";
}

std::string StrictModuleTooManyIterationsException::displayStringHelper()
    const {
  return "StrictModuleTooManyIterationsException";
}

void StrictModuleTooManyIterationsException::raise() {
  throw *this;
}

// StrictModuleUnhandledException
StrictModuleUnhandledException::StrictModuleUnhandledException(
    int lineno,
    int col,
    std::string filename,
    std::string scopeName,
    std::string exceptionName,
    std::vector<std::string> exceptionArgs,
    std::shared_ptr<const StrictModuleException> cause)
    : StrictModuleException(
          lineno,
          col,
          std::move(filename),
          std::move(scopeName),
          "",
          std::move(cause)),
      exceptionName_(std::move(exceptionName)),
      exceptionArgs_(std::move(exceptionArgs)) {}

[[noreturn]] void StrictModuleUnhandledException::raise() {
  throw *this;
}

std::unique_ptr<StrictModuleException> StrictModuleUnhandledException::clone()
    const {
  return std::make_unique<StrictModuleUnhandledException>(
      lineno_,
      col_,
      filename_,
      scopeName_,
      exceptionName_,
      exceptionArgs_,
      cause_);
}

const char* StrictModuleUnhandledException::what() const noexcept {
  msg_ = testString();
  return msg_.c_str();
}

std::string StrictModuleUnhandledException::testStringHelper() const {
  return fmt::format("StrictModuleUnhandledException({})", exceptionName_);
}

std::string StrictModuleUnhandledException::displayStringHelper() const {
  if (exceptionArgs_.empty()) {
    return fmt::format("StrictModuleUnhandledException({})", exceptionName_);
  }
  return fmt::format(
      "StrictModuleUnhandledException({}: {})",
      exceptionName_,
      fmt::join(exceptionArgs_, ", "));
}

// UnknownValueBinaryOpException
UnknownValueBinaryOpExceptionHelper::UnknownValueBinaryOpExceptionHelper(
    std::string name,
    std::string op,
    std::string other)
    : unknownName(std::move(name)), op(op), otherName(std::move(other)) {}

void UnknownValueBinaryOpException::raise() {
  throw *this;
}
// UnknownValueUnaryOpException
UnknownValueUnaryOpExceptionHelper::UnknownValueUnaryOpExceptionHelper(
    std::string op,
    std::string name)
    : op(op), unknownName(std::move(name)) {}

void UnknownValueUnaryOpException::raise() {
  throw *this;
}
// UnknownValueAttributeException
UnknownValueAttributeExceptionHelper::UnknownValueAttributeExceptionHelper(
    std::string name,
    std::string attr)
    : unknownName(std::move(name)), attribute(std::move(attr)) {}

void UnknownValueAttributeException::raise() {
  throw *this;
}
// UnknownValueIndexException
UnknownValueIndexExceptionHelper::UnknownValueIndexExceptionHelper(
    std::string name,
    std::string index)
    : unknownName(std::move(name)), index(std::move(index)) {}

void UnknownValueIndexException::raise() {
  throw *this;
}
// UnknownValueCallException
UnknownValueCallExceptionHelper::UnknownValueCallExceptionHelper(
    std::string name)
    : unknownName(std::move(name)) {}

void UnknownValueCallException::raise() {
  throw *this;
}
// UnknownValueBoolException
UnknownValueBoolExceptionHelper::UnknownValueBoolExceptionHelper(
    std::string name)
    : unknownName(std::move(name)) {}

void UnknownValueBoolException::raise() {
  throw *this;
}

// UnknownValueNotIterableException
UnknownValueNotIterableExceptionHelper::UnknownValueNotIterableExceptionHelper(
    std::string name)
    : unknownName(std::move(name)) {}

void UnknownValueNotIterableException::raise() {
  throw *this;
}
// ImmutableException
ImmutableExceptionHelper::ImmutableExceptionHelper(
    std::string attr,
    std::string kind,
    std::string name)
    : attrName(std::move(attr)),
      immutableKind(std::move(kind)),
      objName(std::move(name)) {}

void ImmutableException::raise() {
  throw *this;
}
// ModifyImportValueException
ModifyImportValueExceptionHelper::ModifyImportValueExceptionHelper(
    std::string name,
    std::string ownerName,
    std::string callerName)
    : objName(std::move(name)),
      ownerName(std::move(ownerName)),
      callerName(std::move(callerName)) {}

void ModifyImportValueException::raise() {
  throw *this;
}

// CoroutineFunctionNotSupportedExceptionHelper
CoroutineFunctionNotSupportedExceptionHelper::
    CoroutineFunctionNotSupportedExceptionHelper(std::string funcName)
    : funcName(std::move(funcName)) {}

void CoroutineFunctionNotSupportedException::raise() {
  throw *this;
}

// UnsafeCallException
UnsafeCallExceptionHelper::UnsafeCallExceptionHelper(std::string name)
    : callableName(std::move(name)) {}

void UnsafeCallException::raise() {
  throw *this;
}

// UnsupportedException
UnsupportedExceptionHelper::UnsupportedExceptionHelper(
    std::string op,
    std::string typeName)
    : opName(std::move(op)), typeName(std::move(typeName)) {}

void UnsupportedException::raise() {
  throw *this;
}

// UnsafeBaseClassException
UnsafeBaseClassExceptionHelper::UnsafeBaseClassExceptionHelper(std::string name)
    : unknownName(std::move(name)) {}

void UnsafeBaseClassException::raise() {
  throw *this;
}

// FailedToUnpackException
FailedToUnpackExceptionHelper::FailedToUnpackExceptionHelper(std::string size)
    : packSize(std::move(size)) {}

void FailedToUnpackException::raise() {
  throw *this;
}

// StarImportDisallowedException
StarImportDisallowedExceptionHelper::StarImportDisallowedExceptionHelper(
    std::string mod)
    : fromMod(std::move(mod)) {}

void StarImportDisallowedException::raise() {
  throw *this;
}

// ImportDisallowedExceptionHelper
ImportDisallowedExceptionHelper::ImportDisallowedExceptionHelper(
    std::string context)
    : context(std::move(context)) {}

void ImportDisallowedException::raise() {
  throw *this;
}

// BadStrictFlagException
BadStrictFlagExceptionHelper::BadStrictFlagExceptionHelper(std::string err)
    : err(std::move(err)) {}

void BadStrictFlagException::raise() {
  throw *this;
}
// ConflictingSourceException
ConflictingSourceExceptionHelper::ConflictingSourceExceptionHelper(
    std::string mod,
    std::string first,
    std::string second)
    : modName(std::move(mod)), firstName(first), secondName(second) {}

void ConflictingSourceException::raise() {
  throw *this;
}
} // namespace strictmod
