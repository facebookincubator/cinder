// Copyright (c) Meta Platforms, Inc. and affiliates. (http://www.meta.com)
/*
 * Type-aliases for map and set containers. This acts as a shim to allow
 * swapping between STL, phmap, and one day other container implementations.
 *
 * While phmap types can be used as an exact drop-in for STL, it includes more
 * optimized containers that can be used if you do not need pointer-stability.
 * That is, pointers to container content will be invalidated on container
 * mutation. To this end the "base" types here (Set, Map, OrderedSet,
 * OrderedMap) do not provide pointer-stability. If you need this, use the
 * StablePointer variants where available.
 *
 * Additionally the StablePointer* variants may have better performance if the
 * contained values are more than 100 bytes or so. This arises as they will not
 * be moving so much data around when rebalancing etc.
 *
 * The phmap Big* variants may have better performance if the number of
 * contained elements is very large. I haven't played with them but apparently
 * they might take advantage of multi-threading?
 */

#pragma once

//#define JIT_FORCE_STL_CONTAINERS

#ifdef JIT_FORCE_STL_CONTAINERS
#include <map>
#include <set>
#include <unordered_map>
#include <unordered_set>
#else
#include "parallel_hashmap/btree.h"
#include "parallel_hashmap/phmap.h"
#endif

namespace jit {

#define SET_TEMPLATE_PARAMS <Key, Hash, KeyEqual, Allocator>
#define SET_ORDERED_TEMPLATE_PARAMS <Key, Compare, Allocator>
#define MAP_TEMPLATE_PARAMS <Key, T, Hash, KeyEqual, Allocator>
#define MAP_ORDERED_TEMPLATE_PARAMS <Key, T, Compare, Allocator>

#ifdef JIT_FORCE_STL_CONTAINERS

#define SET_TEMPLATE_ARGS                  \
  template <                               \
      class Key,                           \
      class Hash = std::hash<Key>,         \
      class KeyEqual = std::equal_to<Key>, \
      class Allocator = std::allocator<Key>>

#define SET_ORDERED_TEMPLATE_ARGS     \
  template <                          \
      class Key,                      \
      class Compare = std::less<Key>, \
      class Allocator = std::allocator<Key>>

#define MAP_TEMPLATE_ARGS                  \
  template <                               \
      class Key,                           \
      class T,                             \
      class Hash = std::hash<Key>,         \
      class KeyEqual = std::equal_to<Key>, \
      class Allocator = std::allocator<std::pair<const Key, T>>>

#define MAP_ORDERED_TEMPLATE_ARGS     \
  template <                          \
      class Key,                      \
      class T,                        \
      class Compare = std::less<Key>, \
      class Allocator = std::allocator<std::pair<const Key, T>>>

SET_TEMPLATE_ARGS using UnorderedSet = std::unordered_set SET_TEMPLATE_PARAMS;
MAP_TEMPLATE_ARGS using UnorderedMap = std::unordered_map MAP_TEMPLATE_PARAMS;
SET_TEMPLATE_ARGS using UnorderedStablePointerSet =
    std::unordered_set SET_TEMPLATE_PARAMS;
MAP_TEMPLATE_ARGS using UnorderedStablePointerMap =
    std::unordered_map MAP_TEMPLATE_PARAMS;
SET_TEMPLATE_ARGS using UnorderedBigSet =
    std::unordered_set SET_TEMPLATE_PARAMS;
MAP_TEMPLATE_ARGS using UnorderedBigMap =
    std::unordered_map MAP_TEMPLATE_PARAMS;
SET_TEMPLATE_ARGS using UnorderedBigStablePointerSet =
    std::unordered_set SET_TEMPLATE_PARAMS;
MAP_TEMPLATE_ARGS using UnorderedBigStablePointerMap =
    std::unordered_map MAP_TEMPLATE_PARAMS;
SET_ORDERED_TEMPLATE_ARGS using OrderedSet =
    std::set SET_ORDERED_TEMPLATE_PARAMS;
MAP_ORDERED_TEMPLATE_ARGS using OrderedMap =
    std::map MAP_ORDERED_TEMPLATE_PARAMS;
SET_ORDERED_TEMPLATE_ARGS using OrderedMultiset =
    std::multiset SET_ORDERED_TEMPLATE_PARAMS;
MAP_ORDERED_TEMPLATE_ARGS using OrderedMultimap =
    std::multimap MAP_ORDERED_TEMPLATE_PARAMS;

#else // JIT_FORCE_STL_CONTAINERS

#define SET_TEMPLATE_ARGS                                 \
  template <                                              \
      class Key,                                          \
      class Hash = phmap::priv::hash_default_hash<Key>,   \
      class KeyEqual = phmap::priv::hash_default_eq<Key>, \
      class Allocator = phmap::priv::Allocator<Key>>

#define SET_ORDERED_TEMPLATE_ARGS       \
  template <                            \
      class Key,                        \
      class Compare = phmap::Less<Key>, \
      class Allocator = phmap::Allocator<Key>>

#define MAP_TEMPLATE_ARGS                  \
  template <                               \
      class Key,                           \
      class T,                             \
      class Hash = std::hash<Key>,         \
      class KeyEqual = std::equal_to<Key>, \
      class Allocator = std::allocator<std::pair<const Key, T>>>

#define MAP_ORDERED_TEMPLATE_ARGS       \
  template <                            \
      class Key,                        \
      class T,                          \
      class Compare = phmap::Less<Key>, \
      class Allocator = phmap::Allocator<phmap::priv::Pair<const Key, T>>>

SET_TEMPLATE_ARGS using UnorderedSet = phmap::flat_hash_set SET_TEMPLATE_PARAMS;
MAP_TEMPLATE_ARGS using UnorderedMap = phmap::flat_hash_map MAP_TEMPLATE_PARAMS;
SET_TEMPLATE_ARGS using UnorderedStablePointerSet =
    phmap::node_hash_set SET_TEMPLATE_PARAMS;
MAP_TEMPLATE_ARGS using UnorderedStablePointerMap =
    phmap::node_hash_map MAP_TEMPLATE_PARAMS;
SET_TEMPLATE_ARGS using UnorderedBigSet =
    phmap::parallel_flat_hash_set SET_TEMPLATE_PARAMS;
MAP_TEMPLATE_ARGS using UnorderedBigMap =
    phmap::parallel_flat_hash_map MAP_TEMPLATE_PARAMS;
SET_TEMPLATE_ARGS using UnorderedBigStablePointerSet =
    phmap::parallel_node_hash_set SET_TEMPLATE_PARAMS;
MAP_TEMPLATE_ARGS using UnorderedBigStablePointerMap =
    phmap::parallel_node_hash_map MAP_TEMPLATE_PARAMS;
SET_ORDERED_TEMPLATE_ARGS using OrderedSet =
    phmap::btree_set SET_ORDERED_TEMPLATE_PARAMS;
MAP_ORDERED_TEMPLATE_ARGS using OrderedMap =
    phmap::btree_map MAP_ORDERED_TEMPLATE_PARAMS;
SET_ORDERED_TEMPLATE_ARGS using OrderedMultiset =
    phmap::btree_multiset SET_ORDERED_TEMPLATE_PARAMS;
MAP_ORDERED_TEMPLATE_ARGS using OrderedMultimap =
    phmap::btree_multimap MAP_ORDERED_TEMPLATE_PARAMS;

#endif

#undef SET_TEMPLATE_PARAMS
#undef SET_ORDERED_TEMPLATE_PARAMS
#undef MAP_TEMPLATE_PARAMS
#undef MAP_ORDERED_TEMPLATE_PARAMS

#undef SET_TEMPLATE_ARGS
#undef SET_ORDERED_TEMPLATE_ARGS
#undef MAP_TEMPLATE_ARGS
#undef MAP_ORDERED_TEMPLATE_ARGS

}; // namespace jit
