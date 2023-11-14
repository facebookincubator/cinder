#include <string>
#include <array>
#include <cstdint>
#include <limits>
#include <random>
#include <utility>
#define PHMAP_ALLOCATOR_NOTHROW 1
#include <parallel_hashmap/phmap.h>

// this is probably the fastest high quality 64bit random number generator that exists.
// Implements Small Fast Counting v4 RNG from PractRand.
class sfc64 {
public:
    using result_type = uint64_t;

    // no copy ctors so we don't accidentally get the same random again
    sfc64(sfc64 const&) = delete;
    sfc64& operator=(sfc64 const&) = delete;

    sfc64(sfc64&&) = default;
    sfc64& operator=(sfc64&&) = default;

    sfc64(std::array<uint64_t, 4> const& _state)
        : m_a(_state[0])
        , m_b(_state[1])
        , m_c(_state[2])
        , m_counter(_state[3]) {}

    static constexpr uint64_t(min)() {
        return (std::numeric_limits<uint64_t>::min)();
    }
    static constexpr uint64_t(max)() {
        return (std::numeric_limits<uint64_t>::max)();
    }

    sfc64()
        : sfc64(UINT64_C(0x853c49e6748fea9b)) {}

    sfc64(uint64_t _seed)
        : m_a(_seed)
        , m_b(_seed)
        , m_c(_seed)
        , m_counter(1) {
        for (int i = 0; i < 12; ++i) {
            operator()();
        }
    }

    void seed() {
        *this = sfc64{std::random_device{}()};
    }

    uint64_t operator()() noexcept {
        auto const tmp = m_a + m_b + m_counter++;
        m_a = m_b ^ (m_b >> right_shift);
        m_b = m_c + (m_c << left_shift);
        m_c = rotl(m_c, rotation) + tmp;
        return tmp;
    }

    // this is a bit biased, but for our use case that's not important.
    uint64_t operator()(uint64_t boundExcluded) noexcept {
#ifdef PHMAP_HAS_UMUL128
        uint64_t h;
        (void)umul128(operator()(), boundExcluded, &h);
        return h;
#else
        return 0;
#endif
    }

    std::array<uint64_t, 4> state() const {
        return {{m_a, m_b, m_c, m_counter}};
    }

    void state(std::array<uint64_t, 4> const& s) {
        m_a = s[0];
        m_b = s[1];
        m_c = s[2];
        m_counter = s[3];
    }

private:
    template <typename T>
    T rotl(T const x, int k) {
        return (x << k) | (x >> (8 * sizeof(T) - k));
    }

    static constexpr int rotation = 24;
    static constexpr int right_shift = 11;
    static constexpr int left_shift = 3;
    uint64_t m_a;
    uint64_t m_b;
    uint64_t m_c;
    uint64_t m_counter;
};


int main()
{
    // Create an unordered_map of three strings (that map to strings)
    using Map = phmap::parallel_node_hash_map<int, int>;
    static size_t const n = 50000000;
    sfc64 rng(123);

    size_t checksum = 0;
    
    if (0)
    {
        size_t const max_rng = n / 20;
        Map map;
        for (size_t i = 0; i < n; ++i) {
            checksum += ++map[static_cast<int>(rng(max_rng))];
        }
    }

    if (0)
    {
        size_t const max_rng = n / 4;
        Map map;
        for (size_t i = 0; i < n; ++i) {
            checksum += ++map[static_cast<int>(rng(max_rng))];
        }
    }

    if (1)
    {
        size_t const max_rng = n / 2;
        Map map;
        for (size_t i = 0; i < n; ++i) {
            checksum += ++map[static_cast<int>(rng(max_rng))];
        }
    }

    if (0)
    {
        Map map;
        for (size_t i = 0; i < n; ++i) {
            checksum += ++map[static_cast<int>(rng())];
        }
    }
    printf("%zu\n", checksum);
}
