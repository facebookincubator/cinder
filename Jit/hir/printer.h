// Copyright (c) Meta Platforms, Inc. and affiliates. (http://www.meta.com)

#pragma once

#include "Jit/hir/hir.h"

#include <json.hpp>

#include <iostream>
#include <sstream>
#include <string>

namespace jit::hir {

// Helper class for pretty printing IR
//
// TODO(mpage): This works, but feels horribly kludgy. This should be possible
// using a custom streambuf for indentation and via overloads for <<.
class HIRPrinter {
 public:
  // Construct an HIRPrinter.
  //
  // If full_snapshots is false, Snapshot instructions printed as part of a
  // BasicBlock, CFG, or Function, will be printed only as the opcode name,
  // with no FrameState. When printing individual instructions, each caller can
  // specify whether or not the full instruction should be printed.
  explicit HIRPrinter(
      bool full_snapshots = false,
      const std::string line_prefix = "")
      : full_snapshots_(full_snapshots), line_prefix_(line_prefix) {}

  void Print(std::ostream& os, const Function& func);
  void Print(std::ostream& os, const CFG& cfg);
  void Print(std::ostream& os, const BasicBlock& block);
  void Print(std::ostream& os, const Instr& instr, bool full_snapshots = true);
  void Print(std::ostream& os, const FrameState& state);
  void Print(std::ostream& os, const CFG& cfg, BasicBlock* start);

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
  bool full_snapshots_{false};
  std::string line_prefix_;
};

// TODO(emacs): Handle no PyCodeObject
class JSONPrinter {
 public:
  nlohmann::json PrintSource(const Function& func);
  nlohmann::json PrintBytecode(const Function& func);
  void Print(
      nlohmann::json& passes,
      const Function& func,
      const char* pass_name,
      std::size_t time_ns);
  nlohmann::json Print(const CFG& cfg);
  nlohmann::json Print(const BasicBlock& instr);
  nlohmann::json Print(const Instr& instr);
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

void DebugPrint(const Function& func);
void DebugPrint(const CFG& cfg);
void DebugPrint(const BasicBlock& block);
void DebugPrint(const Instr& instr);

} // namespace jit::hir
