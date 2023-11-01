// Copyright (c) Meta Platforms, Inc. and affiliates. (http://www.meta.com)

#include "Jit/codegen/copy_graph.h"

namespace jit::codegen {

constexpr int CopyGraph::kTempLoc;

CopyGraph::Node::~Node() {
  if (child_node.isLinked()) {
    child_node.Unlink();
  }
  if (leaf_node.isLinked()) {
    leaf_node.Unlink();
  }
}

void CopyGraph::addEdge(int from, int to) {
  auto parent = getNode(from);
  auto child = getNode(to);
  JIT_DCHECK(child->parent == nullptr, "child already has a parent");
  setParent(child, parent);
}

CopyGraph::Node* CopyGraph::getNode(int loc) {
  auto pair = nodes_.emplace(
      std::piecewise_construct,
      std::forward_as_tuple(loc),
      std::forward_as_tuple(loc));
  if (pair.second) {
    // Every node starts as a leaf.
    leaf_nodes_.PushBack(pair.first->second);
  }
  return &pair.first->second;
}

void CopyGraph::setParent(Node* child, Node* parent) {
  JIT_DCHECK(child != parent, "Can't make node its own parent");
  if (child->child_node.isLinked()) {
    child->child_node.Unlink();
  }
  child->parent = parent;
  if (parent != nullptr) {
    parent->children.PushBack(*child);
    if (parent->leaf_node.isLinked()) {
      parent->leaf_node.Unlink();
    }
  }
}

bool CopyGraph::inRegisterCycle(Node* node) {
  auto cursor = node;
  do {
    if (cursor->loc < 0) {
      return false;
    }
    cursor = cursor->parent;
  } while (cursor != node);
  return true;
}

std::vector<CopyGraph::Op> CopyGraph::process() {
  // The high-level algorithm is:
  //
  //  1. Pick an arbitrary leaf node L. If there are none, goto 5.
  //
  //  2. Generate a copy from L's parent P to L.
  //  3. Remove L from the graph.
  //  4. If P has a parent and is now a leaf node, set L = P and goto
  //     2. Otherwise, goto 1.
  //
  //  5. With no leaf nodes left, all remaining nodes must be part of a
  //     cycle. Since nodes can't have multiple incoming edges, each cycle is a
  //     a simple linked list.
  //
  //  6. Pick an arbitrary node N in the graph. If there are none, return.
  //  7. If the cycle contains any memory locations, goto 11.
  //
  //  8. Clear N's children (there will only be 1) to break the cycle.
  //  9. Generate an exchange between N and N's parent.
  // 10. If N has a parent P, set N = P and goto 9. Otherwise,
  //     remove all nodes in the cycle and goto 6.
  //
  // 11. Generate a copy from N to the temp location.
  // 12. Create a node T for the temp location.
  // 13. Set N's child's parent to T, breaking the cycle and turning N into a
  //     leaf node.
  // 14. Repeat steps 1-4 until no leaf nodes are left. Goto 6.

  std::vector<Op> ops;
  processLeafNodes(ops);

  while (!nodes_.empty()) {
    auto node = &nodes_.begin()->second;

    if (inRegisterCycle(node)) {
      setParent(&node->children.Front(), nullptr);
      while (node->parent != nullptr) {
        ops.emplace_back(Op::Kind::kExchange, node->loc, node->parent->loc);
        auto parent = node->parent;
        nodes_.erase(node->loc);
        node = parent;
      }
      nodes_.erase(node->loc);
      continue;
    }

    ops.emplace_back(Op::Kind::kCopy, node->loc, kTempLoc);
    auto temp_node = getNode(kTempLoc);
    auto child = &node->children.Front();
    setParent(child, temp_node);
    leaf_nodes_.PushBack(*node);
    processLeafNodes(ops);
  }

  return ops;
}

void CopyGraph::processLeafNodes(std::vector<Op>& ops) {
  while (!leaf_nodes_.IsEmpty()) {
    auto node = &leaf_nodes_.Front();
    leaf_nodes_.PopFront();
    auto parent = node->parent;

    ops.emplace_back(Op::Kind::kCopy, parent->loc, node->loc);
    nodes_.erase(node->loc);

    if (parent->children.IsEmpty()) {
      if (parent->parent == nullptr) {
        // The parent has no parent, so this was the last copy in this chain.
        nodes_.erase(parent->loc);
      } else {
        // Process the parent next.
        leaf_nodes_.PushFront(*parent);
      }
    }
  }
}

} // namespace jit::codegen
