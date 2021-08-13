// Copyright (c) Facebook, Inc. and its affiliates. (http://www.facebook.com)
#include "Jit/hir/printer.h"

#include "Jit/lir/operand.h"
#include "Jit/lir/printer.h"
#include "Jit/lir/x86_64.h"

#include <fmt/ostream.h>

#include <iomanip>

namespace jit {
namespace lir {

void Printer::print(std::ostream& out, const Function& func) {
  out << "Function:" << std::endl;
  for (auto& block : func.basicblocks()) {
    print(out, *block);
    out << std::endl;
  }
}

void Printer::print(std::ostream& out, const BasicBlock& block) {
  out << "BB %" << block.id();

  auto print_blocks = [&](const char* which,
                          std::vector<BasicBlock*> blocks,
                          bool is_sorted = true) {
    if (is_sorted) {
      std::sort(blocks.begin(), blocks.end(), [](auto& a, auto& b) {
        return a->id() < b->id();
      });
    }
    if (!blocks.empty()) {
      out << which;
      for (auto& b : blocks) {
        out << " %" << b->id();
      }
    }
  };

  print_blocks(" - preds:", block.predecessors());
  print_blocks(" - succs:", block.successors(), false);
  out << std::endl;

  const hir::Instr* prev_instr = nullptr;
  for (auto& instr : block.instructions()) {
    if (!g_dump_lir_no_origin && instr->origin() != prev_instr) {
      if (instr->origin()) {
        out << std::endl;
        hir_printer_.Print(out, *instr->origin());
        out << std::endl;
      }
      prev_instr = instr->origin();
    }
    print(out, *instr);
    out << std::endl;
  }
}

void Printer::print(std::ostream& out, const Instruction& instr) {
  auto output_opnd = instr.output();
  if (output_opnd->type() == OperandBase::kNone) {
    fmt::print(out, "{:>16}   ", "");
  } else {
    std::stringstream ss;
    print(ss, *output_opnd);
    out << std::setw(16) << ss.str() << " = ";
  }
  out << InstrProperty::getProperties(&instr).name;
  const char* sep = " ";
  if (instr.opcode() == Instruction::kPhi) {
    auto num_inputs = instr.getNumInputs();
    for (size_t i = 0; i < num_inputs; i += 2) {
      out << sep << "(";
      print(out, *(instr.getInput(i)));
      sep = ", ";
      out << sep;
      print(out, *(instr.getInput(i + 1)));
      out << ")";
    }
  } else {
    instr.foreachInputOperand([&sep, &out, this](const OperandBase* operand) {
      out << sep;
      print(out, *operand);
      sep = ", ";
    });
  }
}

void Printer::print(std::ostream& out, const OperandBase& operand) {
  if (operand.isLinked()) {
    auto linked_opnd =
        static_cast<const LinkedOperand&>(operand).getLinkedOperand();
    print(out, *linked_opnd);
    return;
  }

  switch (operand.type()) {
    case OperandBase::kVreg:
      out << "%" << operand.instr()->id();
      break;
    case OperandBase::kReg:
      out << PhyLocation(operand.getPhyRegister());
      break;
    case OperandBase::kStack:
      out << PhyLocation(operand.getStackSlot());
      break;
    case OperandBase::kMem:
      out << "[" << std::hex << operand.getMemoryAddress() << "]" << std::dec;
      break;
    case OperandBase::kInd:
      out << *operand.getMemoryIndirect();
      break;
    case OperandBase::kImm:
      fmt::print(out, "{0}({0:#x})", operand.getConstant());
      break;
    case OperandBase::kLabel:
      out << "BB%" << operand.getBasicBlock()->id();
      break;
    case OperandBase::kNone:
      out << "<!!!None!!!>";
      break;
  }

  if (!operand.isLabel()) {
    out << ":" << operand.getSizeName();
  }
}

void Printer::print(std::ostream& out, const MemoryIndirect& ind) {
  fmt::print(out, "[{}", *ind.getBaseRegOperand());

  auto index_reg = ind.getIndexRegOperand();
  if (index_reg != nullptr) {
    fmt::print(out, " + {}", *index_reg);

    int multiplier = ind.getMultipiler();
    if (multiplier > 0) {
      fmt::print(out, " * {}", 1 << multiplier);
    }
  }

  auto offset = ind.getOffset();
  if (offset != 0) {
    if (offset > 0) {
      fmt::print(out, " + {0:#x}", offset);
    } else {
      fmt::print(out, " - {0:#x}", -offset);
    }
  }

  fmt::print(out, "]");
}

} // namespace lir
} // namespace jit
