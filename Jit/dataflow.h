// Copyright (c) Meta Platforms, Inc. and affiliates. (http://www.meta.com)

#pragma once

#include "Jit/bitvector.h"

#include <algorithm>
#include <list>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace jit::optimizer {

/* This file implements a framework for data-flow analysis based on bit vector
 * operations. DataFlowAnalyzer is a template class, where the template argument
 * represents the type of objects that each bit is associate to. It can be an
 * object of a variable, an expression or even a text string description of the
 * bit. The class can be instantiated directly or derived for a certain specific
 * analysis, such as DataFlowAnalyzer<std::string> analyzer; OR class
 * LivenessAnalysis: public DataFlowAnalyzer<PyObject *> {
 *         // doing your own cool stuff...
 *     };
 *
 *     LivenessAnalysis analyzer;
 *
 * An example of how to use this class can be found in dataflow_test.h in
 * RuntimeTests directory. It implements the example that can be found in
 * Section 8.1 of the book Advanced Compiler Design And Implementation.
 */

struct DataFlowBlock {
  void ConnectTo(DataFlowBlock& block) {
    succ_.insert(&block);
    block.pred_.insert(this);
  }

  jit::util::BitVector gen_;
  jit::util::BitVector kill_;
  jit::util::BitVector in_;
  jit::util::BitVector out_;
  std::unordered_set<DataFlowBlock*> pred_;
  std::unordered_set<DataFlowBlock*> succ_;
};

template <typename T>
class DataFlowAnalyzer {
 public:
  DataFlowAnalyzer()
      : num_bits_(0), entry_block_(nullptr), exit_block_(nullptr) {}

  void AddBlock(DataFlowBlock& block) {
    blocks_.insert(&block);
    block.gen_.SetBitWidth(num_bits_);
    block.kill_.SetBitWidth(num_bits_);
    block.in_.SetBitWidth(num_bits_);
    block.out_.SetBitWidth(num_bits_);
  }

  void SetBlockGenBit(DataFlowBlock& block, const T& bit);
  void SetBlockGenBits(DataFlowBlock& block, const std::vector<T>& bits);
  void SetBlockKillBit(DataFlowBlock& block, const T& bit);
  void SetBlockKillBits(DataFlowBlock& block, const std::vector<T>& bits);
  void SetEntryBlock(DataFlowBlock& block) {
    entry_block_ = &block;
  }
  void SetExitBlock(DataFlowBlock& block) {
    entry_block_ = &block;
  }

  bool GetBlockInBit(const DataFlowBlock& block, const T& bit) const;
  bool GetBlockOutBit(const DataFlowBlock& block, const T& bit) const;

  template <typename F>
  void forEachBlockIn(const DataFlowBlock& block, F per_obj_func) const;

  template <typename F>
  void forEachBlockOut(const DataFlowBlock& block, F per_obj_func) const;

  void AddObject(const T& obj);
  void AddObjects(const std::vector<T>& objs);
  size_t GetObjectIndex(const T& obj) const;

  // This function does forward-flow analysis when forward is set to true.
  // It does backward-flow analysis otherwise.
  void RunAnalysis(bool forward = true);

 private:
  std::unordered_map<T, size_t> obj_to_index_map_;
  std::vector<T> index_to_obj_map_;
  std::unordered_set<DataFlowBlock*> blocks_;
  size_t num_bits_;
  DataFlowBlock* entry_block_;
  DataFlowBlock* exit_block_;
};

template <typename T>
void DataFlowAnalyzer<T>::AddObject(const T& obj) {
  obj_to_index_map_.emplace(obj, num_bits_);
  index_to_obj_map_.emplace_back(obj);
  num_bits_++;

  for (auto& block : blocks_) {
    block->gen_.AddBits(1);
    block->kill_.AddBits(1);
    block->in_.AddBits(1);
    block->out_.AddBits(1);
  }
}

template <typename T>
void DataFlowAnalyzer<T>::AddObjects(const std::vector<T>& objs) {
  for (auto& obj : objs) {
    obj_to_index_map_.emplace(obj, num_bits_);
    index_to_obj_map_.emplace_back(obj);
    num_bits_++;
  }

  auto added_bits = objs.size();
  for (auto& block : blocks_) {
    block->gen_.AddBits(added_bits);
    block->kill_.AddBits(added_bits);
    block->in_.AddBits(added_bits);
    block->out_.AddBits(added_bits);
  }
}

template <typename T>
size_t DataFlowAnalyzer<T>::GetObjectIndex(const T& obj) const {
  return obj_to_index_map_.at(obj);
}

template <typename T>
template <typename F>
void DataFlowAnalyzer<T>::forEachBlockIn(
    const DataFlowBlock& block,
    F per_obj_func) const {
  block.in_.forEachSetBit(
      [&](size_t bit) { per_obj_func(index_to_obj_map_.at(bit)); });
}

template <typename T>
template <typename F>
void DataFlowAnalyzer<T>::forEachBlockOut(
    const DataFlowBlock& block,
    F per_obj_func) const {
  block.out_.forEachSetBit(
      [&](size_t bit) { per_obj_func(index_to_obj_map_.at(bit)); });
}

template <typename T>
void DataFlowAnalyzer<T>::SetBlockGenBit(DataFlowBlock& block, const T& bit) {
  auto pos = obj_to_index_map_.at(bit);
  block.gen_.SetBit(pos);
}

template <typename T>
void DataFlowAnalyzer<T>::SetBlockGenBits(
    DataFlowBlock& block,
    const std::vector<T>& bits) {
  for (const auto& bit : bits) {
    SetBlockGenBit(block, bit);
  }
}

template <typename T>
void DataFlowAnalyzer<T>::SetBlockKillBit(DataFlowBlock& block, const T& bit) {
  auto pos = obj_to_index_map_.at(bit);
  block.kill_.SetBit(pos);
}

template <typename T>
void DataFlowAnalyzer<T>::SetBlockKillBits(
    DataFlowBlock& block,
    const std::vector<T>& bits) {
  for (const auto& bit : bits) {
    SetBlockKillBit(block, bit);
  }
}

template <typename T>
bool DataFlowAnalyzer<T>::GetBlockInBit(
    const DataFlowBlock& block,
    const T& bit) const {
  auto index = obj_to_index_map_.at(bit);
  return block.in_.GetBit(index);
}

template <typename T>
bool DataFlowAnalyzer<T>::GetBlockOutBit(
    const DataFlowBlock& block,
    const T& bit) const {
  auto index = obj_to_index_map_.at(bit);
  return block.out_.GetBit(index);
}

template <typename T>
void DataFlowAnalyzer<T>::RunAnalysis(bool forward) {
  std::list<DataFlowBlock*> blocks;

  std::copy_if(
      blocks_.begin(),
      blocks_.end(),
      std::back_inserter(blocks),
      std::bind1st(
          std::not_equal_to<DataFlowBlock*>(),
          forward ? entry_block_ : exit_block_));

  jit::util::BitVector bv(num_bits_);
  while (!blocks.empty()) {
    auto block = blocks.front();
    blocks.pop_front();

    auto& pred = forward ? block->pred_ : block->succ_;
    auto& succ = forward ? block->succ_ : block->pred_;
    auto& in = forward ? block->in_ : block->out_;
    auto& out = forward ? block->out_ : block->in_;

    jit::util::BitVector new_in(num_bits_);
    bool changed = false;

    for (auto& p : pred) {
      new_in |= (forward ? p->out_ : p->in_);
    }

    changed |= (new_in != in);
    in = std::move(new_in);

    auto new_out = block->gen_ | (in - block->kill_);
    changed |= (new_out != out);
    out = std::move(new_out);

    if (changed) {
      std::copy(succ.begin(), succ.end(), std::back_inserter(blocks));
    }
  }
}

} // namespace jit::optimizer
