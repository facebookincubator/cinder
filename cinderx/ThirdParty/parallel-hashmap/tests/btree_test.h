// ---------------------------------------------------------------------------
// Copyright (c) 2019, Gregory Popovitch - greg7mdp@gmail.com
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
// Includes work from abseil-cpp (https://github.com/abseil/abseil-cpp)
// with modifications.
//
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
// ---------------------------------------------------------------------------

#ifndef PHMAP_CONTAINER_BTREE_TEST_H_
#define PHMAP_CONTAINER_BTREE_TEST_H_

#include <algorithm>
#include <cassert>
#include <random>
#include <string>
#include <utility>
#include <vector>
#include <cstdint>
#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>
#include <cstdlib>
#include <ostream>

#include "gmock/gmock.h"
#include "gtest/gtest.h"

#include "parallel_hashmap/btree.h"
#include "parallel_hashmap/phmap.h"

namespace phmap {

namespace test_internal {

    // A type that counts number of occurrences of the type, the live occurrences of
    // the type, as well as the number of copies, moves, swaps, and comparisons that
    // have occurred on the type. This is used as a base class for the copyable,
    // copyable+movable, and movable types below that are used in actual tests. Use
    // InstanceTracker in tests to track the number of instances.
    class BaseCountedInstance {
    public:
        explicit BaseCountedInstance(int x) : value_(x) {
            ++num_instances_;
            ++num_live_instances_;
        }
        BaseCountedInstance(const BaseCountedInstance& x)
            : value_(x.value_), is_live_(x.is_live_) {
            ++num_instances_;
            if (is_live_) ++num_live_instances_;
            ++num_copies_;
        }
        BaseCountedInstance(BaseCountedInstance&& x)
            : value_(x.value_), is_live_(x.is_live_) {
            x.is_live_ = false;
            ++num_instances_;
            ++num_moves_;
        }
        ~BaseCountedInstance() {
            --num_instances_;
            if (is_live_) --num_live_instances_;
        }

        BaseCountedInstance& operator=(const BaseCountedInstance& x) {
            value_ = x.value_;
            if (is_live_) --num_live_instances_;
            is_live_ = x.is_live_;
            if (is_live_) ++num_live_instances_;
            ++num_copies_;
            return *this;
        }
        BaseCountedInstance& operator=(BaseCountedInstance&& x) {
            value_ = x.value_;
            if (is_live_) --num_live_instances_;
            is_live_ = x.is_live_;
            x.is_live_ = false;
            ++num_moves_;
            return *this;
        }

        bool operator==(const BaseCountedInstance& x) const {
            ++num_comparisons_;
            return value_ == x.value_;
        }

        bool operator!=(const BaseCountedInstance& x) const {
            ++num_comparisons_;
            return value_ != x.value_;
        }

        bool operator<(const BaseCountedInstance& x) const {
            ++num_comparisons_;
            return value_ < x.value_;
        }

        bool operator>(const BaseCountedInstance& x) const {
            ++num_comparisons_;
            return value_ > x.value_;
        }

        bool operator<=(const BaseCountedInstance& x) const {
            ++num_comparisons_;
            return value_ <= x.value_;
        }

        bool operator>=(const BaseCountedInstance& x) const {
            ++num_comparisons_;
            return value_ >= x.value_;
        }

        phmap::weak_ordering compare(const BaseCountedInstance& x) const {
            ++num_comparisons_;
            return value_ < x.value_
                            ? phmap::weak_ordering::less
                            : value_ == x.value_ ? phmap::weak_ordering::equivalent
                            : phmap::weak_ordering::greater;
        }

        int value() const {
            if (!is_live_) std::abort();
            return value_;
        }

        friend std::ostream& operator<<(std::ostream& o,
                                        const BaseCountedInstance& v) {
            return o << "[value:" << v.value() << "]";
        }

        // Implementation of efficient swap() that counts swaps.
        static void SwapImpl(
            BaseCountedInstance& lhs,    // NOLINT(runtime/references)
            BaseCountedInstance& rhs) {  // NOLINT(runtime/references)
            using std::swap;
            swap(lhs.value_, rhs.value_);
            swap(lhs.is_live_, rhs.is_live_);
            ++BaseCountedInstance::num_swaps_;
        }

    private:
        friend class InstanceTracker;

        int value_;

        // Indicates if the value is live, ie it hasn't been moved away from.
        bool is_live_ = true;

        // Number of instances.
        static int num_instances_;

        // Number of live instances (those that have not been moved away from.)
        static int num_live_instances_;

        // Number of times that BaseCountedInstance objects were moved.
        static int num_moves_;

        // Number of times that BaseCountedInstance objects were copied.
        static int num_copies_;

        // Number of times that BaseCountedInstance objects were swapped.
        static int num_swaps_;

        // Number of times that BaseCountedInstance objects were compared.
        static int num_comparisons_;
    };
    
    // Helper to track the BaseCountedInstance instance counters. Expects that the
    // number of instances and live_instances are the same when it is constructed
    // and when it is destructed.
    class InstanceTracker {
    public:
        InstanceTracker()
            : start_instances_(BaseCountedInstance::num_instances_),
              start_live_instances_(BaseCountedInstance::num_live_instances_) {
            ResetCopiesMovesSwaps();
        }
        ~InstanceTracker() {
            if (instances() != 0) std::abort();
            if (live_instances() != 0) std::abort();
        }

        // Returns the number of BaseCountedInstance instances both containing valid
        // values and those moved away from compared to when the InstanceTracker was
        // constructed
        int instances() const {
            return BaseCountedInstance::num_instances_ - start_instances_;
        }

        // Returns the number of live BaseCountedInstance instances compared to when
        // the InstanceTracker was constructed
        int live_instances() const {
            return BaseCountedInstance::num_live_instances_ - start_live_instances_;
        }

        // Returns the number of moves on BaseCountedInstance objects since
        // construction or since the last call to ResetCopiesMovesSwaps().
        int moves() const { return BaseCountedInstance::num_moves_ - start_moves_; }

        // Returns the number of copies on BaseCountedInstance objects since
        // construction or the last call to ResetCopiesMovesSwaps().
        int copies() const {
            return BaseCountedInstance::num_copies_ - start_copies_;
        }

        // Returns the number of swaps on BaseCountedInstance objects since
        // construction or the last call to ResetCopiesMovesSwaps().
        int swaps() const { return BaseCountedInstance::num_swaps_ - start_swaps_; }

        // Returns the number of comparisons on BaseCountedInstance objects since
        // construction or the last call to ResetCopiesMovesSwaps().
        int comparisons() const {
            return BaseCountedInstance::num_comparisons_ - start_comparisons_;
        }

        // Resets the base values for moves, copies, comparisons, and swaps to the
        // current values, so that subsequent Get*() calls for moves, copies,
        // comparisons, and swaps will compare to the situation at the point of this
        // call.
        void ResetCopiesMovesSwaps() {
            start_moves_ = BaseCountedInstance::num_moves_;
            start_copies_ = BaseCountedInstance::num_copies_;
            start_swaps_ = BaseCountedInstance::num_swaps_;
            start_comparisons_ = BaseCountedInstance::num_comparisons_;
        }

    private:
        int start_instances_;
        int start_live_instances_;
        int start_moves_;
        int start_copies_;
        int start_swaps_;
        int start_comparisons_;
    };

    // Copyable, not movable.
    class CopyableOnlyInstance : public BaseCountedInstance {
    public:
        explicit CopyableOnlyInstance(int x) : BaseCountedInstance(x) {}
        CopyableOnlyInstance(const CopyableOnlyInstance& rhs) = default;
        CopyableOnlyInstance& operator=(const CopyableOnlyInstance& rhs) = default;

        friend void swap(CopyableOnlyInstance& lhs, CopyableOnlyInstance& rhs) {
            BaseCountedInstance::SwapImpl(lhs, rhs);
        }

        static bool supports_move() { return false; }
    };

    // Copyable and movable.
    class CopyableMovableInstance : public BaseCountedInstance {
    public:
        explicit CopyableMovableInstance(int x) : BaseCountedInstance(x) {}
        CopyableMovableInstance(const CopyableMovableInstance& rhs) = default;
        CopyableMovableInstance(CopyableMovableInstance&& rhs) = default;
        CopyableMovableInstance& operator=(const CopyableMovableInstance& rhs) =
    default;
        CopyableMovableInstance& operator=(CopyableMovableInstance&& rhs) = default;

        friend void swap(CopyableMovableInstance& lhs, CopyableMovableInstance& rhs) {
            BaseCountedInstance::SwapImpl(lhs, rhs);
        }

        static bool supports_move() { return true; }
    };

    // Only movable, not default-constructible.
    class MovableOnlyInstance : public BaseCountedInstance {
    public:
        explicit MovableOnlyInstance(int x) : BaseCountedInstance(x) {}
        MovableOnlyInstance(MovableOnlyInstance&& other) = default;
        MovableOnlyInstance& operator=(MovableOnlyInstance&& other) = default;

        friend void swap(MovableOnlyInstance& lhs, MovableOnlyInstance& rhs) {
            BaseCountedInstance::SwapImpl(lhs, rhs);
        }

        static bool supports_move() { return true; }
    };

}  // namespace test_internal


namespace priv {

    // Like remove_const but propagates the removal through std::pair.
    template <typename T>
    struct remove_pair_const {
        using type = typename std::remove_const<T>::type;
    };
    template <typename T, typename U>
    struct remove_pair_const<std::pair<T, U> > {
        using type = std::pair<typename remove_pair_const<T>::type,
                               typename remove_pair_const<U>::type>;
    };

    // Utility class to provide an accessor for a key given a value. The default
    // behavior is to treat the value as a pair and return the first element.
    template <typename K, typename V>
    struct KeyOfValue {
        struct type {
            const K& operator()(const V& p) const { return p.first; }
        };
    };

    // Partial specialization of KeyOfValue class for when the key and value are
    // the same type such as in set<> and btree_set<>.
    template <typename K>
    struct KeyOfValue<K, K> {
        struct type {
            const K& operator()(const K& k) const { return k; }
        };
    };

    inline char* GenerateDigits(char buf[16], unsigned val, unsigned maxval) {
        assert(val <= maxval);
        constexpr unsigned kBase = 64;  // avoid integer division.
        unsigned p = 15;
        buf[p--] = 0;
        while (maxval > 0) {
            buf[p--] = ' ' + (val % kBase);
            val /= kBase;
            maxval /= kBase;
        }
        return buf + p + 1;
    }

    template <typename K>
    struct Generator {
        int maxval;
        explicit Generator(int m) : maxval(m) {}
        K operator()(int i) const {
            assert(i <= maxval);
            return K(i);
        }
    };

#if 0
    template <>
    struct Generator<phmap::Time> {
        int maxval;
        explicit Generator(int m) : maxval(m) {}
        phmap::Time operator()(int i) const { return phmap::FromUnixMillis(i); }
    };
#endif

    template <>
    struct Generator<std::string> {
        int maxval;
        explicit Generator(int m) : maxval(m) {}
        std::string operator()(int i) const {
            char buf[16];
            return GenerateDigits(buf, i, maxval);
        }
    };

    template <typename T, typename U>
    struct Generator<std::pair<T, U> > {
        Generator<typename remove_pair_const<T>::type> tgen;
        Generator<typename remove_pair_const<U>::type> ugen;

        explicit Generator(int m) : tgen(m), ugen(m) {}
        std::pair<T, U> operator()(int i) const {
            return std::make_pair(tgen(i), ugen(i));
        }
    };

    // Generate n values for our tests and benchmarks. Value range is [0, maxval].
    inline std::vector<int> GenerateNumbersWithSeed(int n, int maxval, int seed) {
        // NOTE: Some tests rely on generated numbers not changing between test runs.
        // We use std::minstd_rand0 because it is well-defined, but don't use
        // std::uniform_int_distribution because platforms use different algorithms.
        std::minstd_rand0 rng(seed);

        std::vector<int> values;
        phmap::flat_hash_set<int> unique_values;
        if (values.size() < n) {
            for (size_t i = values.size(); i < (size_t)n; i++) {
                int value;
                do {
                    value = static_cast<int>(rng()) % (maxval + 1);
                } while (!unique_values.insert(value).second);

                values.push_back(value);
            }
        }
        return values;
    }

    // Generates n values in the range [0, maxval].
    template <typename V>
    std::vector<V> GenerateValuesWithSeed(int n, int maxval, int seed) {
        const std::vector<int> nums = GenerateNumbersWithSeed(n, maxval, seed);
        Generator<V> gen(maxval);
        std::vector<V> vec;

        vec.reserve(n);
        for (int i = 0; i < n; i++) {
            vec.push_back(gen(nums[i]));
        }

        return vec;
    }

}  // namespace priv

namespace priv {

    // This is a stateful allocator, but the state lives outside of the
    // allocator (in whatever test is using the allocator). This is odd
    // but helps in tests where the allocator is propagated into nested
    // containers - that chain of allocators uses the same state and is
    // thus easier to query for aggregate allocation information.
    template <typename T>
    class CountingAllocator : public std::allocator<T> {
    public:
        using Alloc = std::allocator<T>;
        using AllocTraits = typename std::allocator_traits<Alloc>;
        using pointer = typename AllocTraits::pointer;
        using size_type = typename AllocTraits::size_type;

        CountingAllocator() : bytes_used_(nullptr) {}
        explicit CountingAllocator(int64_t* b) : bytes_used_(b) {}

        template <typename U>
        CountingAllocator(const CountingAllocator<U>& x)
            : Alloc(x), bytes_used_(x.bytes_used_) {}

        pointer allocate(size_type n,
                         std::allocator_traits<std::allocator<void>>::const_pointer hint = nullptr) {
            assert(bytes_used_ != nullptr);
            *bytes_used_ += n * sizeof(T);
            return AllocTraits::allocate(*this, n, hint);
        }

        void deallocate(pointer p, size_type n) {
            AllocTraits::deallocate(*this, p, n);
            assert(bytes_used_ != nullptr);
            *bytes_used_ -= n * sizeof(T);
        }

        template<typename U>
        class rebind {
        public:
            using other = CountingAllocator<U>;
        };

        friend bool operator==(const CountingAllocator& a,
                               const CountingAllocator& b) {
            return a.bytes_used_ == b.bytes_used_;
        }

        friend bool operator!=(const CountingAllocator& a,
                               const CountingAllocator& b) {
            return !(a == b);
        }

        int64_t* bytes_used_;
    };

}  // namespace priv

}  // namespace phmap

#endif  // PHMAP_CONTAINER_BTREE_TEST_H_
