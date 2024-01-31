// Copyright (c) Meta Platforms, Inc. and affiliates.
#pragma once
#include <algorithm>
#include <iterator>
#include <list>
#include <unordered_map>
#include <utility>

/** an unordered_map that can be iterated using insertion order
 *  Note that iterator related operation (find, directly operations on iterator)
 *  will operate on pair<Key, pair<Value, orderIt>> instead of just
 *  pair<Key, Value> as in regular unordered_map
 */
template <
    typename Key, // sequence_map::key_type
    typename T, // sequence_map::mapped_type
    typename Hash = std::hash<Key>, // sequence_map::hasher
    typename Pred = std::equal_to<Key> // sequence_map::key_equal
    >
class sequence_map {
  using OrderValT = const Key*;
  using OrderT = std::list<OrderValT>; // stores the order of insertion
  using OrderItT = typename OrderT::iterator;

  // maps keys to (value, position in insertion order list)
  using MapT = std::unordered_map<Key, std::pair<T, OrderItT>, Hash, Pred>;
  using MapItT = typename MapT::iterator;
  using MapConstItT = typename MapT::const_iterator;
  using size_type = typename MapT::size_type;

 public:
  // iterator that uses insertion order
  class Iterator {
   public:
    using iterator_category = std::forward_iterator_tag;
    using difference_type = typename OrderItT::difference_type;
    using value_type = typename MapItT::value_type;
    using pointer = value_type*;
    using reference = value_type&;
    // keep track of where we are during iteration
    using it_state = OrderItT;
    Iterator(it_state state, MapT& map) : state_(state), map_(map) {}

    // operations
    reference operator*() const {
      return map_.find(**state_).operator*();
    }

    pointer operator->() {
      return map_.find(**state_).operator->();
    }

    // Prefix increment
    Iterator& operator++() {
      state_++;
      return *this;
    }

    // Postfix increment
    Iterator operator++(int) {
      Iterator tmp = *this;
      ++(*this);
      return tmp;
    }

    friend bool operator==(const Iterator& a, const Iterator& b) {
      return a.state_ == b.state_;
    };

    friend bool operator!=(const Iterator& a, const Iterator& b) {
      return a.state_ != b.state_;
    };

   private:
    it_state state_;
    MapT& map_;
  };

  class ConstIterator {
   public:
    using iterator_category = std::forward_iterator_tag;
    using difference_type = typename OrderItT::difference_type;
    using value_type = const typename MapItT::value_type;
    using pointer = value_type*;
    using reference = value_type&;
    // keep track of where we are during iteration
    using it_state = typename OrderT::const_iterator;
    ConstIterator(it_state state, const MapT& map) : state_(state), map_(map) {}

    // operations
    reference operator*() const {
      return map_.find(**state_).operator*();
    }

    pointer operator->() {
      return map_.find(**state_).operator->();
    }

    // Prefix increment
    ConstIterator& operator++() {
      state_++;
      return *this;
    }

    // Postfix increment
    ConstIterator operator++(int) {
      ConstIterator tmp = *this;
      ++(*this);
      return tmp;
    }

    friend bool operator==(const ConstIterator& a, const ConstIterator& b) {
      return a.state_ == b.state_;
    };

    friend bool operator!=(const ConstIterator& a, const ConstIterator& b) {
      return a.state_ != b.state_;
    };

   private:
    it_state state_;
    const MapT& map_;
  };

  sequence_map() : map(), order() {}
  sequence_map(std::initializer_list<std::pair<Key, T>> l) {
    for (auto& item : l) {
      (*this)[std::move(item.first)] = std::move(item.second);
    }
  }
  sequence_map(sequence_map<Key, T>&& other) : sequence_map<Key, T>() {
    map.reserve(other.size());
    for (auto& item : other) {
      (*this)[std::move(item.first)] = std::move(item.second.first);
    }
  }
  sequence_map(const sequence_map<Key, T>& other) : sequence_map<Key, T>() {
    map.reserve(other.size());
    for (auto& item : other) {
      T val(item.second.first);
      Key key(item.first);
      (*this)[std::move(key)] = std::move(val);
    }
  }

  MapItT find(const Key& key) {
    return map.find(key);
  }

  MapConstItT find(const Key& key) const {
    return map.find(key);
  }

  bool empty() const {
    return map.empty();
  }

  size_type size() const {
    return map.size();
  }

  void reserve(size_type n) {
    map.reserve(n);
  }

  T& operator[](const Key& key) {
    auto map_it = map.find(key);
    if (map_it == map.end()) {
      auto& resPair = map[key];
      // XXX: not sure how to avoid a second find here
      auto inserted_it = map.find(key);
      // store pointer to avoid copying
      order.push_back(&(inserted_it->first));
      resPair.second = std::prev(order.end());
      return resPair.first;
    }

    return map_it->second.first;
  }

  T at(const Key& key) {
    auto v = map.at(key);
    return v.first;
  }

  const T at(const Key& key) const {
    auto v = map.at(key);
    return v.first;
  }

  size_t erase(const Key& key) {
    auto map_it = map.find(key);
    return erase(map_it);
  }

  size_t erase(const MapItT& map_it) {
    if (map_it == map.end()) {
      return 0;
    }
    order.erase(map_it->second.second);
    map.erase(map_it);
    return 1;
  }

  void clear() {
    map.clear();
    order.clear();
  }

  Iterator begin() {
    return Iterator(order.begin(), map);
  }
  Iterator end() {
    return Iterator(order.end(), map);
  }

  ConstIterator begin() const {
    return ConstIterator(order.begin(), map);
  }

  ConstIterator end() const {
    return ConstIterator(order.end(), map);
  }

  ConstIterator cbegin() const {
    return ConstIterator(order.begin(), map);
  }

  ConstIterator cend() const {
    return ConstIterator(order.end(), map);
  }

  MapItT map_end() {
    return map.end();
  }

  MapConstItT map_end() const {
    return map.end();
  }

 private:
  MapT map;
  OrderT order;
};
