// Copyright (c) Meta Platforms, Inc. and affiliates.
#include <gtest/gtest.h>

#include "cinderx/Jit/intrusive_list.h"

using jit::IntrusiveList;
using jit::IntrusiveListNode;

struct Entry {
  Entry(int value) : value(value), node() {}

  int value;
  IntrusiveListNode node;
};

using EntryList = IntrusiveList<Entry, &Entry::node>;

TEST(IntrusiveListTest, NewlyCreatedListIsEmpty) {
  EntryList entries;
  ASSERT_TRUE(entries.IsEmpty());
}

TEST(IntrusiveListTest, PushFrontOnEmptyListUpdatesFrontAndBack) {
  EntryList entries;
  Entry entry(100);
  entries.PushFront(entry);
  EXPECT_EQ(entries.Front().value, 100);
  EXPECT_EQ(entries.Back().value, 100);
  EXPECT_FALSE(entries.IsEmpty());
}

TEST(IntrusiveListTest, PushBackOnEmptyListUpdatesFrontAndBack) {
  EntryList entries;
  Entry entry(100);
  entries.PushBack(entry);
  EXPECT_EQ(entries.Front().value, 100);
  EXPECT_EQ(entries.Back().value, 100);
  EXPECT_FALSE(entries.IsEmpty());
}

TEST(IntrusiveListTest, PopFrontUpdatesList) {
  EntryList entries;
  Entry entry1(100);
  entries.PushFront(entry1);
  Entry entry2(200);
  entries.PushFront(entry2);
  Entry entry3(300);
  entries.PushFront(entry3);

  ASSERT_EQ(entries.Front().value, 300);
  ASSERT_EQ(entries.Back().value, 100);

  entries.PopFront();
  ASSERT_EQ(entries.Front().value, 200);
  ASSERT_EQ(entries.Back().value, 100);

  entries.PopFront();
  ASSERT_EQ(entries.Front().value, 100);
  ASSERT_EQ(entries.Back().value, 100);

  entries.PopFront();
  ASSERT_TRUE(entries.IsEmpty());
}

TEST(IntrusiveListTest, ExtractFrontUpdatesList) {
  EntryList entries;
  Entry entry1(100);
  entries.PushFront(entry1);
  Entry entry2(200);
  entries.PushFront(entry2);
  Entry entry3(300);
  entries.PushFront(entry3);

  ASSERT_EQ(entries.ExtractFront().value, 300);
  ASSERT_EQ(entries.ExtractFront().value, 200);
  ASSERT_EQ(entries.ExtractFront().value, 100);
  ASSERT_TRUE(entries.IsEmpty());
}

TEST(IntrusiveListTest, PopBackUpdatesList) {
  EntryList entries;
  Entry entry1(100);
  entries.PushBack(entry1);
  Entry entry2(200);
  entries.PushBack(entry2);
  Entry entry3(300);
  entries.PushBack(entry3);

  ASSERT_EQ(entries.Front().value, 100);
  ASSERT_EQ(entries.Back().value, 300);

  entries.PopBack();
  ASSERT_EQ(entries.Front().value, 100);
  ASSERT_EQ(entries.Back().value, 200);

  entries.PopBack();
  ASSERT_EQ(entries.Front().value, 100);
  ASSERT_EQ(entries.Back().value, 100);

  entries.PopBack();
  ASSERT_TRUE(entries.IsEmpty());
}

TEST(IntrusiveListTest, ExtractBackUpdatesList) {
  EntryList entries;
  Entry entry1(100);
  entries.PushBack(entry1);
  Entry entry2(200);
  entries.PushBack(entry2);
  Entry entry3(300);
  entries.PushBack(entry3);

  ASSERT_EQ(entries.ExtractBack().value, 300);
  ASSERT_EQ(entries.ExtractBack().value, 200);
  ASSERT_EQ(entries.ExtractBack().value, 100);
  ASSERT_TRUE(entries.IsEmpty());
}

TEST(IntrusiveListTest, IsForwardIterable) {
  EntryList entries;
  Entry entry1(100);
  entries.PushBack(entry1);
  Entry entry2(200);
  entries.PushBack(entry2);
  Entry entry3(300);
  entries.PushBack(entry3);

  auto it = entries.begin();
  ASSERT_EQ(it->value, 100);
  it++;
  ASSERT_EQ(it->value, 200);
  ++it;
  ASSERT_EQ(it->value, 300);
  it++;
  ASSERT_TRUE(it == entries.end());
}

TEST(IntrusiveListTest, IsReverseIterable) {
  EntryList entries;
  Entry entry1(100);
  entries.PushBack(entry1);
  Entry entry2(200);
  entries.PushBack(entry2);
  Entry entry3(300);
  entries.PushBack(entry3);

  auto it = entries.rbegin();
  ASSERT_EQ(it->value, 300);
  it++;
  ASSERT_EQ(it->value, 200);
  ++it;
  ASSERT_EQ(it->value, 100);
  it++;
  ASSERT_TRUE(it == entries.rend());
}

TEST(IntrusiveListTest, IsDecrementable) {
  EntryList entries;
  Entry entry1(100);
  entries.PushBack(entry1);
  Entry entry2(200);
  entries.PushBack(entry2);
  Entry entry3(300);
  entries.PushBack(entry3);

  auto it = entries.end();
  it--;
  ASSERT_EQ(it->value, 300);
  --it;
  ASSERT_EQ(it->value, 200);
  it--;
  ASSERT_EQ(it->value, 100);
  ASSERT_TRUE(it == entries.begin());
}

TEST(InstrusiveListTest, CanBeUsedInRangeExpressions) {
  EntryList entries;
  Entry entry1(100);
  entries.PushBack(entry1);
  Entry entry2(200);
  entries.PushBack(entry2);
  Entry entry3(300);
  entries.PushBack(entry3);

  int visited[3] = {-1, -1, -1};
  int idx = 0;
  for (Entry& entry : entries) {
    visited[idx] = entry.value;
    idx++;
  }

  EXPECT_EQ(visited[0], 100);
  EXPECT_EQ(visited[1], 200);
  EXPECT_EQ(visited[2], 300);
}

TEST(InstrusiveListTest, CanBeUsedInRangeExpressionsWithConstReference) {
  EntryList entries;
  Entry entry1(100);
  entries.PushBack(entry1);
  Entry entry2(200);
  entries.PushBack(entry2);
  Entry entry3(300);
  entries.PushBack(entry3);

  int visited[3] = {-1, -1, -1};
  int idx = 0;
  for (const auto& entry : entries) {
    visited[idx] = entry.value;
    idx++;
  }

  EXPECT_EQ(visited[0], 100);
  EXPECT_EQ(visited[1], 200);
  EXPECT_EQ(visited[2], 300);
}

TEST(IntrusiveListTest, CanSpliceEmptyRange) {
  EntryList list1;
  Entry entry1(100);
  list1.PushBack(entry1);
  EntryList list2;
  list2.spliceAfter(entry1, list1);
  ASSERT_TRUE(list2.IsEmpty());
}

TEST(IntrusiveListTest, CanSpliceOneElementRangeOntoEmptyList) {
  EntryList list1;
  Entry entry1(100);
  list1.PushBack(entry1);
  Entry entry2(200);
  list1.PushBack(entry2);

  EntryList list2;
  list2.spliceAfter(entry1, list1);

  ASSERT_FALSE(list2.IsEmpty());
  auto it = list2.begin();
  ASSERT_EQ(it->value, 200);
  it++;
  ASSERT_EQ(it, list2.end());
}

TEST(IntrusiveListTest, CanSpliceMultiElementRangeOntoEmptyList) {
  EntryList list1;
  Entry entry1(100);
  list1.PushBack(entry1);
  Entry entry2(200);
  list1.PushBack(entry2);
  Entry entry3(300);
  list1.PushBack(entry3);

  EntryList list2;
  list2.spliceAfter(entry1, list1);

  ASSERT_FALSE(list2.IsEmpty());
  auto it = list2.begin();
  ASSERT_EQ(it->value, 200);
  it++;
  ASSERT_EQ(it->value, 300);
  it++;
  ASSERT_EQ(it, list2.end());
}

TEST(IntrusiveListTest, CanSpliceOneElementRangeOntoNonEmptyList) {
  EntryList list1;
  Entry entry1(100);
  list1.PushBack(entry1);
  Entry entry2(200);
  list1.PushBack(entry2);

  EntryList list2;
  Entry entry3(300);
  list2.PushBack(entry3);
  Entry entry4(400);
  list2.PushBack(entry4);
  list2.spliceAfter(entry1, list1);

  ASSERT_FALSE(list2.IsEmpty());
  auto it = list2.begin();
  ASSERT_EQ(it->value, 300);
  it++;
  ASSERT_EQ(it->value, 400);
  it++;
  ASSERT_EQ(it->value, 200);
  it++;
  ASSERT_EQ(it, list2.end());
}

TEST(IntrusiveListTest, CanSpliceMultiElementRangeOntoNonEmptyList) {
  EntryList list1;
  Entry entry1(100);
  list1.PushBack(entry1);
  Entry entry2(200);
  list1.PushBack(entry2);
  Entry entry3(300);
  list1.PushBack(entry3);

  EntryList list2;
  Entry entry4(400);
  list2.PushBack(entry4);
  Entry entry5(500);
  list2.PushBack(entry5);
  list2.spliceAfter(entry1, list1);

  ASSERT_FALSE(list2.IsEmpty());
  auto it = list2.begin();
  ASSERT_EQ(it->value, 400);
  it++;
  ASSERT_EQ(it->value, 500);
  it++;
  ASSERT_EQ(it->value, 200);
  it++;
  ASSERT_EQ(it->value, 300);
  it++;
  ASSERT_EQ(it, list2.end());
}

TEST(InstrusiveListTest, CanGetReverseIteratorsToElements) {
  EntryList list;
  Entry entry1(100);
  list.PushBack(entry1);
  Entry entry2(200);
  list.PushBack(entry2);
  Entry entry3(300);
  list.PushBack(entry3);

  auto it1 = list.reverse_iterator_to(entry3);
  ASSERT_EQ(it1->value, 300);
  it1++;
  ASSERT_EQ(it1->value, 200);
  ++it1;
  ASSERT_EQ(it1->value, 100);
  it1++;
  ASSERT_EQ(it1, list.rend());

  auto it2 = list.reverse_iterator_to(entry2);
  ASSERT_EQ(it2->value, 200);
  ++it2;
  ASSERT_EQ(it2->value, 100);
  it2++;
  ASSERT_EQ(it2, list.rend());

  auto it3 = list.reverse_iterator_to(entry1);
  ASSERT_EQ(it3->value, 100);
  ++it3;
  ASSERT_EQ(it3, list.rend());
}
