// Copyright (c) Meta Platforms, Inc. and affiliates.
#pragma once

#include <fmt/format.h>

#include <array>
#include <exception>
#include <iterator>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

namespace strictmod {

// We will inject the wiki base url from callsite
const std::string kWikiBase = "";

/**
 * Base of all strict module related exceptions
 *  lineno, col: line number and col number in source code
 *  filename: file in which exception occured
 *  scopeName: scope (e.g. function name or <module>) in which exception occured
 *  msg: message of exception
 *  cause: which exception caused the current exception
 */
class StrictModuleException : public std::exception {
 public:
  StrictModuleException(
      int lineno,
      int col,
      std::string filename,
      std::string scopeName,
      std::string msg,
      std::shared_ptr<const StrictModuleException> cause = nullptr);

  virtual ~StrictModuleException() {}

  /* getters */
  std::string getMsg() const;
  const std::shared_ptr<const StrictModuleException> getCause() const;
  int getLineno() const;
  int getCol() const;
  void setlineInfo(int lineno, int col);
  const std::string& getFilename() const;
  const std::string& getScopeName() const;
  void setFilename(std::string filename);
  void setScopeName(std::string scopeName);

  /* concise string form of the exception, used in tests */
  std::string testString() const;
  /* more readable string form of the exception, shown to users */
  std::string displayString(bool useLocation = false) const;
  void setCause(std::shared_ptr<StrictModuleException> cause) {
    cause_ = std::move(cause);
  }

  /* dynamically dispatched throw
   * All subclasses need to implement this
   */
  [[noreturn]] virtual void raise();

  /* deepcopy the exception object. */
  virtual std::unique_ptr<StrictModuleException> clone() const;

  /* what to output as a std::exception */
  virtual const char* what() const noexcept override;

 protected:
  int lineno_;
  int col_;
  /* The file containing the line of code that causes the error
   */
  std::string filename_;
  std::string scopeName_;
  mutable std::string msg_;
  /* use of shared_ptr here because unique_ptr cannot
   * be copy constructed, and we need to throw here
   */
  std::shared_ptr<const StrictModuleException> cause_;

  virtual std::string testStringHelper() const;
  virtual std::string displayStringHelper() const;
};

/** exception for unsupported language features
 * encountered during the analysis.
 * There should be nothing throwing this
 * by the end of the migration
 */
class StrictModuleNotImplementedException : public StrictModuleException {
 public:
  StrictModuleNotImplementedException(
      int lineno,
      int col,
      std::string filename,
      std::string scopeName,
      std::unique_ptr<const StrictModuleException> cause = nullptr);

  [[noreturn]] virtual void raise() override;

 private:
  virtual std::string testStringHelper() const override;
  virtual std::string displayStringHelper() const override;
};

/** iteration exceeds limit. */
class StrictModuleTooManyIterationsException : public StrictModuleException {
 public:
  StrictModuleTooManyIterationsException(
      int lineno,
      int col,
      std::string filename,
      std::string scopeName);

  [[noreturn]] virtual void raise() override;

 private:
  virtual std::string testStringHelper() const override;
  virtual std::string displayStringHelper() const override;
};

/** Use this for user space exceptions, i.e. exceptions
 *  that the analyzed Python program may raise
 */

template <typename T>
class StrictModuleUserException : public StrictModuleException {
 public:
  StrictModuleUserException(
      int lineno,
      int col,
      std::string filename,
      std::string scopeName,
      std::shared_ptr<T> wrapped,
      std::shared_ptr<const StrictModuleException> cause = nullptr);

  const std::shared_ptr<const T> getWrapped() const;
  const std::shared_ptr<T> getWrapped();

  [[noreturn]] virtual void raise() override;
  virtual std::unique_ptr<StrictModuleException> clone() const override;
  virtual const char* what() const noexcept override;

 private:
  std::shared_ptr<T> wrapped_;

  virtual std::string testStringHelper() const override;
  virtual std::string displayStringHelper() const override;
};

class StrictModuleUnhandledException : public StrictModuleException {
 public:
  StrictModuleUnhandledException(
      int lineno,
      int col,
      std::string filename,
      std::string scopeName,
      std::string exceptionName,
      std::vector<std::string> exceptionArgs,
      std::shared_ptr<const StrictModuleException> cause = nullptr);

  [[noreturn]] virtual void raise() override;
  virtual std::unique_ptr<StrictModuleException> clone() const override;
  virtual const char* what() const noexcept override;

 private:
  std::string exceptionName_;
  std::vector<std::string> exceptionArgs_;

  virtual std::string testStringHelper() const override;
  virtual std::string displayStringHelper() const override;
};

/**
 * Use this template to specify which fields in a structued
 * exception will be formatted into the final
 * error message.
 * Be careful that the order of member supplied should be the order
 * they appear in the format string, and should be the order in the
 * constructor
 */
template <typename T, typename E, std::string T::*... mp>
class StructuredStrictModuleException : public T, public StrictModuleException {
 public:
  template <typename... Args>
  StructuredStrictModuleException(
      int lineno,
      int col,
      std::string filename,
      std::string scopeName,
      Args... args);

  template <typename... Args>
  StructuredStrictModuleException(
      int lineno,
      int col,
      std::string filename,
      std::string scopeName,
      std::shared_ptr<const StrictModuleException> cause,
      Args... args);

  virtual std::unique_ptr<StrictModuleException> clone() const override;
  virtual const char* what() const noexcept override;

 private:
  std::string formatError() const;
  virtual std::string testStringHelper() const override;
  virtual std::string displayStringHelper() const override;
};

//--------------------------Structured exceptions---------------------------
// UnknownValueBinaryOpException
struct UnknownValueBinaryOpExceptionHelper {
  UnknownValueBinaryOpExceptionHelper(
      std::string name,
      std::string op,
      std::string other);

  std::string unknownName;
  std::string op;
  std::string otherName;

  static constexpr const char* excName = "UnknownValueBinaryOpException";
  static constexpr const char* fmt =
      "Module-level binary operation on non-strict value '{} {} {}' is "
      "prohibited.";
  static constexpr const char* wiki = "unknown_value_binary_op";
};

class UnknownValueBinaryOpException
    : public StructuredStrictModuleException<
          UnknownValueBinaryOpExceptionHelper,
          UnknownValueBinaryOpException,
          &UnknownValueBinaryOpExceptionHelper::unknownName,
          &UnknownValueBinaryOpExceptionHelper::op,
          &UnknownValueBinaryOpExceptionHelper::otherName> {
 public:
  using StructuredStrictModuleException::StructuredStrictModuleException;
  [[noreturn]] virtual void raise() override;
};

// UnknownValueUnaryOpException
struct UnknownValueUnaryOpExceptionHelper {
  UnknownValueUnaryOpExceptionHelper(std::string op, std::string name);

  std::string op;
  std::string unknownName;

  static constexpr const char* excName = "UnknownValueUnaryOpException";
  static constexpr const char* fmt =
      "Module-level unary operation on non-strict value '{} {}' is "
      "prohibited.";
  static constexpr const char* wiki = "unknown_value_binary_op";
};

class UnknownValueUnaryOpException
    : public StructuredStrictModuleException<
          UnknownValueUnaryOpExceptionHelper,
          UnknownValueUnaryOpException,
          &UnknownValueUnaryOpExceptionHelper::op,
          &UnknownValueUnaryOpExceptionHelper::unknownName> {
 public:
  using StructuredStrictModuleException::StructuredStrictModuleException;
  [[noreturn]] virtual void raise() override;
};

// UnknownValueAttributeException
struct UnknownValueAttributeExceptionHelper {
  UnknownValueAttributeExceptionHelper(std::string name, std::string attribute);

  std::string unknownName;
  std::string attribute;

  static constexpr const char* excName = "UnknownValueAttributeException";
  static constexpr const char* fmt =
      "Module-level attribute access on non-strict value '{}.{}' is "
      "prohibited.";
  static constexpr const char* wiki = "unknown_value_attribute";
};

class UnknownValueAttributeException
    : public StructuredStrictModuleException<
          UnknownValueAttributeExceptionHelper,
          UnknownValueAttributeException,
          &UnknownValueAttributeExceptionHelper::unknownName,
          &UnknownValueAttributeExceptionHelper::attribute> {
 public:
  using StructuredStrictModuleException::StructuredStrictModuleException;
  [[noreturn]] virtual void raise() override;
};

// UnknownValueIndexException
struct UnknownValueIndexExceptionHelper {
  UnknownValueIndexExceptionHelper(std::string name, std::string index);

  std::string unknownName;
  std::string index;

  static constexpr const char* excName = "UnknownValueIndexException";
  static constexpr const char* fmt =
      "Module-level index into non-strict value '{}[{}]' is "
      "prohibited.";
  static constexpr const char* wiki = "unknown_value_index";
};

class UnknownValueIndexException
    : public StructuredStrictModuleException<
          UnknownValueIndexExceptionHelper,
          UnknownValueIndexException,
          &UnknownValueIndexExceptionHelper::unknownName,
          &UnknownValueIndexExceptionHelper::index> {
 public:
  using StructuredStrictModuleException::StructuredStrictModuleException;
  [[noreturn]] virtual void raise() override;
};

// UnknownValueCallException
struct UnknownValueCallExceptionHelper {
  UnknownValueCallExceptionHelper(std::string name);

  std::string unknownName;

  static constexpr const char* excName = "UnknownValueCallException";
  static constexpr const char* fmt =
      "Module-level call of non-strict value '{}()' is prohibited.";
  static constexpr const char* wiki = "unknown_call";
};

class UnknownValueCallException
    : public StructuredStrictModuleException<
          UnknownValueCallExceptionHelper,
          UnknownValueCallException,
          &UnknownValueCallExceptionHelper::unknownName> {
 public:
  using StructuredStrictModuleException::StructuredStrictModuleException;
  [[noreturn]] virtual void raise() override;
};

// UnknownValueBoolException
struct UnknownValueBoolExceptionHelper {
  UnknownValueBoolExceptionHelper(std::string name);

  std::string unknownName;

  static constexpr const char* excName = "UnknownValueBoolException";
  static constexpr const char* fmt =
      "Module-level conversion to bool on non-strict value '{}' is prohibited.";
  static constexpr const char* wiki = "unknown_value_bool_op";
};

class UnknownValueBoolException
    : public StructuredStrictModuleException<
          UnknownValueBoolExceptionHelper,
          UnknownValueBoolException,
          &UnknownValueBoolExceptionHelper::unknownName> {
 public:
  using StructuredStrictModuleException::StructuredStrictModuleException;
  [[noreturn]] virtual void raise() override;
};

// UnknownValueNotIterableException
struct UnknownValueNotIterableExceptionHelper {
  UnknownValueNotIterableExceptionHelper(std::string name);

  std::string unknownName;

  static constexpr const char* excName = "UnknownValueNotIterableException";
  static constexpr const char* fmt =
      "Attempt to iterate over non-iterable object: '{}";
  static constexpr const char* wiki = "unknown_value_attribute";
};

class UnknownValueNotIterableException
    : public StructuredStrictModuleException<
          UnknownValueNotIterableExceptionHelper,
          UnknownValueNotIterableException,
          &UnknownValueNotIterableExceptionHelper::unknownName> {
 public:
  using StructuredStrictModuleException::StructuredStrictModuleException;
  [[noreturn]] virtual void raise() override;
};

// ImmutableException
struct ImmutableExceptionHelper {
  ImmutableExceptionHelper(
      std::string attrName,
      std::string kind,
      std::string name);

  std::string attrName;
  std::string immutableKind; // module, type or object
  std::string objName;

  static constexpr const char* excName = "ImmutableException";
  static constexpr const char* fmt =
      "can't set attribute {} of immutable {} '{}'";
  static constexpr const char* wiki = "";
};

class ImmutableException : public StructuredStrictModuleException<
                               ImmutableExceptionHelper,
                               ImmutableException,
                               &ImmutableExceptionHelper::attrName,
                               &ImmutableExceptionHelper::immutableKind,
                               &ImmutableExceptionHelper::objName> {
 public:
  using StructuredStrictModuleException::StructuredStrictModuleException;
  [[noreturn]] virtual void raise() override;
};

// ModifyImportValueException
struct ModifyImportValueExceptionHelper {
  ModifyImportValueExceptionHelper(
      std::string name,
      std::string ownerName,
      std::string callerName);

  std::string objName;
  std::string ownerName;
  std::string callerName;

  static constexpr const char* excName = "ModifyImportValueException";
  static constexpr const char* fmt =
      "{} from module {} is modified by {}; this is prohibited.";
  static constexpr const char* wiki = "modify_imported_value";
};

class ModifyImportValueException
    : public StructuredStrictModuleException<
          ModifyImportValueExceptionHelper,
          ModifyImportValueException,
          &ModifyImportValueExceptionHelper::objName,
          &ModifyImportValueExceptionHelper::ownerName,
          &ModifyImportValueExceptionHelper::callerName> {
 public:
  using StructuredStrictModuleException::StructuredStrictModuleException;
  [[noreturn]] virtual void raise() override;
};

// CoroutineFunctionNotSupportedException
class CoroutineFunctionNotSupportedExceptionHelper {
 public:
  CoroutineFunctionNotSupportedExceptionHelper(std::string funcName);
  std::string funcName;
  static constexpr const char* excName =
      "CoroutineFunctionNotSupportedException";
  static constexpr const char* fmt =
      "coroutines function {} with yield expressions are not supported.";
  static constexpr const char* wiki = "";
};

class CoroutineFunctionNotSupportedException
    : public StructuredStrictModuleException<
          CoroutineFunctionNotSupportedExceptionHelper,
          CoroutineFunctionNotSupportedException,
          &CoroutineFunctionNotSupportedExceptionHelper::funcName> {
 public:
  using StructuredStrictModuleException::StructuredStrictModuleException;
  [[noreturn]] virtual void raise() override;
};

// UnsafeCallException
struct UnsafeCallExceptionHelper {
  UnsafeCallExceptionHelper(std::string name);

  std::string callableName;

  static constexpr const char* excName = "UnsafeCallException";
  static constexpr const char* fmt =
      "Call '{}()' may have side effects and is prohibited at module level.";
  static constexpr const char* wiki = "unsafe_call";
};

class UnsafeCallException : public StructuredStrictModuleException<
                                UnsafeCallExceptionHelper,
                                UnsafeCallException,
                                &UnsafeCallExceptionHelper::callableName> {
 public:
  using StructuredStrictModuleException::StructuredStrictModuleException;
  [[noreturn]] virtual void raise() override;
};

// UnsupportedException
struct UnsupportedExceptionHelper {
  UnsupportedExceptionHelper(std::string name, std::string typeName);

  std::string opName;
  std::string typeName;

  static constexpr const char* excName = "UnsupportedException";
  static constexpr const char* fmt =
      "Operation '{}' on type '{}' is unsupported";
  static constexpr const char* wiki = "";
};

class UnsupportedException : public StructuredStrictModuleException<
                                 UnsupportedExceptionHelper,
                                 UnsupportedException,
                                 &UnsupportedExceptionHelper::opName,
                                 &UnsupportedExceptionHelper::typeName> {
 public:
  using StructuredStrictModuleException::StructuredStrictModuleException;
  [[noreturn]] virtual void raise() override;
};

// UnsafeBaseClassException
struct UnsafeBaseClassExceptionHelper {
  UnsafeBaseClassExceptionHelper(std::string name);

  std::string unknownName;

  static constexpr const char* excName = "UnsafeBaseClassException";
  static constexpr const char* fmt = "'{}' is not a valid base class";
  static constexpr const char* wiki = "";
};

class UnsafeBaseClassException
    : public StructuredStrictModuleException<
          UnsafeBaseClassExceptionHelper,
          UnsafeBaseClassException,
          &UnsafeBaseClassExceptionHelper::unknownName> {
 public:
  using StructuredStrictModuleException::StructuredStrictModuleException;
  [[noreturn]] virtual void raise() override;
};

// FailedToUnpackException
struct FailedToUnpackExceptionHelper {
  FailedToUnpackExceptionHelper(std::string size);

  std::string packSize;

  static constexpr const char* excName = "FailedToUnpackException";
  static constexpr const char* fmt = "failed to unpack rhs into {} variables";
  static constexpr const char* wiki = "";
};

class FailedToUnpackException : public StructuredStrictModuleException<
                                    FailedToUnpackExceptionHelper,
                                    FailedToUnpackException,
                                    &FailedToUnpackExceptionHelper::packSize> {
 public:
  using StructuredStrictModuleException::StructuredStrictModuleException;
  [[noreturn]] virtual void raise() override;
};

// StarImportDisallowedException
struct StarImportDisallowedExceptionHelper {
  StarImportDisallowedExceptionHelper(std::string mod);

  std::string fromMod;

  static constexpr const char* excName = "StarImportDisallowedException";
  static constexpr const char* fmt = "cannot import * from {}";
  static constexpr const char* wiki = "";
};

class StarImportDisallowedException
    : public StructuredStrictModuleException<
          StarImportDisallowedExceptionHelper,
          StarImportDisallowedException,
          &StarImportDisallowedExceptionHelper::fromMod> {
 public:
  using StructuredStrictModuleException::StructuredStrictModuleException;
  [[noreturn]] virtual void raise() override;
};

// ImportDisallowedException
struct ImportDisallowedExceptionHelper {
  ImportDisallowedExceptionHelper(std::string context);

  std::string context;

  static constexpr const char* excName = "ImportDisallowedException";
  static constexpr const char* fmt = "import statements in {} is disallowed";
  static constexpr const char* wiki = "";
};
class ImportDisallowedException
    : public StructuredStrictModuleException<
          ImportDisallowedExceptionHelper,
          ImportDisallowedException,
          &ImportDisallowedExceptionHelper::context> {
 public:
  using StructuredStrictModuleException::StructuredStrictModuleException;
  [[noreturn]] virtual void raise() override;
};

// BadStrictFlagException
struct BadStrictFlagExceptionHelper {
  BadStrictFlagExceptionHelper(std::string err);

  std::string err;

  static constexpr const char* excName = "BadStrictFlagException";
  static constexpr const char* fmt = "bad strict flag: {}";
  static constexpr const char* wiki = "";
};
class BadStrictFlagException : public StructuredStrictModuleException<
                                   BadStrictFlagExceptionHelper,
                                   BadStrictFlagException,
                                   &BadStrictFlagExceptionHelper::err> {
 public:
  using StructuredStrictModuleException::StructuredStrictModuleException;
  [[noreturn]] virtual void raise() override;
};

// ConflictingSourceException
class ConflictingSourceExceptionHelper {
 public:
  ConflictingSourceExceptionHelper(
      std::string modName,
      std::string firstName,
      std::string secondName);
  std::string modName;
  std::string firstName;
  std::string secondName;
  static constexpr const char* excName = "ConflictingSourceException";
  static constexpr const char* fmt =
      "Got conflicting source files for module {}, first seen: {}, second "
      "seen: {}";
  static constexpr const char* wiki = "";
};

class ConflictingSourceException
    : public StructuredStrictModuleException<
          ConflictingSourceExceptionHelper,
          ConflictingSourceException,
          &ConflictingSourceExceptionHelper::modName,
          &ConflictingSourceExceptionHelper::firstName,
          &ConflictingSourceExceptionHelper::secondName> {
 public:
  using StructuredStrictModuleException::StructuredStrictModuleException;
  [[noreturn]] virtual void raise() override;
};

// ------------------Out of line implementations---------------

// StrictModuleException
inline std::string StrictModuleException::getMsg() const {
  return msg_;
}

inline const std::shared_ptr<const StrictModuleException>
StrictModuleException::getCause() const {
  return std::shared_ptr<const StrictModuleException>(cause_);
}

inline int StrictModuleException::getLineno() const {
  return lineno_;
}

inline int StrictModuleException::getCol() const {
  return col_;
}

inline void StrictModuleException::setlineInfo(int lineno, int col) {
  lineno_ = lineno;
  col_ = col;
}

inline const std::string& StrictModuleException::getFilename() const {
  return filename_;
}

inline const std::string& StrictModuleException::getScopeName() const {
  return scopeName_;
}

inline void StrictModuleException::setFilename(std::string filename) {
  filename_ = std::move(filename);
}

inline void StrictModuleException::setScopeName(std::string scopeName) {
  scopeName_ = std::move(scopeName);
}

inline std::string StrictModuleException::testString() const {
  if (cause_ != nullptr) {
    return fmt::format(
        "{} {} {} {}", lineno_, col_, testStringHelper(), cause_->testString());
  }
  return fmt::format("{} {} {}", lineno_, col_, testStringHelper());
}

inline std::string StrictModuleException::displayString(
    bool useLocation) const {
  std::string res;
  if (cause_ != nullptr) {
    res = fmt::format(
        "{}\nCaused by: {}",
        displayStringHelper(),
        cause_->displayString(true)); // cause always has lineinfo
  } else {
    res = displayStringHelper();
  }
  if (useLocation) {
    return fmt::format("[{} {}:{}] {}", filename_, lineno_, col_, res);
  } else {
    return res;
  }
}

// StrictModuleUserException
template <typename T>
StrictModuleUserException<T>::StrictModuleUserException(
    int lineno,
    int col,
    std::string filename,
    std::string scopeName,
    std::shared_ptr<T> wrapped,
    std::shared_ptr<const StrictModuleException> cause)
    : StrictModuleException(
          lineno,
          col,
          std::move(filename),
          std::move(scopeName),
          "",
          std::move(cause)),
      wrapped_(std::move(wrapped)) {}

template <typename T>
const std::shared_ptr<const T> StrictModuleUserException<T>::getWrapped()
    const {
  return std::shared_ptr<const T>(wrapped_);
}

template <typename T>
const std::shared_ptr<T> StrictModuleUserException<T>::getWrapped() {
  return wrapped_;
}

template <typename T>
[[noreturn]] void StrictModuleUserException<T>::raise() {
  throw *this;
}

template <typename T>
std::unique_ptr<StrictModuleException> StrictModuleUserException<T>::clone()
    const {
  return std::make_unique<StrictModuleUserException<T>>(
      lineno_, col_, filename_, scopeName_, wrapped_, cause_);
}

template <typename T>
const char* StrictModuleUserException<T>::what() const noexcept {
  msg_ = testString();
  return msg_.c_str();
}

template <typename T>
std::string StrictModuleUserException<T>::testStringHelper() const {
  return "StrictModuleUserException " + wrapped_->getTypeRef().getDisplayName();
}

template <typename T>
std::string StrictModuleUserException<T>::displayStringHelper() const {
  return fmt::format("UserException({})", wrapped_->getDisplayName());
}

// StrictModuleStructuredException
template <typename T, typename E, std::string T::*... mp>
template <typename... Args>
StructuredStrictModuleException<T, E, mp...>::StructuredStrictModuleException(
    int lineno,
    int col,
    std::string filename,
    std::string scopeName,
    Args... args)
    : T(std::move(args)...),
      StrictModuleException(
          lineno,
          col,
          std::move(filename),
          std::move(scopeName),
          formatError()) {}

template <typename T, typename E, std::string T::*... mp>
template <typename... Args>
StructuredStrictModuleException<T, E, mp...>::StructuredStrictModuleException(
    int lineno,
    int col,
    std::string filename,
    std::string scopeName,
    std::shared_ptr<const StrictModuleException> cause,
    Args... args)
    : T(std::move(args)...),
      StrictModuleException(
          lineno,
          col,
          std::move(filename),
          std::move(scopeName),
          formatError(),
          cause) {}

template <typename T, typename E, std::string T::*... mp>
std::unique_ptr<StrictModuleException>
StructuredStrictModuleException<T, E, mp...>::clone() const {
  return std::make_unique<E>(
      lineno_,
      col_,
      filename_,
      scopeName_,
      cause_,
      (static_cast<const T*>(this)->*mp)...);
}

template <typename T, typename E, std::string T::*... mp>
const char* StructuredStrictModuleException<T, E, mp...>::what()
    const noexcept {
  msg_ = formatError();
  return msg_.c_str();
}

template <typename T, typename E, std::string T::*... mp>
std::string StructuredStrictModuleException<T, E, mp...>::formatError() const {
  std::ostringstream stream;
  stream << fmt::format(T::fmt, static_cast<const T*>(this)->*mp...);
  stream << "\nSee " << kWikiBase << T::wiki;
  return stream.str();
}

template <typename T, typename E, std::string T::*... mp>
std::string StructuredStrictModuleException<T, E, mp...>::testStringHelper()
    const {
  constexpr int size = sizeof...(mp);
  std::array<std::string, size> strings = {
      (static_cast<const T*>(this)->*mp)...};
  return fmt::format(
      "{} {}", static_cast<const T*>(this)->excName, fmt::join(strings, " "));
}

template <typename T, typename E, std::string T::*... mp>
std::string StructuredStrictModuleException<T, E, mp...>::displayStringHelper()
    const {
  return fmt::format(T::fmt, static_cast<const T*>(this)->*mp...);
}
} // namespace strictmod
