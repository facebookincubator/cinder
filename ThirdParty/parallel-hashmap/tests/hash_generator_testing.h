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
//
// Generates random values for testing. Specialized only for the few types we
// care about.

#ifndef PHMAP_PRIV_HASH_GENERATOR_TESTING_H_
#define PHMAP_PRIV_HASH_GENERATOR_TESTING_H_

#include <stdint.h>
#include <algorithm>
#include <iosfwd>
#include <random>
#include <tuple>
#include <type_traits>
#include <utility>
#include <string>
#include <deque>
#include <functional>

#if PHMAP_HAVE_STD_STRING_VIEW
    #include <string_view>
#endif

#include "hash_policy_testing.h"

namespace phmap {
namespace priv {
namespace hash_internal {
namespace generator_internal {

template <class Container, class = void>
struct IsMap : std::false_type {};

template <class Map>
struct IsMap<Map, phmap::void_t<typename Map::mapped_type>> : std::true_type {};

}  // namespace generator_internal

namespace 
{
    class RandomDeviceSeedSeq {
    public:
        using result_type = typename std::random_device::result_type;

        template <class Iterator>
        void generate(Iterator start, Iterator end) {
            while (start != end) {
                *start = gen_();
                ++start;
            }
        }

    private:
        std::random_device gen_;
    };
}  // namespace

std::mt19937_64* GetSharedRng(); // declaration

std::mt19937_64* GetSharedRng() {
    RandomDeviceSeedSeq seed_seq;
    static auto* rng = new std::mt19937_64(seed_seq);
    return rng;
}


enum Enum {
    kEnumEmpty,
    kEnumDeleted,
};

enum class EnumClass : uint64_t {
  kEmpty,
  kDeleted,
};

inline std::ostream& operator<<(std::ostream& o, const EnumClass& ec) {
  return o << static_cast<uint64_t>(ec);
}

template <class T, class E = void>
struct Generator;

template <class T>
struct Generator<T, typename std::enable_if<std::is_integral<T>::value>::type> {
    T operator()() const {
        std::uniform_int_distribution<T> dist;
        return dist(*GetSharedRng());
    }
};

template <>
struct Generator<Enum> {
    Enum operator()() const {
        std::uniform_int_distribution<typename std::underlying_type<Enum>::type> dist;

        while (true) {
            auto variate = dist(*GetSharedRng());
            if (variate != kEnumEmpty && variate != kEnumDeleted)
                return static_cast<Enum>(variate);
        }
    }
};

template <>
struct Generator<EnumClass> {
    EnumClass operator()() const {
        std::uniform_int_distribution<
            typename std::underlying_type<EnumClass>::type> dist;
        while (true) {
            EnumClass variate = static_cast<EnumClass>(dist(*GetSharedRng()));
            if (variate != EnumClass::kEmpty && variate != EnumClass::kDeleted)
                return static_cast<EnumClass>(variate);
        }
    }
};

template <>
struct Generator<std::string> {
    std::string operator()() const {
        // NOLINTNEXTLINE(runtime/int)
        std::uniform_int_distribution<short> chars(0x20, 0x7E);
        std::string res;
        res.resize(32);
        std::generate(res.begin(), res.end(),
                      [&]() { return (char)chars(*GetSharedRng()); });
        return res;
    }
};

#if PHMAP_HAVE_STD_STRING_VIEW
template <>
struct Generator<std::string_view> {
    std::string_view operator()() const {
        static auto* arena = new std::deque<std::string>();
        // NOLINTNEXTLINE(runtime/int)
        std::uniform_int_distribution<short> chars(0x20, 0x7E);
        arena->emplace_back();
        auto& res = arena->back();
        res.resize(32);
        std::generate(res.begin(), res.end(),
                      [&]() { return (char)chars(*GetSharedRng()); });
        return res;
    }
};
#endif

template <>
struct Generator<NonStandardLayout> {
    NonStandardLayout operator()() const {
        return NonStandardLayout(Generator<std::string>()());
    }
};

template <class K, class V>
struct Generator<std::pair<K, V>> {
    std::pair<K, V> operator()() const {
        return std::pair<K, V>(Generator<typename std::decay<K>::type>()(),
                               Generator<typename std::decay<V>::type>()());
    }
};

template <class... Ts>
struct Generator<std::tuple<Ts...>> {
    std::tuple<Ts...> operator()() const {
        return std::tuple<Ts...>(Generator<typename std::decay<Ts>::type>()()...);
    }
};

template <class U>
struct Generator<U, phmap::void_t<decltype(std::declval<U&>().key()),
                                decltype(std::declval<U&>().value())>>
    : Generator<std::pair<
          typename std::decay<decltype(std::declval<U&>().key())>::type,
          typename std::decay<decltype(std::declval<U&>().value())>::type>> {};

template <class Container>
using GeneratedType = decltype(
    std::declval<const Generator<
        typename std::conditional<generator_internal::IsMap<Container>::value,
                                  typename Container::value_type,
                                  typename Container::key_type>::type>&>()());

}  // namespace hash_internal
}  // namespace priv
}  // namespace phmap

namespace std
{
    using phmap::priv::hash_internal::EnumClass;
    using phmap::priv::hash_internal::Enum;

    template<> 
    struct hash<EnumClass>
    {
        std::size_t operator()(EnumClass const &p) const { return (std::size_t)p; }
    };
    template<> 
    struct hash<Enum>
    {
        std::size_t operator()(Enum const &p) const { return (std::size_t)p; }
    };
}


#endif  // PHMAP_PRIV_HASH_GENERATOR_TESTING_H_
