// Copyright (c) Meta Platforms, Inc. and affiliates.
#include <gtest/gtest.h>

#include "cinderx/Jit/hir/alias_class.h"

using namespace jit::hir;

TEST(AliasClassTest, Equality) {
  EXPECT_EQ(ACellItem, ACellItem);
  EXPECT_NE(ACellItem, AOther);
  EXPECT_NE(ATypeAttrCache, AEmpty);
}

TEST(AliasClassTest, Subsets) {
  EXPECT_TRUE(ACellItem <= AAny);
  EXPECT_TRUE(ACellItem < AAny);
  EXPECT_TRUE(AEmpty <= AEmpty);
  EXPECT_TRUE(AEmpty < ACellItem);
  EXPECT_TRUE(AAny <= AAny);

  EXPECT_FALSE(ACellItem < AOther);
  EXPECT_FALSE(AAny <= AListItem);
}

TEST(AliasClassTest, Combine) {
  EXPECT_TRUE(ACellItem < (ACellItem | AOther));
  EXPECT_EQ(ACellItem & AGlobal, AEmpty);
  EXPECT_EQ(AAny & AListItem, AListItem);
  EXPECT_EQ(AAny | AOther, AAny);
}

TEST(AliasClassTest, ToString) {
  EXPECT_EQ(AAny.toString(), "Any");
  EXPECT_EQ(AGlobal.toString(), "Global");
  EXPECT_EQ((AListItem | AFuncAttr).toString(), "{FuncAttr|ListItem}");
}
