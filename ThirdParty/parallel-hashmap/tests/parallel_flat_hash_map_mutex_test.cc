#define THIS_HASH_MAP  parallel_flat_hash_map
#define THIS_TEST_NAME ParallelFlatHashMap

#if 1
    #define THIS_EXTRA_TPL_PARAMS , 4, std::mutex
#else
    #include <boost/thread/locks.hpp>
    #include <boost/thread/shared_mutex.hpp>
    #define THIS_EXTRA_TPL_PARAMS , 4, boost::upgrade_mutex
#endif

#include "parallel_hash_map_test.cc"
