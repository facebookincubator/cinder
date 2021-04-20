#ifndef THIS_HASH_MAP
    #define THIS_HASH_MAP  parallel_flat_hash_map
    #define THIS_TEST_NAME ParallelFlatHashMap
#endif

#include "flat_hash_map_test.cc"

namespace phmap {
namespace priv {
namespace {

TEST(THIS_TEST_NAME, ThreadSafeContains) {
    // We can't test mutable keys, or non-copyable keys with ThisMap.
    // Test that the nodes have the proper API.
    using Map = ThisMap<int, int>;

    {
        // ----------------
        // test if_contains
        // ----------------
        Map m = { {1, 7}, {2, 9} };
        const Map& const_m(m);
    
        auto val = 0; 
        auto get_value = [&val](const int& v) { val = v; };
        EXPECT_TRUE(const_m.if_contains(2, get_value));
        EXPECT_EQ(val, 9);

        EXPECT_FALSE(m.if_contains(3, get_value));
    }

    {
        // --------------
        // test modify_if
        // --------------
        Map m = { {1, 7}, {2, 9} };

        auto set_value = [](int& v) { v = 11; };
        EXPECT_TRUE(m.modify_if(2, set_value));
        EXPECT_EQ(m[2], 11);

        EXPECT_FALSE(m.modify_if(3, set_value)); // because m[3] does not exist
    }

    {
        // ------------------
        // test try_emplace_l
        // ------------------
        Map m = { {1, 7}, {2, 9} };

        // overwrite an existing value
        m.try_emplace_l(2, [](int& v) { v = 5; });
        EXPECT_EQ(m[2], 5);

        // insert a value that is not already present. Will be default initialised to 0 and lambda not called
        m.try_emplace_l(3, 
                        [](int& v) { v = 6; }, // called only when key was already present
                        1);                    // argument to construct new value is key not present
        EXPECT_EQ(m[3], 1);
    
        // insert a value that is not already present, provide argument to value-construct it
        m.try_emplace_l(4, 
                        [](int& ) {},          // called only when key was already present
                        999);                  // argument to construct new value is key not present

        EXPECT_EQ(m[4], 999);
    }

    {
        // --------------------
        // test lazy__emplace_l
        // --------------------
        Map m = { {1, 7}, {2, 9} };
 
        // insert a value that is not already present.
        // right now m[5] does not exist
        m.lazy_emplace_l(5, 
                         [](int& v) { v = 6; },                              // called only when key was already present
                         [](const Map::constructor& ctor) { ctor(5, 13); }); // construct value_type in place when key not present

        EXPECT_EQ(m[5], 13);

        // change a value that is present. Currently m[5] == 13
        m.lazy_emplace_l(5, 
                         [](int& v) { v = 6; },                              // called only when key was already present
                         [](const Map::constructor& ctor) { ctor(5, 13); }); // construct value_type in place when key not present
        EXPECT_EQ(m[5], 6);
    }

    {
        // -------------
        // test erase_if
        // -------------
        Map m = { {1, 7}, {2, 9}, {5, 6} };

        EXPECT_EQ(m.erase_if(9, [](int& v) { assert(0); return v==12; }), false); // m[9] not present - lambda not called
        EXPECT_EQ(m.erase_if(5, [](int& v) { return v==12; }), false);            // m[5] == 6, so erase not performed
        EXPECT_EQ(m[5], 6);
        EXPECT_EQ(m.erase_if(5, [](int& v) { return v==6; }), true);              // lambda returns true, so m[5] erased
        EXPECT_EQ(m[5], 0);
    }

}

}  // namespace
}  // namespace priv
}  // namespace phmap
