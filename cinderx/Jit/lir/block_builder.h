// Copyright (c) Meta Platforms, Inc. and affiliates.

#pragma once

#include "cinderx/Jit/codegen/code_section.h"
#include "cinderx/Jit/codegen/environ.h"
#include "cinderx/Jit/hir/hir.h"
#include "cinderx/Jit/lir/block.h"

#include <fmt/format.h>
#include <fmt/ostream.h>

#include <cctype>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>

// XXX: this file needs to be revisited when we optimize HIR-to-LIR translation
// in codegen.cpp/h. Currently, this file is almost an identical copy from
// bbbuilder.h with some interfaces changes so that it works with the new
// LIR.

// This custom formatter is here because of how Generator and BasicBlockBuilder
// stringify LIR before actually generating it.
template <>
struct fmt::formatter<jit::hir::Register*> {
  template <typename ParseContext>
  constexpr auto parse(ParseContext& ctx) {
    return ctx.begin();
  }

  template <typename FormatContext>
  auto format(jit::hir::Register* const& reg, FormatContext& ctx) {
    if (reg->type().hasIntSpec()) {
      return fmt::format_to(
          ctx.out(),
          "{}:{}",
          reg->type().intSpec(),
          reg->type().unspecialized());
    } else if (reg->type().hasDoubleSpec()) {
      return fmt::format_to(
          ctx.out(),
          "{}:{}",
          reg->type().doubleSpec(),
          reg->type().unspecialized());
    } else if (reg->type() <= jit::hir::TPrimitive) {
      return fmt::format_to(
          ctx.out(), "{}:{}", reg->name(), reg->type().toString());
    } else {
      return fmt::format_to(ctx.out(), "{}", reg->name());
    }
  }
};

template <>
struct fmt::formatter<PyObject*> : fmt::formatter<void*> {};

namespace jit::lir {

inline Operand::DataType hirTypeToDataType(hir::Type tp) {
  if (tp <= hir::TCDouble) {
    return Operand::DataType::kDouble;
  } else if (tp <= (hir::TCInt8 | hir::TCUInt8 | hir::TCBool)) {
    return Operand::DataType::k8bit;
  } else if (tp <= (hir::TCInt16 | hir::TCUInt16)) {
    return Operand::DataType::k16bit;
  } else if (tp <= (hir::TCInt32 | hir::TCUInt32)) {
    return Operand::DataType::k32bit;
  } else if (tp <= (hir::TCInt64 | hir::TCUInt64)) {
    return Operand::DataType::k64bit;
  } else {
    return Operand::DataType::kObject;
  }
}

static inline std::pair<std::string, Operand::DataType> getIdAndType(
    const std::string& name) {
  static UnorderedMap<std::string_view, Operand::DataType> typeMap = {
      {"CInt8", Operand::k8bit},
      {"CUInt8", Operand::k8bit},
      {"CBool", Operand::k8bit},
      {"CInt16", Operand::k16bit},
      {"CUInt16", Operand::k16bit},
      {"CInt32", Operand::k32bit},
      {"CUInt32", Operand::k32bit},
      {"CInt64", Operand::k64bit},
      {"CUInt64", Operand::k64bit},
      {"CDouble", Operand::kDouble},
  };
  size_t colon;
  Operand::DataType data_type = Operand::kObject;

  if ((colon = name.find(':')) != std::string::npos) {
    auto type = std::string_view(name).substr(colon + 1);
    auto t = typeMap.find(type);
    if (t != typeMap.end()) {
      data_type = t->second;
    }
    return {name.substr(0, colon), data_type};
  } else {
    return {name, data_type};
  }
}

class BasicBlockBuilder {
 public:
  BasicBlockBuilder(jit::codegen::Environ* env, Function* func);

  void setCurrentInstr(const hir::Instr* inst) {
    cur_hir_instr_ = inst;
    cur_deopt_metadata_ = std::nullopt;
  }

  // Return the id of a DeoptMetadata for the current instruction, returning
  // the same id if called multiple times for the same instruction.
  std::size_t makeDeoptMetadata();

  // Allocate a new block, not yet attached anywhere in the current CFG.
  BasicBlock* allocateBlock();

  // Append a block to the CFG and switch to it.
  void appendBlock(BasicBlock* block);

  // Terminate the current block and switch over to a new one.
  //
  // Any predecessor/successor links are expected to be set up already.
  void switchBlock(BasicBlock* block);

  // Allocate and append a new instruction to the instruction stream.
  template <class... Args>
  Instruction* appendInstr(Instruction::Opcode opcode, Args&&... args) {
    auto instr = cur_bb_->allocateInstr(opcode, cur_hir_instr_);

    return appendInstrArguments(instr, std::forward<Args>(args)...);
  }

  // Allocate and append a new instruction to the instruction stream.
  //
  // The instruction is expecting to produce a VReg and match it to an HIR
  // register.
  template <class... Args>
  Instruction*
  appendInstr(hir::Register* dest, Instruction::Opcode opcode, Args&&... args) {
    auto dest_lir = OutVReg{hirTypeToDataType(dest->type())};
    auto instr = appendInstr(opcode, dest_lir, std::forward<Args>(args)...);
    auto [it, inserted] = env_->output_map.emplace(dest, instr);
    JIT_CHECK(inserted, "HIR value '{}' defined twice in LIR", dest->name());
    return instr;
  }

  // Allocate and append a new instruction to the instruction stream.
  template <class... Args>
  Instruction*
  appendInstr(OutInd dest, Instruction::Opcode opcode, Args&&... args) {
    auto instr = appendInstr(opcode, std::forward<Args>(args)...);
    instr->output()->setMemoryIndirect(
        dest.base, dest.index, dest.multiplier, dest.offset);
    return instr;
  }

  // Allocate and append a new instruction to the instruction stream.
  //
  // The instruction is expecting to produce a VReg and match it to an HIR
  // register.
  template <class... Args>
  Instruction*
  appendInstr(OutMemImm dest, Instruction::Opcode opcode, Args&&... args) {
    auto instr = appendInstr(opcode, std::forward<Args>(args)...);
    instr->output()->setMemoryAddress(dest.value);
    return instr;
  }

  // Allocate and append a new instruction to the instruction stream.
  //
  // The instruction is expecting to produce a VReg and match it to an HIR
  // register.
  template <class... Args>
  Instruction*
  appendInstr(OutVReg dest, Instruction::Opcode opcode, Args&&... args) {
    auto instr = appendInstr(opcode, std::forward<Args>(args)...);
    instr->output()->setVirtualRegister();
    instr->output()->setDataType(dest.data_type);
    return instr;
  }

  // Allocate and append a new branching instruction to the instruction stream.
  template <class Arg>
  Instruction* appendBranch(
      Instruction::Opcode opcode,
      Arg&& arg,
      BasicBlock* true_bb,
      BasicBlock* false_bb) {
    auto instr = appendInstr(opcode, std::forward<Arg>(arg));
    cur_bb_->addSuccessor(true_bb);
    cur_bb_->addSuccessor(false_bb);
    return instr;
  }

  // Allocate and append a new branching instruction which is checking a flag
  Instruction* appendBranch(Instruction::Opcode opcode, BasicBlock* true_bb) {
    auto instr = appendInstr(opcode);
    cur_bb_->addSuccessor(true_bb);
    return instr;
  }

  template <
      typename FuncReturnType,
      typename... FuncArgs,
      typename... AppendArgs>
  Instruction* appendCallInstruction(
      hir::Register* dst,
      FuncReturnType (*func)(FuncArgs...),
      AppendArgs&&... args) {
    static_assert(
        !std::is_void_v<FuncReturnType>,
        "appendCallInstruction cannot be used with functions that return "
        "void.");
    auto instr =
        appendCallInstructionInternal(func, std::forward<AppendArgs>(args)...);
    createInstrOutput(instr, dst);
    return instr;
  }

  template <
      typename FuncReturnType,
      typename... FuncArgs,
      typename... AppendArgs>
  Instruction* appendCallInstruction(
      OutVReg dst,
      FuncReturnType (*func)(FuncArgs...),
      AppendArgs&&... args) {
    static_assert(
        !std::is_void_v<FuncReturnType>,
        "appendCallInstruction cannot be used with functions that return "
        "void.");
    auto instr =
        appendCallInstructionInternal(func, std::forward<AppendArgs>(args)...);
    instr->addOperands(dst);
    return instr;
  }

  template <
      typename FuncReturnType,
      typename... FuncArgs,
      typename... AppendArgs>
  Instruction* appendInvokeInstruction(
      FuncReturnType (*func)(FuncArgs...),
      AppendArgs&&... args) {
    static_assert(
        std::is_void_v<FuncReturnType>,
        "appendInvokeInstruction can only be used with functions that return "
        "void.");
    return appendCallInstructionInternal(
        func, std::forward<AppendArgs>(args)...);
  }

  Instruction* createInstr(Instruction::Opcode opcode);

  Instruction* getDefInstr(const hir::Register* reg);

  void createInstrInput(Instruction* instr, hir::Register* reg);
  void createInstrOutput(Instruction* instr, hir::Register* dst);

  std::vector<BasicBlock*> Generate() {
    return bbs_;
  }

 private:
  const hir::Instr* cur_hir_instr_{nullptr};
  std::optional<std::size_t> cur_deopt_metadata_;
  BasicBlock* cur_bb_{nullptr};
  std::vector<BasicBlock*> bbs_;
  jit::codegen::Environ* env_;
  Function* func_;

  constexpr Instruction* appendInstrArguments(Instruction* instr) {
    return instr;
  }

  template <typename FirstT, typename... T>
  Instruction*
  appendInstrArguments(Instruction* instr, FirstT&& first_arg, T&&... args) {
    genericCreateInstrInput(instr, first_arg);
    return appendInstrArguments(instr, std::forward<T>(args)...);
  }

  template <
      typename FuncReturnType,
      typename... FuncArgs,
      typename... AppendArgs>
  Instruction* appendCallInstructionInternal(
      FuncReturnType (*func)(FuncArgs...),
      AppendArgs&&... args) {
    static_assert(
        sizeof...(FuncArgs) == sizeof...(AppendArgs),
        "The number of parameters the function accepts and the number of "
        "arguments passed is different.");

    auto instr = createInstr(Instruction::kCall);
    genericCreateInstrInput(instr, func);

    // Although the static_assert above will fail if this is false, the compiler
    // will still attempt to instatiate appendCallInstructionArguments, which
    // will result in a ton of error spew that hides the actual error that we've
    // generated.
    if constexpr (sizeof...(FuncArgs) == sizeof...(AppendArgs)) {
      appendCallInstructionArguments<
          sizeof...(FuncArgs),
          0,
          std::tuple<FuncArgs...>>(instr, std::forward<AppendArgs>(args)...);
    }

    return instr;
  }

  template <
      size_t ArgsLeft,
      size_t CurArg,
      typename FuncArgTuple,
      typename... AppendArgs>
  std::enable_if_t<ArgsLeft == 0, void> appendCallInstructionArguments(
      Instruction*,
      AppendArgs&&...) {}

  template <
      size_t ArgsLeft,
      size_t CurArg,
      typename FuncArgTuple,
      typename... AppendArgs>
  std::enable_if_t<ArgsLeft != 0, void> appendCallInstructionArguments(
      Instruction* instr,
      AppendArgs&&... args) {
    using CurArgType = std::remove_cv_t<std::remove_reference_t<
        std::tuple_element_t<CurArg, std::tuple<AppendArgs...>>>>;
    using CurFuncArgType = std::tuple_element_t<CurArg, FuncArgTuple>;
    auto&& cur_arg = std::get<CurArg>(std::forward_as_tuple(args...));
    if constexpr (std::is_same_v<CurFuncArgType, PyThreadState*>) {
      JIT_CHECK(
          cur_arg == env_->asm_tstate,
          "The thread state was passed as a different value than "
          "env_->asm_tstate");
    } else if constexpr (
        std::is_same_v<CurArgType, hir::Register*> ||
        std::is_same_v<CurArgType, std::string>) {
      // Could add a runtime check here to ensure the type of the register is
      // correct, at least for non-temp-register args, but not doing that
      // currently.
    } else if constexpr (std::is_same_v<CurArgType, Instruction*>) {
    } else if constexpr (std::is_pointer_v<CurFuncArgType>) {
      if constexpr (std::is_function_v<CurArgType>) {
        // This came in as a reference to a function, as a bare function is
        // not a valid parameter type. The ref was removed as part of the
        // uniform handling above, so compare without the pointer on the
        // CurFuncArgType.
        static_assert(
            std::is_same_v<CurArgType, std::remove_pointer_t<CurFuncArgType>>,
            "Mismatched function pointer parameter types!");
      } else if constexpr (!std::is_same_v<CurArgType, std::nullptr_t>) {
        static_assert(
            std::is_same_v<CurArgType, CurFuncArgType>,
            "Mismatched function parameter types!");
      }
    } else {
      static_assert(
          std::is_same_v<CurArgType, CurFuncArgType>,
          "Mismatched function parameter types!");
    }
    genericCreateInstrInput(instr, cur_arg);
    appendCallInstructionArguments<ArgsLeft - 1, CurArg + 1, FuncArgTuple>(
        instr, std::forward<AppendArgs>(args)...);
  }

  template <typename T>
  void genericCreateInstrInput(Instruction* instr, const T& val) {
    using CurArgType = std::remove_cv_t<std::remove_reference_t<T>>;
    if constexpr (std::is_same_v<CurArgType, hir::Register*>) {
      if (val == nullptr) {
        instr->allocateImmediateInput(
            static_cast<uint64_t>(0), Operand::DataType::k64bit);
      } else {
        auto tp = val->type();
        auto dat = hirTypeToDataType(tp);
        // We don't turn constant floats into immediates, as we always
        // need to load these from general purpose registers or memory
        // anyways.
        if (tp.hasIntSpec()) {
          instr->allocateImmediateInput(
              static_cast<uint64_t>(tp.intSpec()), dat);
        } else if (tp.hasObjectSpec()) {
          env_->code_rt->addReference(tp.objectSpec());
          instr->allocateImmediateInput(
              reinterpret_cast<uint64_t>(tp.objectSpec()),
              Operand::DataType::kObject);
        } else {
          createInstrInput(instr, val);
        }
      }
    } else if constexpr (std::is_same_v<CurArgType, Instruction*>) {
      instr->allocateLinkedInput(val);
    } else if constexpr (
        std::is_pointer_v<CurArgType> || std::is_function_v<CurArgType>) {
      instr->allocateImmediateInput(
          reinterpret_cast<uint64_t>(val), Operand::DataType::kObject);
    } else if constexpr (std::is_same_v<CurArgType, std::nullptr_t>) {
      instr->allocateImmediateInput(
          static_cast<uint64_t>(0), Operand::DataType::kObject);
    } else if constexpr (std::is_same_v<CurArgType, bool>) {
      instr->allocateImmediateInput(val ? 1 : 0, Operand::DataType::k8bit);
    } else if constexpr (std::is_floating_point_v<CurArgType>) {
      instr->allocateImmediateInput(
          bit_cast<uint64_t>(val), Operand::DataType::kDouble);
    } else if constexpr (std::is_integral_v<CurArgType>) {
      if constexpr (sizeof(CurArgType) == 1) {
        instr->allocateImmediateInput(
            static_cast<uint64_t>(val), Operand::DataType::k8bit);
      } else if constexpr (sizeof(CurArgType) == 2) {
        instr->allocateImmediateInput(
            static_cast<uint64_t>(val), Operand::DataType::k16bit);
      } else if constexpr (sizeof(CurArgType) == 4) {
        instr->allocateImmediateInput(
            static_cast<uint64_t>(val), Operand::DataType::k32bit);
      } else if constexpr (sizeof(CurArgType) == 8) {
        instr->allocateImmediateInput(
            static_cast<uint64_t>(val), Operand::DataType::k64bit);
      } else {
        static_assert(!std::is_same_v<T, T>, "Unknown integral size!");
      }
    } else {
      instr->addOperands(val);
    }
  }
};

} // namespace jit::lir
