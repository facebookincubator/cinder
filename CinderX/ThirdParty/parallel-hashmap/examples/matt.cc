#include <chrono>
#include <ctime>
#include <cstring>
#include <cstdlib>
#include <cstdio>
#include <cmath>
#include <vector>
#include <random>
#include <parallel_hashmap/phmap.h>
#include <parallel_hashmap/btree.h>

// -------------------------------------------------------------------
// -------------------------------------------------------------------
class Timer
{
public:
    Timer(std::string name) : _name(name), _start(std::chrono::high_resolution_clock::now()) {}

    ~Timer() 
    {
        std::chrono::duration<float> elapsed_seconds = std::chrono::high_resolution_clock::now() - _start;
        printf("%s: %.3fs\n", _name.c_str(), elapsed_seconds.count());
    }

private:
    std::string _name;
    std::chrono::high_resolution_clock::time_point _start;
};

// --------------------------------------------------------------------------
//  from: https://github.com/preshing/RandomSequence
// --------------------------------------------------------------------------
class RSU
{
private:
    uint32_t m_index;
    uint32_t m_intermediateOffset;

    static uint32_t permuteQPR(uint32_t x)
    {
        static const uint32_t prime = 4294967291u;
        if (x >= prime)
            return x;  // The 5 integers out of range are mapped to themselves.
        uint32_t residue = ((unsigned long long) x * x) % prime;
        return (x <= prime / 2) ? residue : prime - residue;
    }

public:
    RSU(uint32_t seedBase, uint32_t seedOffset)
    {
        m_index = permuteQPR(permuteQPR(seedBase) + 0x682f0161);
        m_intermediateOffset = permuteQPR(permuteQPR(seedOffset) + 0x46790905);
    }

    uint32_t next()
    {
        return permuteQPR((permuteQPR(m_index++) + m_intermediateOffset) ^ 0x5bf03635);
    }
};

using Perturb = std::function<void (std::vector<uint64_t> &)>;

// --------------------------------------------------------------------------
// --------------------------------------------------------------------------
template<class Set, size_t N>
void test(const char *name, Perturb perturb1, Perturb /* perturb2 */)
{
    //phmap::btree_set<uint64_t> s;
    Set s;

    unsigned int seed = 76687;
	RSU rsu(seed, seed + 1);

    for (uint32_t i=0; i<N; ++i)
        s.insert(rsu.next());

    std::vector<uint64_t> order(s.begin(), s.end()); // contains sorted, randomly generated keys (when using phmap::btree_set)
                                                     // or keys in the final order of a Set (when using Set).

    perturb1(order);                      // either keep them in same order, or shuffle them

#if 0
    order.resize(N/4);
    perturb2(order);
#endif

    Timer t(name); // start timer
    Set c;
    //c.reserve(order.size());               // whether this "reserve()" is present or not makes a huge difference
    c.insert(order.begin(), order.end());  // time for inserting the same keys into the set
                                           // should not depend on them being sorted or not.
}

// --------------------------------------------------------------------------
// --------------------------------------------------------------------------
template <class T, size_t N>
using pset = phmap::parallel_flat_hash_set<T, 
                                           phmap::priv::hash_default_hash<T>,
                                           phmap::priv::hash_default_eq<T>,
                                           phmap::priv::Allocator<T>, // alias for std::allocator
                                           N>;

// --------------------------------------------------------------------------
// --------------------------------------------------------------------------
int main()
{
    auto shuffle = [](std::vector<uint64_t> &order) { 
        std::random_device rd;
        std::mt19937 g(rd());
        std::shuffle(order.begin(), order.end(), g); 
    };

    auto noop = [](std::vector<uint64_t> &) {};

    auto perturb2 = noop;

    constexpr uint32_t num_keys = 10000000;
    using T = uint64_t;

    test<phmap::flat_hash_set<T>, num_keys>("flat_hash_set ordered ", noop, perturb2);

    test<phmap::flat_hash_set<T>, num_keys>("flat_hash_set shuffled", shuffle, perturb2);

    test<pset<T, 4>, num_keys>("parallel (16) ordered ", noop, perturb2);

    test<pset<T, 4>, num_keys>("parallel (16) shuffled", shuffle, perturb2);

    test<pset<T, 6>, num_keys>("parallel (64) ordered ", noop, perturb2);

    test<pset<T, 6>, num_keys>("parallel (64) shuffled", shuffle, perturb2);

    test<pset<T, 8>, num_keys>("parallel (256) ordered ", noop, perturb2);

    test<pset<T, 8>, num_keys>("parallel (256) shuffled", shuffle, perturb2);
}
    
    
    
    
