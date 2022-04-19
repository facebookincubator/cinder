// Copyright (c) Facebook, Inc. and its affiliates. (http://www.facebook.com)
#pragma once

#include "Jit/codegen/code_section.h"
#include "Jit/codegen/environ.h"
#include "Jit/hir/hir.h"
#include "Jit/lir/lir.h"

#include <fmt/format.h>

#include <cctype>
#include <cstdint>
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

namespace jit {
namespace lir {

class BasicBlockBuilder {
 public:
  BasicBlockBuilder(jit::codegen::Environ* env, Function* func);

  void setCurrentInstr(const hir::Instr* inst) {
    cur_hir_instr_ = inst;
  }

  void AppendCode(const std::string& s);

  template <typename... T>
  void AppendCode(std::string_view s, T&&... args) {
    AppendCode(fmt::format(s, std::forward<T>(args)...));
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

  void CreateInstrInput(Instruction* instr, const std::string& name_size);
  void CreateInstrImmediateInput(
      Instruction* instr,
      const std::string& val_size);
  void CreateInstrOutput(Instruction* instr, const std::string& name_size);

  void CreateInstrIndirect(
      Instruction* instr,
      const std::string& name_size,
      int offset);
  void CreateInstrIndirectOutput(
      Instruction* instr,
      const std::string& name_size,
      int offset);
  void SetBlockSection(
      const std::string& label,
      codegen::CodeSection section);

  std::vector<BasicBlock*> Generate() {
    return bbs_;
  }

 private:
  const hir::Instr* cur_hir_instr_{nullptr};
  BasicBlock* cur_bb_;
  std::vector<BasicBlock*> bbs_;
  jit::codegen::Environ* env_;
  Function* func_;
  std::unordered_map<std::string, BasicBlock*> label_to_bb_;

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
    std::vector<std::string> tokenized_call{};

    if (dst != nullptr) {
      tokenized_call.emplace_back("Call");
      tokenized_call.emplace_back(fmt::format("{}", dst));
    } else {
      tokenized_call.emplace_back("Invoke");
    }

    tokenized_call.emplace_back(
        fmt::format("{:#x}", reinterpret_cast<uint64_t>(func)));

    // Although the static_assert above will fail if this is false, the compiler
    // will still attempt to instatiate AppendCallArguments, which will result
    // in a ton of error spew that hides the actual error that we've generated.
    if constexpr (sizeof...(FuncArgs) == sizeof...(AppendArgs)) {
      AppendCallArguments<sizeof...(FuncArgs), 0, std::tuple<FuncArgs...>>(
          tokenized_call, std::forward<AppendArgs>(args)...);
    }
    AppendTokenizedCodeLine(tokenized_call);
  }

  template <
      size_t ArgsLeft,
      size_t CurArg,
      typename FuncArgTuple,
      typename... AppendArgs>
  std::enable_if_t<ArgsLeft == 0, void> AppendCallArguments(
      std::vector<std::string>&,
      AppendArgs&&...) {}

  template <
      size_t ArgsLeft,
      size_t CurArg,
      typename FuncArgTuple,
      typename... AppendArgs>
  std::enable_if_t<ArgsLeft != 0, void> AppendCallArguments(
      std::vector<std::string>& token_list,
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
      token_list.emplace_back(fmt::format("{}", cur_arg));
    } else if constexpr (
        std::is_same_v<CurArgType, hir::Register*> ||
        std::is_same_v<CurArgType, std::string>) {
      // Could add a runtime check here to ensure the type of the register is
      // correct, at least for non-temp-register args, but not doing that
      // currently.
      token_list.emplace_back(fmt::format("{}", cur_arg));
    } else {
      if constexpr (std::is_pointer_v<CurFuncArgType>) {
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
        token_list.emplace_back(
            fmt::format("{:#x}", reinterpret_cast<uint64_t>(cur_arg)));
      } else {
        static_assert(
            std::is_same_v<CurArgType, CurFuncArgType>,
            "Mismatched function parameter types!");
        if constexpr (std::is_same_v<CurArgType, bool>) {
          token_list.emplace_back(fmt::format("{}", cur_arg ? 1 : 0));
        } else {
          token_list.emplace_back(fmt::format("{}", cur_arg));
        }
      }
    }
    AppendCallArguments<ArgsLeft - 1, CurArg + 1, FuncArgTuple>(
        token_list, std::forward<AppendArgs>(args)...);
  }

  void AppendTokenizedCodeLine(const std::vector<std::string>& tokens);
  void AppendCodeLine(const std::string& s) {
    AppendTokenizedCodeLine(Tokenize(s));
  }

  bool IsConstant(const std::string& s) {
    return isdigit(s[0]) || (s[0] == '-');
  }

  static bool IsLabel(const std::string& s) {
    return s.back() == ':';
  }
  static std::vector<std::string> Tokenize(const std::string& s);
};

} // namespace lir
} // namespace jit
