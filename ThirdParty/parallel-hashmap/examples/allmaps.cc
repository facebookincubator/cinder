// Silly program just to test the natvis file for Visual Studio
// ------------------------------------------------------------
#include <string>
#include "parallel_hashmap/phmap.h"

template<class Set, class F>
void test_set(const F &f)
{
    Set s;
    typename Set::iterator it;
    for (int i=0; i<100; ++i)
        s.insert(f(i));

    it = s.begin();
    ++it;

	it = s.end();
    it = s.begin();
    while(it != s.end())
        ++it;
    it = s.begin();
}

int main(int, char **)
{
    using namespace std;

    auto make_int    = [](int i) { return i; };
    auto make_string = [](int i) { return std::to_string(i); };

    auto make_2int    = [](int i) { return std::make_pair(i, i); };
    auto make_2string = [](int i) { return std::make_pair(std::to_string(i), std::to_string(i)); };
    

    test_set<phmap::flat_hash_set<int>>(make_int);
    test_set<phmap::flat_hash_set<string>>(make_string);

    test_set<phmap::node_hash_set<int>>(make_int);
    test_set<phmap::node_hash_set<string>>(make_string);

    test_set<phmap::flat_hash_map<int, int>>(make_2int);
    test_set<phmap::flat_hash_map<string, string>>(make_2string);

    test_set<phmap::node_hash_map<int, int>>(make_2int);
    test_set<phmap::node_hash_map<string, string>>(make_2string);

    test_set<phmap::parallel_flat_hash_set<int>>(make_int);
    test_set<phmap::parallel_flat_hash_set<string>>(make_string);

    test_set<phmap::parallel_node_hash_set<int>>(make_int);
    test_set<phmap::parallel_node_hash_set<string>>(make_string);

    test_set<phmap::parallel_flat_hash_map<int, int>>(make_2int);
    test_set<phmap::parallel_flat_hash_map<string, string>>(make_2string);

    test_set<phmap::parallel_node_hash_map<int, int>>(make_2int);
    test_set<phmap::parallel_node_hash_map<string, string>>(make_2string);

    // example of using default parameters in order to specify the mutex type.
    // 
    // Please be aware that the iterators returned (by find for example) cannot 
    // be safely read in a multithreaded environment. Instead use if_contains(), 
    // which passes a reference value to the callback while holding the submap lock. 
    // Similarly, write access can be done safely using modify_if, try_emplace_l 
    // or lazy_emplace_l.
    // ----------------------------------------------------------------------------
    using Map = phmap::parallel_flat_hash_map<std::size_t, std::size_t,
                                              std::hash<size_t>,
                                              std::equal_to<size_t>, 
                                              std::allocator<std::pair<const size_t, size_t>>, 
                                              4, 
                                              std::mutex>;
    auto make_2size_t    = [](size_t i) { return std::make_pair(i, i); };
    test_set<Map>(make_2size_t);
}
