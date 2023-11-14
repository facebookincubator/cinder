// Copyright 2018 The Abseil Authors.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      https://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#ifndef THIS_HASH_SET
    #define THIS_HASH_SET   flat_hash_set
    #define THIS_TEST_NAME  FlatHashSet
#endif

#include "parallel_hashmap/phmap.h"

#include <vector>

#include "hash_generator_testing.h"
#include "unordered_set_constructor_test.h"
#include "unordered_set_lookup_test.h"
#include "unordered_set_members_test.h"
#include "unordered_set_modifiers_test.h"

namespace phmap {
namespace priv {
namespace {

using ::phmap::priv::hash_internal::Enum;
using ::phmap::priv::hash_internal::EnumClass;
using ::testing::Pointee;
using ::testing::UnorderedElementsAre;
using ::testing::UnorderedElementsAreArray;

template <class T>
using Set =
    phmap::THIS_HASH_SET<T, StatefulTestingHash, StatefulTestingEqual, Alloc<T>>;

using SetTypes =
    ::testing::Types<Set<int>, Set<std::string>, Set<Enum>, Set<EnumClass>>;

INSTANTIATE_TYPED_TEST_SUITE_P(THIS_TEST_NAME, ConstructorTest, SetTypes);
INSTANTIATE_TYPED_TEST_SUITE_P(THIS_TEST_NAME, LookupTest, SetTypes);
INSTANTIATE_TYPED_TEST_SUITE_P(THIS_TEST_NAME, MembersTest, SetTypes);
INSTANTIATE_TYPED_TEST_SUITE_P(THIS_TEST_NAME, ModifiersTest, SetTypes);

#if PHMAP_HAVE_STD_STRING_VIEW
TEST(THIS_TEST_NAME, EmplaceString) {
  std::vector<std::string> v = {"a", "b"};
  phmap::THIS_HASH_SET<std::string_view> hs(v.begin(), v.end());
  //EXPECT_THAT(hs, UnorderedElementsAreArray(v));
}
#endif

TEST(THIS_TEST_NAME, BitfieldArgument) {
  union {
    int n : 1;
  };
  n = 0;
  phmap::THIS_HASH_SET<int> s = {n};
  s.insert(n);
  s.insert(s.end(), n);
  s.insert({n});
  s.erase(n);
  s.count(n);
  s.prefetch(n);
  s.find(n);
  s.contains(n);
  s.equal_range(n);
}

TEST(THIS_TEST_NAME, MergeExtractInsert) {
  struct Hash {
    size_t operator()(const std::unique_ptr<int>& p) const { return *p; }
  };
  struct Eq {
    bool operator()(const std::unique_ptr<int>& a,
                    const std::unique_ptr<int>& b) const {
      return *a == *b;
    }
  };
  phmap::THIS_HASH_SET<std::unique_ptr<int>, Hash, Eq> set1, set2;
  set1.insert(phmap::make_unique<int>(7));
  set1.insert(phmap::make_unique<int>(17));

  set2.insert(phmap::make_unique<int>(7));
  set2.insert(phmap::make_unique<int>(19));

  EXPECT_THAT(set1, UnorderedElementsAre(Pointee(7), Pointee(17)));
  EXPECT_THAT(set2, UnorderedElementsAre(Pointee(7), Pointee(19)));

  set1.merge(set2);

  EXPECT_THAT(set1, UnorderedElementsAre(Pointee(7), Pointee(17), Pointee(19)));
  EXPECT_THAT(set2, UnorderedElementsAre(Pointee(7)));

  auto node = set1.extract(phmap::make_unique<int>(7));
  EXPECT_TRUE(node);
  EXPECT_THAT(node.value(), Pointee(7));
  EXPECT_THAT(set1, UnorderedElementsAre(Pointee(17), Pointee(19)));

  auto insert_result = set2.insert(std::move(node));
  EXPECT_FALSE(node);
  EXPECT_FALSE(insert_result.inserted);
  EXPECT_TRUE(insert_result.node);
  EXPECT_THAT(insert_result.node.value(), Pointee(7));
  EXPECT_EQ(**insert_result.position, 7);
  EXPECT_NE(insert_result.position->get(), insert_result.node.value().get());
  EXPECT_THAT(set2, UnorderedElementsAre(Pointee(7)));

  node = set1.extract(phmap::make_unique<int>(17));
  EXPECT_TRUE(node);
  EXPECT_THAT(node.value(), Pointee(17));
  EXPECT_THAT(set1, UnorderedElementsAre(Pointee(19)));

  node.value() = phmap::make_unique<int>(23);

  insert_result = set2.insert(std::move(node));
  EXPECT_FALSE(node);
  EXPECT_TRUE(insert_result.inserted);
  EXPECT_FALSE(insert_result.node);
  EXPECT_EQ(**insert_result.position, 23);
  EXPECT_THAT(set2, UnorderedElementsAre(Pointee(7), Pointee(23)));
}

}  // namespace
}  // namespace priv
}  // namespace phmap
