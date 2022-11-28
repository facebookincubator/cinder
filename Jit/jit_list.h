// Copyright (c) Meta Platforms, Inc. and affiliates. (http://www.meta.com)

#pragma once

#include "Python.h"

#include "Jit/ref.h"
#include "Jit/util.h"

#include <memory>

namespace jit {

// Control global setting to use line numbers or not when checking if a function
// is on a JIT list.
void jitlist_match_line_numbers(bool v);
bool get_jitlist_match_line_numbers();

// The JIT list is a file that specifies which functions should be compiled.
//
// The file consists of one function per line in the following format
//
//   <module>:<qualname>
//
// Leading and trailing whitespace is ignored. Lines that begin with `#` are
// also ignored.
class JITList {
 public:
  static std::unique_ptr<JITList> create();
  virtual ~JITList() {}

  // Parse a JIT list from a file.
  //
  // Returns true on success or false on error.
  bool parseFile(const char* filename);

  // Parse a single entry on the JIT list.
  //
  // Returns true on success or false on error.
  bool parseLine(const std::string& line);

  // Check if function is on the list.
  //
  // Returns 1, 0, -1 if the function was found, not found, or an error
  // occurred, respectively.
  int lookup(BorrowedRef<PyFunctionObject> function);
  virtual int lookupFO(BorrowedRef<> mod, BorrowedRef<> qualname);
  virtual int lookupCO(BorrowedRef<PyCodeObject> code);

  // Return a new reference to the dictionary used for matching elements in the
  // JIT list.
  Ref<> getList() const;

 protected:
  JITList(Ref<> qualnames, Ref<> name_file_line_no)
      : qualnames_(std::move(qualnames)),
        name_file_line_no_(std::move(name_file_line_no)) {}

  virtual bool addEntryFO(const char* module_name, const char* qualname);
  bool addEntryFO(BorrowedRef<> module_name, BorrowedRef<> qualname);

  virtual bool
  addEntryCO(const char* name, const char* file, const char* line_no);
  bool
  addEntryCO(BorrowedRef<> name, BorrowedRef<> file, BorrowedRef<> line_no);

  // Dict of module name to set of qualnames
  Ref<> qualnames_;

  // Dict of name/qualname -> dict of file basename -> set of line numbers
  Ref<> name_file_line_no_;

 private:
  DISALLOW_COPY_AND_ASSIGN(JITList);

  Ref<> pathBasename(BorrowedRef<> path);

  Ref<> path_sep_;
};

// A wildcard JIT list allows one to match multiple functions with a single
// entry in the JIT list.
//
// The file format is the same as the non-wildcard JIT list, with added
// support for wildcards:
//
// - The character `*` may be used in place of `<module>` or `<qualname>` to
//   match anything.
// - The token `*.<name>` may be used to match any `<qualname>` that ends with
//   `.<name>`, where `<name>` contains no `.` characters.
//
// Wildcard support enables a few common use cases that are helpful when
// experimenting with different JIT lists.
//
// JIT all functions in module `foo.bar`:
//
//   `foo.bar:*`
//
// JIT all functions whose qualname is `hello`:
//
//   `*:hello`
//
// JIT all constructors:
//
//   `*:*.__init__`
//
// Supplying `*:*` is NOT a valid entry. Don't use a JIT list if you want to
// JIT everything.
class WildcardJITList : public JITList {
 public:
  static std::unique_ptr<WildcardJITList> create();

  int lookupFO(BorrowedRef<> mod, BorrowedRef<> qualname) override;

 protected:
  WildcardJITList(Ref<> wildcard, Ref<> qualnames)
      : JITList(std::move(qualnames), Ref<>::steal(PyDict_New())),
        wildcard_(std::move(wildcard)) {}

  bool addEntryFO(const char* module_name, const char* qualname) override;

  Ref<> wildcard_;
};

} // namespace jit
