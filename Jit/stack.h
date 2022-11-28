// Copyright (c) Meta Platforms, Inc. and affiliates. (http://www.meta.com)

#pragma once

#include "Jit/log.h"
#include "Jit/util.h"

#include <initializer_list>
#include <vector>

namespace jit {

template <class T>
class Stack {
 public:
  Stack() = default;
  Stack(std::initializer_list<T> l) : stack_(l) {}

  T pop() {
    JIT_CHECK(!stack_.empty(), "Can't pop from empty stack");
    T result = stack_.back();
    stack_.pop_back();
    return result;
  }

  void discard(std::size_t n) {
    for (std::size_t i = 0; i < n; ++i) {
      pop();
    }
  }

  const T& top(std::size_t idx = 0) const {
    return at(size() - idx - 1);
  }

  void topPut(std::size_t idx, const T& value) {
    atPut(size() - idx - 1, value);
  }

  void push(const T& value) {
    stack_.push_back(value);
  }

  bool isEmpty() const {
    return stack_.empty();
  }

  void clear() {
    stack_.clear();
  }

  T& at(std::size_t idx) {
    return stack_[idx];
  }

  const T& at(std::size_t idx) const {
    return stack_[idx];
  }

  void atPut(std::size_t idx, const T& value) {
    stack_[idx] = value;
  }

  const T& peek(size_t idx) const {
    return at(size() - idx);
  }

  std::size_t size() const {
    return stack_.size();
  }

  bool operator==(const Stack& other) const {
    return stack_ == other.stack_;
  }

  bool operator!=(const Stack& other) const {
    return !(*this == other);
  }

  auto begin() {
    return stack_.begin();
  }

  auto end() {
    return stack_.end();
  }

  auto begin() const {
    return stack_.begin();
  }

  auto end() const {
    return stack_.end();
  }

 private:
  std::vector<T> stack_;
};

} // namespace jit
