#ifndef HIR_PRINTER_H
#define HIR_PRINTER_H

#include <iostream>
#include <sstream>
#include <string>

#include "Jit/hir/hir.h"

namespace jit {
namespace hir {

// Helper class for pretty printing IR
//
// TODO(mpage): This works, but feels horribly kludgy. This should be possible
// using a custom streambuf for indentation and via overloads for <<.
class HIRPrinter {
 public:
  explicit HIRPrinter(
      bool show_snapshots = false,
      const std::string line_prefix = "")
      : show_snapshots_(show_snapshots), line_prefix_(line_prefix) {}

  void Print(std::ostream& os, const Function& func);
  void Print(std::ostream& os, const CFG& cfg);
  void Print(std::ostream& os, const BasicBlock& block);
  void Print(std::ostream& os, const Instr& instr);
  void Print(std::ostream& os, const FrameState& state);

  template <class T>
  std::string ToString(const T& obj) {
    std::ostringstream os;
    Print(os, obj);
    return os.str();
  }

  template <class T>
  void Print(const T& obj) {
    Print(std::cout, obj);
  }

 private:
  void Indent();
  void Dedent();
  std::ostream& Indented(std::ostream& os);

  int indent_level_{0};
  bool show_snapshots_{false};
  std::string line_prefix_;
};

inline std::ostream& operator<<(std::ostream& os, const Function& func) {
  HIRPrinter().Print(os, func);
  return os;
}

inline std::ostream& operator<<(std::ostream& os, const CFG& cfg) {
  HIRPrinter().Print(os, cfg);
  return os;
}

inline std::ostream& operator<<(std::ostream& os, const BasicBlock& block) {
  HIRPrinter().Print(os, block);
  return os;
}

inline std::ostream& operator<<(std::ostream& os, const Instr& instr) {
  HIRPrinter().Print(os, instr);
  return os;
}

inline std::ostream& operator<<(std::ostream& os, const FrameState& state) {
  HIRPrinter().Print(os, state);
  return os;
}

} // namespace hir
} // namespace jit
#endif // HIR_PRINTER_H
