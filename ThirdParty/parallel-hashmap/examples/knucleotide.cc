// ------------------------------------------------------------------
// run with: knucleotide 0 < ../examples/knucleotide-input.txt
// ------------------------------------------------------------------
//
// output should be:
// 
// T 31.520
// A 29.600
// C 19.480
// G 19.400
// 
// AT 9.922
// TT 9.602
// TA 9.402
// AA 8.402
// GA 6.321
// TC 6.301
// TG 6.201
// GT 6.041
// CT 5.961
// AG 5.841
// CA 5.461
// AC 5.441
// CC 4.041
// CG 4.021
// GC 3.701
// GG 3.341
// 
// 54	GGT
// 24	GGTA
// 4	GGTATT
// 0	GGTATTTTAATT
// 0	GGTATTTTAATTTATAGT
// ------------------------------------------------------------------
#include <iostream>
#include <iomanip>
#include <cstdint>
#include <string>
#include <algorithm>
#include <map>
#include <thread>
#include <type_traits>
#include <cstring>
#include <array>
#include <vector>
#include <cassert>
#include <parallel_hashmap/phmap.h>

// ------------------------------------------------------------------
constexpr size_t thread_count = 4;

struct Cfg {
    unsigned char *to_char;
    unsigned char to_num[128];
    using Data = std::vector<unsigned char>;

    Cfg() {
        static unsigned char __tochar[] =  {'A', 'C', 'T', 'G'};
        to_char = __tochar;
        to_num[static_cast<unsigned char>('A')] = to_num[static_cast<unsigned char>('a')] = 0;
        to_num[static_cast<unsigned char>('C')] = to_num[static_cast<unsigned char>('c')] = 1;
        to_num[static_cast<unsigned char>('T')] = to_num[static_cast<unsigned char>('t')] = 2;
        to_num[static_cast<unsigned char>('G')] = to_num[static_cast<unsigned char>('g')] = 3;
    }
} const cfg;

// ------------------------------------------------------------------
template <size_t size>
struct Key
{
    // select type to use for 'data', if hash key can fit on 32-bit integer
    // then use uint32_t else use uint64_t.
    using Data = typename std::conditional<size<=16, uint32_t, uint64_t>::type;

    struct Hash {
        Data operator()(const Key& t)const{ return t._data; }
    };

    Key() : _data(0) {
    }

    Key(const char *str) {
        _data = 0;
        for(unsigned i = 0; i < size; ++i){
            _data <<= 2;
            _data |= cfg.to_num[unsigned(str[i])];
        }
    }

    // initialize hash from input data
    void InitKey(const unsigned char *data) {
        for(unsigned i = 0; i < size; ++i){
            _data <<= 2;
            _data |= data[i];
        }
    }

    // updates the key with 1 byte
    void UpdateKey(const unsigned char data) {
        _data <<= 2;
        _data |= data;
    }

    // masks out excess information
    void MaskKey() {
        _data &= _mask;
    }

    // implicit casting operator to string
    operator std::string() const {
        std::string tmp;
        Data data = _data;
        for(size_t i = 0; i != size; ++i, data >>= 2)
            tmp += cfg.to_char[data & 3ull];
        std::reverse(tmp.begin(), tmp.end());
        return tmp;
    }

    bool operator== (const Key& in) const {
        return _data == in._data;
    }
private:
    static constexpr Data _mask = ~(Data(-1) << (2 * size));
    Data _data;
};

// ------------------------------------------------------------------
template <size_t size, typename K = Key<size> >
using HashTable = phmap::flat_hash_map<K, unsigned, typename K::Hash>;

// ------------------------------------------------------------------
template <size_t size>
void Calculate(const Cfg::Data& input, size_t begin, HashTable<size>& table)
{
    // original implementation fully recomputes the hash key for each
    // insert to the hash table. This implementation only partially
    // updates the hash, this is the same with C GCC, Rust #6 and Rust #4
    Key<size> key;
    // initialize key
    key.InitKey(input.data() + begin);
    // use key to increment value
    ++table[key];

    auto itr_begin = input.data() + begin + thread_count;
    auto itr_end = (input.data() + input.size() + 1) - size;
    size_t nsize = std::min(size, thread_count);
    for(;itr_begin < itr_end; itr_begin += thread_count) {
        // update the key 1 byte at a time
        for(unsigned i = 0; i < nsize; ++i)
            key.UpdateKey( itr_begin[i] );

        // then finally mask out excess information
        key.MaskKey();

        // then use key to increment value
        ++table[key];
    }
}

// ------------------------------------------------------------------
template <size_t size>
HashTable<size> CalculateInThreads(const Cfg::Data& input)
{
    HashTable<size> hash_tables[thread_count];
    std::thread threads[thread_count];

    auto invoke = [&](unsigned begin) {
        Calculate<size>(input, begin, hash_tables[begin]);
    };

    for(unsigned i = 0; i < thread_count; ++i)
        threads[i] = std::thread(invoke, i);

    for(auto& i : threads)
        i.join();

    auto& frequencies = hash_tables[0];
    for(unsigned i = 1 ; i < thread_count; ++i)
        for(auto& j : hash_tables[i])
            frequencies[j.first] += j.second;

    // return the 'frequency' by move instead of copy.
    return std::move(frequencies);
}

// ------------------------------------------------------------------
template <unsigned size>
void WriteFrequencies(const Cfg::Data& input)
{
    // we "receive" the returned object by move instead of copy.
    auto&& frequencies = CalculateInThreads<size>(input);
    std::map<unsigned, std::string, std::greater<unsigned>> freq;
    for(const auto& i: frequencies)
        freq.insert({i.second, i.first});

    const unsigned sum = static_cast<unsigned>(input.size()) + 1 - size;
    for(const auto& i : freq)
        std::cout << i.second << ' ' << (sum ? double(100 * i.first) / sum : 0.0) << '\n';
    std::cout << '\n';
}

// ------------------------------------------------------------------
template <unsigned size>
void WriteCount( const Cfg::Data& input, const char *text ) {
    // we "receive" the returned object by move instead of copy.
    auto&& frequencies = CalculateInThreads<size>(input);
    std::cout << frequencies[Key<size>(text)] << '\t' << text << '\n';
}

// ------------------------------------------------------------------
int main()
{
    Cfg::Data data;
    std::array<char, 256> buf;

    while(fgets(buf.data(), static_cast<int>(buf.size()), stdin) && memcmp(">THREE", buf.data(), 6));
    while(fgets(buf.data(), static_cast<int>(buf.size()), stdin) && buf.front() != '>') {
        if(buf.front() != ';'){
            auto i = std::find(buf.begin(), buf.end(), '\n');
            data.insert(data.end(), buf.begin(), i);
        }
    }
    std::transform(data.begin(), data.end(), data.begin(), [](unsigned char c){
        return cfg.to_num[c];
    });
    std::cout << std::setprecision(3) << std::setiosflags(std::ios::fixed);

    WriteFrequencies<1>(data);
    WriteFrequencies<2>(data);
    // value at left is the length of the passed string.
    WriteCount<3>(data, "GGT");
    WriteCount<4>(data, "GGTA");
    WriteCount<6>(data, "GGTATT");
    WriteCount<12>(data, "GGTATTTTAATT");
    WriteCount<18>(data, "GGTATTTTAATTTATAGT");
}
