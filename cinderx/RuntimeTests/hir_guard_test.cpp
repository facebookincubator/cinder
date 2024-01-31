// Copyright (c) Meta Platforms, Inc. and affiliates.
#include <gtest/gtest.h>

#include "cinderx/Jit/hir/hir.h"
#include "cinderx/Jit/hir/optimization.h"
#include "cinderx/Jit/hir/parser.h"
#include "cinderx/Jit/hir/printer.h"
#include "cinderx/Jit/hir/ssa.h"

#include "cinderx/RuntimeTests/fixtures.h"
#include "cinderx/RuntimeTests/testutil.h"

class GuardTest : public RuntimeTest {};

using namespace jit::hir;

static void testFillGuards(const char* hir_source, const char* expected) {
  auto func = HIRParser().ParseHIR(hir_source);
  ASSERT_NE(func, nullptr);
  ASSERT_TRUE(checkFunc(*func, std::cout));
  reflowTypes(*func);
  RefcountInsertion().Run(*func);
  ASSERT_EQ(HIRPrinter(true).ToString(*func), expected);
}

TEST_F(GuardTest, BindFrameStateFromBlock) {
  const char* hir = R"(
fun test {
  bb 0 {
    v0 = LoadArg<0>
    v1 = LoadArg<1>
    Snapshot {
      NextInstrOffset 0
      Stack<0>
      BlockStack {
      }
    }
    Guard v1
    Return v1
  }
}
)";
  const char* expected = R"(fun test {
  bb 0 {
    v1:Object = LoadArg<1>
    Guard v1 {
      LiveValues<1> b:v1
      FrameState {
        NextInstrOffset 0
      }
    }
    Incref v1
    Return v1
  }
}
)";
  EXPECT_NO_FATAL_FAILURE(testFillGuards(hir, expected));
}

TEST_F(GuardTest, BindFrameStateFromInstr) {
  const char* hir = R"(fun test {
  bb 0 {
    v0 = LoadArg<0>
    Snapshot {
      NextInstrOffset 0
      Stack<0>
      BlockStack {
      }
    }
    v1 = LoadGlobal<0>
    CheckExc v1 {
      FrameState {
        NextInstrOffset 2
        Stack<1> v1
      }
    }
    Snapshot {
      NextInstrOffset 2
      Stack<1> v1
    }
    Guard v1
    Return v1
  }
}
)";
  const char* expected = R"(fun test {
  bb 0 {
    v1:Object = LoadGlobal<0> {
      FrameState {
        NextInstrOffset 0
      }
    }
    CheckExc v1 {
      LiveValues<1> o:v1
      FrameState {
        NextInstrOffset 2
        Stack<1> v1
      }
    }
    Guard v1 {
      LiveValues<1> o:v1
      FrameState {
        NextInstrOffset 2
        Stack<1> v1
      }
    }
    Return v1
  }
}
)";
  EXPECT_NO_FATAL_FAILURE(testFillGuards(hir, expected));
}

TEST_F(GuardTest, BindFrameStateFromInstrWithStack) {
  const char* hir = R"(
fun __main__:test {
  bb 0 {
    v0 = LoadArg<0>
    Snapshot {
      NextInstrOffset 0
      Stack<0>
      BlockStack {
      }
    }
    CheckVar<"foo"> v0 {
      FrameState {
        NextInstrOffset 6
        Stack<0>
      }
    }
    v1 = LoadConst<NoneType>
    v2 = LoadGlobal<0>
    CheckExc v2 {
      FrameState {
        NextInstrOffset 6
        Stack<0>
      }
    }
    Snapshot {
      NextInstrOffset 6
      Stack<3> v0 v1 v2
    }
    Guard v2
    v3 = VectorCall<2> v0 v1 v2
    CheckExc v3 {
      FrameState {
        NextInstrOffset 8
        Stack<0>
      }
    }
    Snapshot {
      NextInstrOffset 8
      Stack<1> v3
    }
    Return v3
  }
}
)";
  const char* expected = R"(fun __main__:test {
  bb 0 {
    v0:Object = LoadArg<0>
    CheckVar<"foo"> v0 {
      LiveValues<1> b:v0
      FrameState {
        NextInstrOffset 6
      }
    }
    v1:NoneType = LoadConst<NoneType>
    v2:Object = LoadGlobal<0> {
      LiveValues<2> b:v0 b:v1
      FrameState {
        NextInstrOffset 0
      }
    }
    CheckExc v2 {
      LiveValues<3> b:v0 b:v1 o:v2
      FrameState {
        NextInstrOffset 6
      }
    }
    Guard v2 {
      LiveValues<3> b:v0 b:v1 o:v2
      FrameState {
        NextInstrOffset 6
        Stack<3> v0 v1 v2
      }
    }
    v3:Object = VectorCall<2> v0 v1 v2 {
      LiveValues<3> b:v0 b:v1 o:v2
      FrameState {
        NextInstrOffset 0
      }
    }
    Decref v2
    CheckExc v3 {
      LiveValues<1> o:v3
      FrameState {
        NextInstrOffset 8
      }
    }
    Return v3
  }
}
)";
  EXPECT_NO_FATAL_FAILURE(testFillGuards(hir, expected));
}
