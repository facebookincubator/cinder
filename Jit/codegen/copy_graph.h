// Copyright (c) Meta Platforms, Inc. and affiliates. (http://www.meta.com)

#pragma once

#include "Jit/intrusive_list.h"

#include <iterator>
#include <limits>
#include <map>
#include <tuple>
#include <unordered_map>
#include <utility>
#include <vector>

namespace jit::codegen {

// CopyGraph is used to generate a sequence of copies and/or exchanges to
// shuffle data between registers (non-negative ints) and memory locations
// (negative ints).
//
// Every location may have up to one incoming edge and arbitrarily many
// outgoing edges.
//
// CopyGraph::kTempLoc is used to indicate an arbitrary temporary location that
// is used to break cycles involving memory operands. The choice of this
// location, including ensuring that it doesn't conflict with any locations in
// the graph, is up to the caller.
class CopyGraph {
 public:
  static constexpr int kTempLoc = std::numeric_limits<int>::max();

  struct Op {
    enum class Kind {
      kCopy,
      kExchange,
    };

    Op(Kind kind, int from, int to) : kind{kind}, from{from}, to{to} {}

    bool operator==(const Op& other) const {
      return kind == other.kind && from == other.from && to == other.to;
    }

    Kind kind;
    int from;
    int to;
  };

  // Add a copy edge to the graph.
  void addEdge(int from, int to);

  // Process the graph and return the sequence of copies and/or exchanges.
  std::vector<Op> process();

  // Check if the copy graph is empty.
  bool isEmpty() const {
    return nodes_.empty();
  }

 private:
  struct Node {
    explicit Node(int loc) : loc{loc} {}
    ~Node();

    bool operator<(const Node& other) const {
      return loc < other.loc;
    }

    const int loc;
    Node* parent{nullptr};
    IntrusiveListNode child_node;
    IntrusiveListNode leaf_node;
    IntrusiveList<Node, &Node::child_node> children;

    DISALLOW_COPY_AND_ASSIGN(Node);
  };

  // Create or look up a node for the given location. Newly-created nodes will
  // automatically be added to leaf_nodes_.
  Node* getNode(int loc);

  // Add child as a child of parent, removing parent from leaf_nodes_ if
  // appropriate.
  void setParent(Node* child, Node* parent);

  // Process all leaf nodes in the graph, putting any necessary operations in
  // ops.
  void processLeafNodes(std::vector<Op>& ops);

  // Given a node in a cycle, returns true iff the cycle contains only register
  // (non-negative) locations.
  bool inRegisterCycle(Node* node);

  // All nodes in the graph, keyed by location.
  std::map<int, Node> nodes_;

  // All nodes with no outgoing edges (children).
  IntrusiveList<Node, &Node::leaf_node> leaf_nodes_;
};

// the same as CopyGraph, but preserves certain types of `from` nodes.
template <typename FromType>
class CopyGraphWithType : public CopyGraph {
 public:
  struct Op : CopyGraph::Op {
    Op(const CopyGraph::Op& op, FromType t) : CopyGraph::Op(op), type(t) {}

    const FromType type;
  };

  void addEdge(int from, int to, FromType type) {
    auto pair = from_types_.emplace(from, type);
    JIT_DCHECK(
        pair.second || pair.first->second == type,
        "Different type for from %d.",
        from);

    CopyGraph::addEdge(from, to);
  }

  std::vector<Op> process() {
    auto ops = CopyGraph::process();
    std::vector<Op> ret;
    ret.reserve(ops.size());

    for (auto& op : ops) {
      auto from_type = map_get(from_types_, op.from);
      ret.emplace_back(op, from_type);

      if (op.to == kTempLoc) {
        from_types_[kTempLoc] = from_type;
      }
    }

    return ret;
  }

 private:
  std::unordered_map<int, std::remove_cv_t<FromType>> from_types_;
};

} // namespace jit::codegen
