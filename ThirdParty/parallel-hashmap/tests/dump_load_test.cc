#include <vector>

#include "gtest/gtest.h"

#include "parallel_hashmap/phmap_dump.h"

namespace phmap {
namespace priv {
namespace {

TEST(DumpLoad, FlatHashSet_uin32) {
    phmap::flat_hash_set<uint32_t> st1 = { 1991, 1202 };

    {
        phmap::BinaryOutputArchive ar_out("./dump.data");
        EXPECT_TRUE(st1.dump(ar_out));
    }

    phmap::flat_hash_set<uint32_t> st2;
    {
        phmap::BinaryInputArchive ar_in("./dump.data");
        EXPECT_TRUE(st2.load(ar_in));
    }
    EXPECT_TRUE(st1 == st2);
}

TEST(DumpLoad, FlatHashMap_uint64_uint32) {
    phmap::flat_hash_map<uint64_t, uint32_t> mp1 = {
        { 78731, 99}, {13141, 299}, {2651, 101} };

    {
        phmap::BinaryOutputArchive ar_out("./dump.data");
        EXPECT_TRUE(mp1.dump(ar_out));
    }

    phmap::flat_hash_map<uint64_t, uint32_t> mp2;
    {
        phmap::BinaryInputArchive ar_in("./dump.data");
        EXPECT_TRUE(mp2.load(ar_in));
    }

    EXPECT_TRUE(mp1 == mp2);
}

TEST(DumpLoad, ParallelFlatHashMap_uint64_uint32) {
    phmap::parallel_flat_hash_map<uint64_t, uint32_t> mp1 = {
        {99, 299}, {992, 2991}, {299, 1299} };

    {
        phmap::BinaryOutputArchive ar_out("./dump.data");
        EXPECT_TRUE(mp1.dump(ar_out));
    }

    phmap::parallel_flat_hash_map<uint64_t, uint32_t> mp2;
    {
        phmap::BinaryInputArchive ar_in("./dump.data");
        EXPECT_TRUE(mp2.load(ar_in));
    }
    EXPECT_TRUE(mp1 == mp2);
}

}
}
}

