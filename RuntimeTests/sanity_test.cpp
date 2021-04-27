// Copyright (c) Facebook, Inc. and its affiliates. (http://www.facebook.com)
#include "gtest/gtest.h"

#include "fixtures.h"
#include "testutil.h"

#include "Python.h"

class SanityTest : public RuntimeTest {};

TEST_F(SanityTest, CanUsePrivateAPIs) {
  auto g = Ref<>::steal(PyLong_FromLong(100));
  ASSERT_NE(g.get(), nullptr);
  ASSERT_TRUE(PyLong_CheckExact(g.get()));
  ASSERT_EQ(_PyLong_AsInt(g.get()), 100);
}
