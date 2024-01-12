// Copyright (c) Meta Platforms, Inc. and affiliates. (http://www.meta.com)

#include "Jit/hir/printer.h"

#include "Jit/codegen/code_section.h"
#include "Jit/lir/operand.h"
#include "Jit/lir/printer.h"
#include "Jit/lir/x86_64.h"

#include <fmt/ostream.h>

#include <iomanip>

namespace jit::lir {

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
  auto section = block.section();
  // Avoid printing hot sections to keep the printouts a bit less noisy.
  if (section != codegen::CodeSection::kHot) {
    out << " - section: " << codeSectionName(section);
  }
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

nlohmann::json JSONPrinter::print(const Function& func, const char* pass_name) {
  nlohmann::json result;
  result["name"] = pass_name;
  result["type"] = "ssa";
  nlohmann::json blocks;
  for (auto& block : func.basicblocks()) {
    blocks.emplace_back(print(*block));
  }
  result["blocks"] = blocks;
  return result;
}

static nlohmann::json blockList(std::vector<BasicBlock*>& blocks) {
  JIT_CHECK(!blocks.empty(), "should not add empty list");
  nlohmann::json result;
  for (auto block : blocks) {
    result.emplace_back(fmt::format("BB%{}", block->id()));
  }
  return result;
}

static bool endsBlock(const Instruction* instr) {
  return instr->isTerminator() || instr->isAnyBranch();
}

nlohmann::json JSONPrinter::print(const BasicBlock& block) {
  nlohmann::json result;
  result["name"] = fmt::format("BB%{}", block.id());
  std::vector<BasicBlock*> preds(block.predecessors());
  // Predecessors are already sorted; don't sort.
  if (!preds.empty()) {
    result["preds"] = blockList(preds);
  }
  nlohmann::json instrs = nlohmann::json::array();
  for (auto& instr : block.instructions()) {
    if (endsBlock(instr.get())) {
      // Handle specially below
      break;
    }
    instrs.emplace_back(print(*instr));
  }
  result["instrs"] = instrs;
  {
    const Instruction* instr = block.getLastInstr();
    if (instr == nullptr || !endsBlock(instr)) {
      nlohmann::json terminator;
      terminator["opcode"] = "Fallthrough";
      result["terminator"] = terminator;
    } else {
      result["terminator"] = print(*instr);
    }
    std::vector<BasicBlock*> succs(block.successors());
    // Successors are not sorted; sort.
    std::sort(succs.begin(), succs.end(), [](auto& a, auto& b) {
      return a->id() < b->id();
    });
    if (!succs.empty()) {
      result["succs"] = blockList(succs);
    }
  }
  return result;
}

nlohmann::json JSONPrinter::print(const Instruction& instr) {
  nlohmann::json result;
  const Operand* output = instr.output();
  result["line"] = instr.origin() ? instr.origin()->lineNumber() : -1;
  if (instr.origin() && instr.origin()->bytecodeOffset() != 1) {
    result["bytecode_offset"] = instr.origin()->bytecodeOffset().value();
  }
  if (output->type() != OperandBase::kNone) {
    result["output"] = print(*output);
    // TODO(emacs): Type
  }
  // TODO(emacs): Use Instr::opname()
  result["opcode"] = InstrProperty::getProperties(&instr).name;
  nlohmann::json operands = nlohmann::json::array();
  // TODO(enacs): Maybe special case Phi
  instr.foreachInputOperand([&operands, this](const OperandBase* operand) {
    operands.emplace_back(print(*operand));
  });
  result["operands"] = operands;
  return result;
}

std::string JSONPrinter::print(const OperandBase& operand) {
  if (operand.isLinked()) {
    auto linked_opnd =
        static_cast<const LinkedOperand&>(operand).getLinkedOperand();
    return print(*linked_opnd);
  }

  std::stringstream ss;
  switch (operand.type()) {
    case OperandBase::kVreg:
      ss << "%" << operand.instr()->id();
      break;
    case OperandBase::kReg:
      ss << PhyLocation(operand.getPhyRegister());
      break;
    case OperandBase::kStack:
      ss << PhyLocation(operand.getStackSlot());
      break;
    case OperandBase::kMem:
      ss << "[" << std::hex << operand.getMemoryAddress() << "]" << std::dec;
      break;
    case OperandBase::kInd:
      ss << *operand.getMemoryIndirect();
      break;
    case OperandBase::kImm:
      fmt::print(ss, "{0}({0:#x})", operand.getConstant());
      break;
    case OperandBase::kLabel:
      // TODO(emacs): Fix and print as ssa-block
      ss << "BB%" << operand.getBasicBlock()->id();
      break;
    case OperandBase::kNone:
      // TODO(emacs): Fix and print as something else
      ss << "<!!!None!!!>";
      break;
  }

  if (!operand.isLabel()) {
    ss << ":" << operand.getSizeName();
  }

  return ss.str();
}

} // namespace jit::lir
