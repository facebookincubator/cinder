// Copyright (c) Meta Platforms, Inc. and affiliates.
#include <gtest/gtest.h>

#include "cinderx/Jit/hir/parser.h"
#include "cinderx/Jit/hir/printer.h"

#include "cinderx/RuntimeTests/fixtures.h"
#include "cinderx/RuntimeTests/testutil.h"

#include <memory>

using namespace jit::hir;

using HIRParserTest = RuntimeTest;

TEST_F(HIRParserTest, ParsesHIR) {
  const char* ir =
      R"(fun user_id {
            bb 0 {
              v00 = LoadCurrentFunc
              v0 = InitialYield
              CheckVar<-1> v0 {
              }
              v1 = LoadAttrCached<0> v0
              CheckExc v1 {
              }
              Incref v1
              v0 = YieldValue v2 {
                LiveValues<1> o:v1
              }
              v0 = YieldValue v2 {
                LiveValues<2> o:v1 o:v3
              }
              CondBranch<1, 2> v0
            }
            bb 1 {
              v2 = LoadConst<NoneType>
              Incref v2
              v1 = VectorCall<1> v2 v3
              v1 = VectorCallKW<1> v2 v3
              v1 = VectorCallStatic<1> v2 v3
              v1 = CallExKw v2 v3 v4
              v1 = CallEx v2 v3
              v1 = ImportFrom<2> v3
              v1 = ImportName<2> v3 v4
              Decref v2
              CondBranch<3, 2> v1
            }
            bb 2 {
              v3 = Phi<1, 0> v2 v1
              v4 = Phi<0, 1> v0 v2
              Return v1
            }
            bb 3 {
              RaiseAwaitableError<53,52> v1
            }
         })";

  HIRParser parser;
  std::unique_ptr<Function> func(parser.ParseHIR(ir));

  auto traversal = func->cfg.GetRPOTraversal();
  ASSERT_EQ(traversal.size(), 4);

  auto block = func->cfg.entry_block;
  ASSERT_NE(block, nullptr);
  ASSERT_EQ(block->id, 0);
  ASSERT_EQ(block->cfg, &func->cfg);

  auto blocks_it = func->cfg.blocks.begin();
  auto blocks_end = func->cfg.blocks.end();

  ASSERT_NE(blocks_it, blocks_end);
  ASSERT_EQ(blocks_it->id, 0);
  auto it = blocks_it->begin();
  auto end = blocks_it->end();
  ASSERT_NE(it, end);
  ASSERT_EQ(it->opcode(), Opcode::kLoadCurrentFunc);
  ++it;
  ASSERT_NE(it, end);
  ASSERT_EQ(it->opcode(), Opcode::kInitialYield);
  {
    auto& initial_yield = static_cast<InitialYield&>(*it);
    ASSERT_EQ(initial_yield.live_regs().size(), 0);
    ASSERT_EQ(initial_yield.GetOutput()->name(), "v0");
  }
  ++it;
  ASSERT_EQ(it->opcode(), Opcode::kCheckVar);
  ++it;
  ASSERT_NE(it, end);
  ASSERT_EQ(it->opcode(), Opcode::kLoadAttrCached);
  ++it;
  ASSERT_NE(it, end);
  ASSERT_EQ(it->opcode(), Opcode::kCheckExc);
  ++it;
  ASSERT_NE(it, end);
  ASSERT_EQ(it->opcode(), Opcode::kIncref);
  ++it;
  ASSERT_NE(it, end);
  ASSERT_EQ(it->opcode(), Opcode::kYieldValue);
  {
    auto& yield_value = static_cast<YieldValue&>(*it);
    const auto& reg_states = yield_value.live_regs();
    ASSERT_EQ(reg_states.size(), 1);
    ASSERT_EQ(reg_states.at(0).reg->name(), "v1");
    ASSERT_EQ(reg_states.at(0).ref_kind, RefKind::kOwned);
    ASSERT_EQ(yield_value.GetOutput()->name(), "v0");
    ASSERT_EQ(yield_value.reg()->name(), "v2");
  }
  ++it;
  ASSERT_NE(it, end);
  ASSERT_EQ(it->opcode(), Opcode::kYieldValue);
  {
    auto& yield_value = static_cast<YieldValue&>(*it);
    const auto& reg_states = yield_value.live_regs();
    ASSERT_EQ(reg_states.size(), 2);
    ASSERT_EQ(reg_states.at(0).reg->name(), "v1");
    ASSERT_EQ(reg_states.at(0).ref_kind, RefKind::kOwned);
    ASSERT_EQ(reg_states.at(1).reg->name(), "v3");
    ASSERT_EQ(reg_states.at(1).ref_kind, RefKind::kOwned);
    ASSERT_EQ(yield_value.GetOutput()->name(), "v0");
    ASSERT_EQ(yield_value.reg()->name(), "v2");
  }
  ++it;
  ASSERT_NE(it, end);
  ASSERT_EQ(it->opcode(), Opcode::kCondBranch);
  {
    auto& cond_branch = static_cast<CondBranch&>(*it);
    ASSERT_EQ(cond_branch.true_bb()->id, 1);
    ASSERT_EQ(cond_branch.false_bb()->id, 2);
  }
  ++it;
  ASSERT_EQ(it, end);
  ++blocks_it;

  ASSERT_NE(blocks_it, blocks_end);
  ASSERT_EQ(blocks_it->id, 1);
  it = blocks_it->begin();
  end = blocks_it->end();
  ASSERT_NE(it, end);
  ASSERT_EQ(it->opcode(), Opcode::kLoadConst);
  ++it;
  ASSERT_NE(it, end);
  ASSERT_EQ(it->opcode(), Opcode::kIncref);
  ++it;
  ASSERT_NE(it, end);
  ASSERT_EQ(it->opcode(), Opcode::kVectorCall);
  ASSERT_EQ(static_cast<VectorCall&>(*it).numArgs(), 1);
  ASSERT_EQ(static_cast<VectorCall&>(*it).GetOutput()->name(), "v1");
  ASSERT_EQ(static_cast<VectorCall&>(*it).func()->name(), "v2");
  ASSERT_EQ(static_cast<VectorCall&>(*it).arg(0)->name(), "v3");
  ++it;
  ASSERT_NE(it, end);
  ASSERT_EQ(it->opcode(), Opcode::kVectorCallKW);
  ASSERT_EQ(static_cast<VectorCallKW&>(*it).numArgs(), 1);
  ASSERT_EQ(static_cast<VectorCallKW&>(*it).GetOutput()->name(), "v1");
  ASSERT_EQ(static_cast<VectorCallKW&>(*it).func()->name(), "v2");
  ASSERT_EQ(static_cast<VectorCallKW&>(*it).arg(0)->name(), "v3");
  ++it;
  ASSERT_NE(it, end);
  ASSERT_EQ(it->opcode(), Opcode::kVectorCallStatic);
  ASSERT_EQ(static_cast<VectorCallStatic&>(*it).numArgs(), 1);
  ASSERT_EQ(static_cast<VectorCallStatic&>(*it).GetOutput()->name(), "v1");
  ASSERT_EQ(static_cast<VectorCallStatic&>(*it).func()->name(), "v2");
  ASSERT_EQ(static_cast<VectorCallStatic&>(*it).arg(0)->name(), "v3");
  ++it;
  ASSERT_NE(it, end);
  ASSERT_EQ(it->opcode(), Opcode::kCallExKw);
  ASSERT_EQ(static_cast<CallExKw&>(*it).GetOutput()->name(), "v1");
  ASSERT_EQ(static_cast<CallExKw&>(*it).func()->name(), "v2");
  ASSERT_EQ(static_cast<CallExKw&>(*it).pargs()->name(), "v3");
  ASSERT_EQ(static_cast<CallExKw&>(*it).kwargs()->name(), "v4");
  ++it;
  ASSERT_NE(it, end);
  ASSERT_EQ(it->opcode(), Opcode::kCallEx);
  ASSERT_EQ(static_cast<CallEx&>(*it).GetOutput()->name(), "v1");
  ASSERT_EQ(static_cast<CallEx&>(*it).func()->name(), "v2");
  ASSERT_EQ(static_cast<CallEx&>(*it).pargs()->name(), "v3");
  ++it;
  ASSERT_NE(it, end);
  ASSERT_EQ(it->opcode(), Opcode::kImportFrom);
  ASSERT_EQ(static_cast<ImportFrom&>(*it).GetOutput()->name(), "v1");
  ASSERT_EQ(static_cast<ImportFrom&>(*it).name_idx(), 2);
  ASSERT_EQ(static_cast<ImportFrom&>(*it).module()->name(), "v3");
  ++it;
  ASSERT_NE(it, end);
  ASSERT_EQ(it->opcode(), Opcode::kImportName);
  ASSERT_EQ(static_cast<ImportName&>(*it).GetOutput()->name(), "v1");
  ASSERT_EQ(static_cast<ImportName&>(*it).name_idx(), 2);
  ASSERT_EQ(static_cast<ImportName&>(*it).GetFromList()->name(), "v3");
  ASSERT_EQ(static_cast<ImportName&>(*it).GetLevel()->name(), "v4");
  ++it;
  ASSERT_NE(it, end);
  ASSERT_EQ(it->opcode(), Opcode::kDecref);
  ++it;
  ASSERT_NE(it, end);
  ASSERT_EQ(it->opcode(), Opcode::kCondBranch);
  auto cbr = static_cast<const CondBranch*>(&*it);
  ASSERT_EQ(cbr->true_bb()->id, 3);
  ASSERT_EQ(cbr->false_bb()->id, 2);
  ++it;
  ASSERT_EQ(it, end);
  ++blocks_it;

  ASSERT_NE(blocks_it, blocks_end);
  ASSERT_EQ(blocks_it->id, 2);
  it = blocks_it->begin();
  end = blocks_it->end();
  ASSERT_NE(it, end);
  ASSERT_EQ(it->opcode(), Opcode::kPhi);
  auto phi = static_cast<const Phi*>(&*it);
  ASSERT_EQ(phi->GetOutput()->name(), "v3");
  ASSERT_EQ(phi->basic_blocks().size(), 2);
  ASSERT_EQ(phi->basic_blocks()[0]->id, 0);
  ASSERT_EQ(phi->basic_blocks()[1]->id, 1);
  ASSERT_EQ(phi->NumOperands(), 2);
  ASSERT_EQ(phi->GetOperand(0)->name(), "v1");
  ASSERT_EQ(phi->GetOperand(1)->name(), "v2");
  ++it;
  ASSERT_NE(it, end);
  ASSERT_EQ(it->opcode(), Opcode::kPhi);
  phi = static_cast<const Phi*>(&*it);
  ASSERT_EQ(phi->GetOutput()->name(), "v4");
  ASSERT_EQ(phi->basic_blocks().size(), 2);
  ASSERT_EQ(phi->basic_blocks()[0]->id, 0);
  ASSERT_EQ(phi->basic_blocks()[1]->id, 1);
  ASSERT_EQ(phi->NumOperands(), 2);
  ASSERT_EQ(phi->GetOperand(0)->name(), "v0");
  ASSERT_EQ(phi->GetOperand(1)->name(), "v2");
  ++it;
  ASSERT_EQ(it->opcode(), Opcode::kReturn);
  ++it;
  ASSERT_EQ(it, end);
  ++blocks_it;

  ASSERT_NE(blocks_it, blocks_end);
  ASSERT_EQ(blocks_it->id, 3);
  it = blocks_it->begin();
  end = blocks_it->end();
  ASSERT_NE(it, end);
  ASSERT_EQ(it->opcode(), Opcode::kRaiseAwaitableError);
  auto rae = static_cast<const RaiseAwaitableError*>(&*it);
  ASSERT_EQ(rae->GetOperand(0)->name(), "v1");
  ASSERT_EQ(rae->with_opcode(), BEFORE_ASYNC_WITH);
  ASSERT_EQ(rae->with_prev_opcode(), 53);
  ++blocks_it;

  ASSERT_EQ(blocks_it, blocks_end);
}

TEST_F(HIRParserTest, ParsesFrameState) {
  const char* ir = R"(fun test {
  bb 0 {
    Snapshot {
      NextInstrOffset 0
      Stack<0>
      BlockStack {
      }
    }
    v0 = LoadGlobal<0>
    CheckExc v0
    Snapshot {
      NextInstrOffset 2
      Stack<1> v0
    }
    Branch<1>
  }

  bb 1 {
    Snapshot {
      NextInstrOffset 2
      Stack<1> v0
    }
    Return v0
  }
}
)";

  HIRParser parser;
  std::unique_ptr<Function> func(parser.ParseHIR(ir));
}

TEST_F(HIRParserTest, IgnoresEscapedName) {
  const char* hir_src = R"(
fun test {
  bb 0 {
    v0 = LoadArg<0; "a\n\"bc\\d">
    v1 = LoadArg<1>
    v2 = LoadConst<12>
    Branch<1>
  }
  bb 1 {
    Branch<0>
  }
}
)";
  auto func = HIRParser{}.ParseHIR(hir_src);
  const char* expected_hir = R"(fun test {
  bb 0 (preds 1) {
    v0 = LoadArg<0>
    v1 = LoadArg<1>
    v2 = LoadConst<Bottom>
    Branch<1>
  }

  bb 1 (preds 0) {
    Branch<0>
  }
}
)";
  EXPECT_EQ(HIRPrinter{}.ToString(*func), expected_hir);
}

TEST_F(HIRParserTest, InvokeStaticFunction) {
  const char* hir_src = R"(
fun test {
  bb 0 {
    v0 = LoadArg<0; "a\n\"bc\\d">
    v1 = InvokeStaticFunction<os._exists, 0, Long>
    Return v1
  }
}
)";
  auto func = HIRParser{}.ParseHIR(hir_src);
  const char* expected_hir = R"(fun test {
  bb 0 {
    v0 = LoadArg<0>
    v1 = InvokeStaticFunction<os._exists, 0, Long> {
      FrameState {
        NextInstrOffset 0
      }
    }
    Return v1
  }
}
)";
  EXPECT_EQ(HIRPrinter{}.ToString(*func), expected_hir);
}

TEST_F(HIRParserTest, FormatValue) {
  const char* hir_source = R"(fun test {
  bb 0 {
    v0 = LoadArg<0>
    v0 = CheckVar<"bar"> v0 {
      FrameState {
        NextInstrOffset 2
        Locals<1> v0
      }
    }
    v1 = LoadConst<Nullptr>
    v2 = FormatValue<None> v1 v0 {
      FrameState {
        NextInstrOffset 4
        Locals<1> v0
      }
    }
    Return v2
  }
}
)";
  auto func = HIRParser{}.ParseHIR(hir_source);
  EXPECT_EQ(HIRPrinter{}.ToString(*func), hir_source);
}

TEST_F(HIRParserTest, ParsesReturnType) {
  const char* hir_source = R"(fun test {
  bb 0 {
    v0 = LoadConst<CInt32[0]>
    Return<CInt32> v0
  }
}
)";
  auto func = HIRParser{}.ParseHIR(hir_source);
  EXPECT_EQ(HIRPrinter{}.ToString(*func), hir_source);
}

TEST_F(HIRParserTest, PartialRoundtripWithNames) {
  const char* py_src = R"(
def my_func(a, b, c):
  a.some_attr = b.some_method()
)";

  std::unique_ptr<Function> func;
  ASSERT_NO_FATAL_FAILURE(CompileToHIR(py_src, "my_func", func));
  std::string printed_hir = HIRPrinter{}.ToString(*func);

  // For now, just verify that we can parse the printed HIR into
  // *something*. We can't do a true roundtrip yet since names are ignored by
  // the parser.
  auto parsed_func = HIRParser{}.ParseHIR(printed_hir.c_str());
  ASSERT_NE(parsed_func, nullptr);
}

TEST_F(HIRParserTest, ParseSimple) {
  HIRParser parser;

  EXPECT_EQ(parser.parseType("Top"), TTop);
  EXPECT_EQ(parser.parseType("Bottom"), TBottom);
  EXPECT_EQ(parser.parseType("NoneType"), TNoneType);
  EXPECT_EQ(parser.parseType("Long"), TLong);
  EXPECT_EQ(parser.parseType("ImmortalTuple"), TImmortalTuple);
  EXPECT_EQ(parser.parseType("MortalUser"), TMortalUser);

  EXPECT_EQ(
      parser.parseType("CInt64[123456]"), Type::fromCInt(123456, TCInt64));
  EXPECT_EQ(parser.parseType("CUInt8[42]"), Type::fromCUInt(42, TCUInt8));
  EXPECT_EQ(parser.parseType("CInt32[-5678]"), Type::fromCInt(-5678, TCInt32));
  EXPECT_EQ(parser.parseType("CBool[true]"), Type::fromCBool(true));
  EXPECT_EQ(parser.parseType("CBool[false]"), Type::fromCBool(false));
  EXPECT_EQ(parser.parseType("CBool[banana]"), TBottom);
  EXPECT_EQ(parser.parseType("Bool[True]"), Type::fromObject(Py_True));
  EXPECT_EQ(parser.parseType("Bool[False]"), Type::fromObject(Py_False));
  EXPECT_EQ(parser.parseType("Bool[banana]"), TBottom);

  // Unknown types or unsupported specializations parse to Bottom
  EXPECT_EQ(parser.parseType("Bootom"), TBottom);
  EXPECT_EQ(parser.parseType("Banana"), TBottom);
}

static ::testing::AssertionResult
isLongTypeWithValue(Type actual, Type expected, Py_ssize_t value) {
  if (!(actual <= expected)) {
    return ::testing::AssertionFailure()
        << "Expected " << actual.toString() << " <= " << expected.toString()
        << ", but it was not";
  }
  if (!actual.hasObjectSpec()) {
    return ::testing::AssertionFailure() << "Expected " << actual.toString()
                                         << " to have int spec but it did not";
  }
  PyObject* obj = actual.objectSpec();
  if (PyLong_AsLong(obj) != value) {
    return ::testing::AssertionFailure()
        << "Expected " << actual.toString() << " to be == " << value
        << " but it was not";
  }
  return ::testing::AssertionSuccess();
}

TEST_F(HIRParserTest, ParsePyObject) {
  // Function isn't used directly, it's only here to initialize a new
  // Environment object for the parser.
  const char* source = R"(fun test {
  bb 0 {
    v0 = LoadConst<Long[1]>
    Return<Long[1]> v0
  }
}
)";
  HIRParser parser;
  auto func = parser.ParseHIR(source);

  EXPECT_TRUE(isLongTypeWithValue(parser.parseType("Long[1]"), TLong, 1));
  EXPECT_TRUE(isLongTypeWithValue(
      parser.parseType("ImmortalLongExact[2]"), TImmortalLong, 2));

  EXPECT_EQ(
      parser.parseType("Long[123123123123123123123123123123123123]"), TBottom);
}
