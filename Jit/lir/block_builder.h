// Copyright (c) Meta Platforms, Inc. and affiliates. (http://www.meta.com)

#pragma once

#include "Jit/codegen/code_section.h"
#include "Jit/codegen/environ.h"
#include "Jit/hir/hir.h"
#include "Jit/lir/lir.h"

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
  //
  // If the block name already exists, then this returns the existing block.
  BasicBlock* allocateBlock(std::string_view label);

  // Append a block to the CFG.
  void appendBlock(BasicBlock* block);

  // Allocate and append a new instruction to the instruction stream.
  template <class... Args>
  Instruction* appendInstr(Instruction::Opcode opcode, Args&&... args) {
    auto instr = cur_bb_->allocateInstr(opcode, cur_hir_instr_);
    return instr->addOperands(convertArg(std::forward<Args>(args))...);
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
    auto [it, inserted] = env_->output_map.emplace(dest->name(), instr);
    JIT_CHECK(inserted, "HIR value '%s' defined twice in LIR", dest->name());
    return instr;
  }

  void AppendCode(std::string_view s) {
    AppendTokenizedCodeLine(Tokenize(s));
  }
  void AppendCode(const fmt::memory_buffer& buf) {
    AppendCode(std::string_view(buf.data(), buf.size()));
  }
  void AppendLabel(std::string_view s);

  template <typename... T>
  void AppendCode(fmt::format_string<T...> s, T&&... args) {
    fmt::memory_buffer buf;
    fmt::format_to(std::back_inserter(buf), s, std::forward<T>(args)...);
    AppendCode(buf);
  }

  template <
      typename FuncReturnType,
      typename... FuncArgs,
      typename... AppendArgs>
  void AppendCall(
      hir::Register* dst,
      FuncReturnType (*func)(FuncArgs...),
      AppendArgs&&... args) {
    static_assert(
        !std::is_void_v<FuncReturnType>,
        "AppendCall cannot be used with functions that return void.");
    AppendCallInternal(dst, func, std::forward<AppendArgs>(args)...);
  }

  template <
      typename FuncReturnType,
      typename... FuncArgs,
      typename... AppendArgs>
  void AppendInvoke(FuncReturnType (*func)(FuncArgs...), AppendArgs&&... args) {
    static_assert(
        std::is_void_v<FuncReturnType>,
        "AppendInvoke can only be used with functions that return void.");
    AppendCallInternal(nullptr, func, std::forward<AppendArgs>(args)...);
  }

  Instruction* createInstr(Instruction::Opcode opcode);

  Instruction* getDefInstr(const std::string& name);
  Instruction* getDefInstr(const hir::Register* reg);

  void CreateInstrInput(Instruction* instr, const std::string& name);
  void CreateInstrOutput(
      Instruction* instr,
      const std::string& name,
      Operand::DataType data_type);
  void CreateInstrInputFromStr(
      Instruction* instr,
      const std::string& name_size);
  void CreateInstrImmediateInputFromStr(
      Instruction* instr,
      const std::string& val_size);
  void CreateInstrOutputFromStr(
      Instruction* instr,
      const std::string& name_size);

  void CreateInstrIndirect(
      Instruction* instr,
      const std::string& base,
      const std::string& index,
      int multiplier, // log2(scale)
      int offset);
  void CreateInstrIndirectFromStr(
      Instruction* instr,
      const std::string& name_size,
      int offset);
  void CreateInstrIndirectOutputFromStr(
      Instruction* instr,
      const std::string& name_size,
      int offset);
  void SetBlockSection(const std::string& label, codegen::CodeSection section);

  std::vector<BasicBlock*> Generate() {
    return bbs_;
  }

 private:
  const hir::Instr* cur_hir_instr_{nullptr};
  std::optional<std::size_t> cur_deopt_metadata_;
  BasicBlock* cur_bb_;
  std::vector<BasicBlock*> bbs_;
  jit::codegen::Environ* env_;
  Function* func_;
  std::unordered_map<std::string, BasicBlock*> label_to_bb_;

  auto convertArg(hir::Register* src) {
    return VReg{getDefInstr(src->name()), hirTypeToDataType(src->type())};
  }

  auto convertArg(Instruction* instr) {
    return VReg{instr, instr->output()->dataType()};
  }

  template <class Arg>
  auto convertArg(Arg&& arg) {
    return std::forward<Arg>(arg);
  }

  BasicBlock* GetBasicBlockByLabel(const std::string& label);

  template <
      typename FuncReturnType,
      typename... FuncArgs,
      typename... AppendArgs>
  void AppendCallInternal(
      hir::Register* dst,
      FuncReturnType (*func)(FuncArgs...),
      AppendArgs&&... args) {
    static_assert(
        sizeof...(FuncArgs) == sizeof...(AppendArgs),
        "The number of parameters the function accepts and the number of "
        "arguments passed is different.");

    auto instr = createInstr(Instruction::kCall);
    GenericCreateInstrInput(instr, func);

    // Although the static_assert above will fail if this is false, the compiler
    // will still attempt to instatiate AppendCallArguments, which will result
    // in a ton of error spew that hides the actual error that we've generated.
    if constexpr (sizeof...(FuncArgs) == sizeof...(AppendArgs)) {
      AppendCallArguments<sizeof...(FuncArgs), 0, std::tuple<FuncArgs...>>(
          instr, std::forward<AppendArgs>(args)...);
    }

    if (dst != nullptr) {
      GenericCreateInstrOutput(instr, dst);
    }
  }

  template <
      size_t ArgsLeft,
      size_t CurArg,
      typename FuncArgTuple,
      typename... AppendArgs>
  std::enable_if_t<ArgsLeft == 0, void> AppendCallArguments(
      Instruction*,
      AppendArgs&&...) {}

  template <
      size_t ArgsLeft,
      size_t CurArg,
      typename FuncArgTuple,
      typename... AppendArgs>
  std::enable_if_t<ArgsLeft != 0, void> AppendCallArguments(
      Instruction* instr,
      AppendArgs&&... args) {
    using CurArgType = std::remove_cv_t<std::remove_reference_t<
        std::tuple_element_t<CurArg, std::tuple<AppendArgs...>>>>;
    using CurFuncArgType = std::tuple_element_t<CurArg, FuncArgTuple>;
    auto&& cur_arg = std::get<CurArg>(std::forward_as_tuple(args...));
    if constexpr (std::is_same_v<CurFuncArgType, PyThreadState*>) {
      static constexpr char asm_thread_state[] = "__asm_tstate";
      static_assert(
          std::is_same_v<CurArgType, hir::Register*> ||
              std::is_same_v<
                  CurArgType,
                  std::remove_cv_t<decltype(asm_thread_state)>>,
          "Thread state must be passed in a register or explicitly as "
          "\"__asm_tstate\".");
      JIT_CHECK(
          std::string_view(asm_thread_state) == cur_arg,
          "The thread state was passed as a string that wasn't __asm_tstate.");
    } else if constexpr (
        std::is_same_v<CurArgType, hir::Register*> ||
        std::is_same_v<CurArgType, std::string>) {
      // Could add a runtime check here to ensure the type of the register is
      // correct, at least for non-temp-register args, but not doing that
      // currently.
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
    GenericCreateInstrInput(instr, cur_arg);
    AppendCallArguments<ArgsLeft - 1, CurArg + 1, FuncArgTuple>(
        instr, std::forward<AppendArgs>(args)...);
  }

  template <typename T>
  void GenericCreateInstrInput(Instruction* instr, const T& val) {
    using CurArgType = std::remove_cv_t<std::remove_reference_t<T>>;
    if constexpr (std::is_same_v<CurArgType, hir::Register*>) {
      if (val == nullptr) {
        instr->allocateImmediateInput(
            static_cast<uint64_t>(0), Operand::DataType::k64bit);
      } else {
        auto tp = val->type();
        auto dat = hirTypeToDataType(tp);
        if (tp.hasDoubleSpec()) {
          instr->allocateImmediateInput(
              bit_cast<uint64_t>(tp.doubleSpec()), dat);
        } else if (tp.hasIntSpec()) {
          instr->allocateImmediateInput(
              static_cast<uint64_t>(tp.intSpec()), dat);
        } else if (tp.hasObjectSpec()) {
          env_->code_rt->addReference(tp.objectSpec());
          instr->allocateImmediateInput(
              reinterpret_cast<uint64_t>(tp.objectSpec()),
              Operand::DataType::kObject);
        } else {
          CreateInstrInput(instr, val->name());
        }
      }
    } else if constexpr (
        std::is_same_v<CurArgType, std::string> ||
        std::is_same_v<CurArgType, std::string_view> ||
        std::is_same_v<CurArgType, char*> || std::is_array_v<CurArgType>) {
      CreateInstrInput(instr, val);
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
      static_assert(!std::is_same_v<T, T>, "Unknown instruction input type!");
    }
  }

  void GenericCreateInstrOutput(Instruction* instr, hir::Register* dst) {
    CreateInstrOutput(instr, dst->name(), hirTypeToDataType(dst->type()));
  }

  void AppendTokenizedCodeLine(const std::vector<std::string>& tokens);

  bool IsConstant(std::string_view s) {
    return isdigit(s[0]) || (s[0] == '-');
  }

  static bool IsLabel(std::string_view s) {
    return s.back() == ':';
  }

  void createBasicInstr(
      Instruction::Opcode opc,
      bool has_output,
      int arg_count,
      const std::vector<std::string>& tokens);
  void createBasicCallInstr(
      const std::vector<std::string>& tokens,
      bool is_invoke,
      bool is_vector_call);
  static std::vector<std::string> Tokenize(std::string_view s);
};

} // namespace jit::lir
