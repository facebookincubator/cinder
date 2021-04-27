#ifndef __STRICTM_EXCEPTIONS_H__
#define __STRICTM_EXCEPTIONS_H__

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
  const std::string& getFilename() const;
  const std::string& getScopeName() const;
  /* concise string form of the exception, used in tests */
  std::string testString() const;

  /* dynamically dispatched throw
   * All subclasses need to implement this
   */
  [[noreturn]] virtual void raise();

  /* deepcopy the exception object. */
  virtual std::unique_ptr<StrictModuleException> clone();

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
      std::shared_ptr<T> wrapped);

  const std::shared_ptr<const T> getWrapped() const;

  [[noreturn]] virtual void raise() override;
  virtual std::unique_ptr<StrictModuleException> clone() override;
  virtual const char* what() const noexcept override;

 private:
  std::shared_ptr<T> wrapped_;

  virtual std::string testStringHelper() const override;
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
  virtual std::unique_ptr<StrictModuleException> clone() override;
  virtual const char* what() const noexcept override;

 private:
  std::string exceptionName_;
  std::vector<std::string> exceptionArgs_;

  virtual std::string testStringHelper() const override;
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

  virtual std::unique_ptr<StrictModuleException> clone() override;
  virtual const char* what() const noexcept override;

 private:
  std::string formatError() const;
  virtual std::string testStringHelper() const override;
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
      "Module-level unary operation on non-strict value '%s %s' is "
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
      "Module-level attribute access on non-strict value '%s.%s' is "
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
      "Module-level index into non-strict value '%s[%s]' is "
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
      "Module-level call of non-strict value '%s()' is prohibited.";
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
      "Module-level conversion to bool on non-strict value '%s' is prohibited.";
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
      "Attempt to iterate over non-iterable object: '%s";
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
      "can't set attribute %s of immutable %s '%s'";
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
      "%s from module %s is modified by %s; this is prohibited.";
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
      "coroutines function %s with yield expressions are not supported.";
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

inline const std::string& StrictModuleException::getFilename() const {
  return filename_;
}

inline const std::string& StrictModuleException::getScopeName() const {
  return scopeName_;
}

inline std::string StrictModuleException::testString() const {
  return fmt::format("{} {} {}", lineno_, col_, testStringHelper());
}

// StrictModuleUserException
template <typename T>
StrictModuleUserException<T>::StrictModuleUserException(
    int lineno,
    int col,
    std::string filename,
    std::string scopeName,
    std::shared_ptr<T> wrapped)
    : StrictModuleException(
          lineno,
          col,
          std::move(filename),
          std::move(scopeName),
          "",
          nullptr),
      wrapped_(std::move(wrapped)) {}

template <typename T>
const std::shared_ptr<const T> StrictModuleUserException<T>::getWrapped()
    const {
  return std::shared_ptr<const T>(wrapped_);
}

template <typename T>
[[noreturn]] void StrictModuleUserException<T>::raise() {
  throw *this;
}

template <typename T>
std::unique_ptr<StrictModuleException> StrictModuleUserException<T>::clone() {
  return std::make_unique<StrictModuleUserException<T>>(
      lineno_, col_, filename_, scopeName_, wrapped_);
}

template <typename T>
const char* StrictModuleUserException<T>::what() const noexcept {
  msg_ = testString();
  return msg_.c_str();
}

template <typename T>
std::string StrictModuleUserException<T>::testStringHelper() const {
  return "StrictModuleUserException";
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
StructuredStrictModuleException<T, E, mp...>::clone() {
  return std::make_unique<E>(
      lineno_,
      col_,
      filename_,
      scopeName_,
      cause_,
      (static_cast<T*>(this)->*mp)...);
}

template <typename T, typename E, std::string T::*... mp>
const char* StructuredStrictModuleException<T, E, mp...>::what() const
    noexcept {
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
  return fmt::format("{}", fmt::join(strings, " "));
}
} // namespace strictmod

#endif // __STRICTM_EXCEPTIONS_H__
