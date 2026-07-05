#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

namespace minisql {

// Where a row actually lives, so the index can point to real data without
// storing a copy of it. Executor (later) turns this into an actual row by
// reading pageId's Page and taking the record at recordIndex.
struct RowLocation {
    int32_t pageId;
    size_t recordIndex;
};

// A single node in the tree. Deliberately simple scope for v1 of v2:
//   - in-memory only (no disk persistence for the tree itself)
//   - point lookups only (no leaf sibling chain / range scans)
//   - small fixed fanout, so splits trigger with a handful of inserts
//     instead of needing hundreds - makes testing/reasoning tractable
struct BTreeNode {
    bool isLeaf;
    std::vector<int32_t> keys;

    // Leaf nodes only: values[i] corresponds to keys[i].
    std::vector<RowLocation> values;

    // Internal nodes only: children.size() == keys.size() + 1.
    // children[i] holds all keys < keys[i]; children[i+1] holds keys >= keys[i].
    std::vector<std::unique_ptr<BTreeNode>> children;

    explicit BTreeNode(bool leaf) : isLeaf(leaf) {}
};

class BPlusTree {
public:
    // Small on purpose - see the comment on BTreeNode. A node holds at
    // most MAX_KEYS keys before it needs to split.
    static constexpr size_t MAX_KEYS = 4;

    BPlusTree();

    // THE CORE METHODS - see the guidance above each one.

    // Point lookup: returns the RowLocation for `key`, or std::nullopt if
    // not present.
    //
    // Suggested approach: start at root_. While the current node is NOT
    // a leaf, figure out which child to descend into: walk `keys` to find
    // the first key greater than the search key - that tells you the
    // child index (if all keys are smaller, go to the last child). Once
    // you reach a leaf, look for an exact match in its `keys`.
    std::optional<RowLocation> search(int32_t key) const;

    // Inserts `key` -> `location`. If this causes a node to exceed
    // MAX_KEYS, that node must SPLIT, and the split may need to propagate
    // upward (a split leaf causes its parent to gain a new key+child; if
    // THAT overflows the parent, the parent splits too; if the root
    // itself splits, a brand new root is created above it, making the
    // tree one level taller).
    //
    // Suggested approach: this is naturally recursive. Write a private
    // helper (declare it yourself) that takes a node and returns either
    // "nothing to propagate" or "here's a (splitKey, newRightSiblingNode)
    // pair the caller needs to insert into ITS OWN keys/children." Start
    // simple: get insertion into a non-full leaf working and tested
    // first, WITHOUT splitting, before tackling the split logic at all.
    void insert(int32_t key, RowLocation location);

    // Removes `key` if present. Simple scope: just erase it from its
    // leaf's keys/values. No merging/redistribution if a leaf becomes
    // underfull - a documented v1-of-v2 simplification, same spirit as
    // Page/Catalog's earlier scope cuts.
    void remove(int32_t key);

    // Returns the number of levels in the tree (a leaf-only tree has
    // height 1). Useful for tests to confirm splits actually happened.
    size_t height() const;

    // --- Helpers (already implemented for you) ---

    // Inserts (key, location) into a LEAF node's keys/values at the
    // correct sorted position. Does NOT check MAX_KEYS - that's your
    // job to check before/after calling this.
    static void insertIntoLeafSorted(BTreeNode* leaf, int32_t key, RowLocation location);

    // Inserts a (key, child) pair into an INTERNAL node at the correct
    // sorted position - used when propagating a split upward. `child`
    // is inserted at the position immediately after `key`'s slot.
    static void insertIntoInternalSorted(BTreeNode* node, int32_t key,
                                          std::unique_ptr<BTreeNode> child);

    // Returns the index of the first key in `keys` that is > `key`
    // (i.e. std::upper_bound). Used to decide which child to descend
    // into, or where a new key belongs in a sorted vector.
    static size_t upperBoundIndex(const std::vector<int32_t>& keys, int32_t key);

private:
    struct InsertSplitResult {
        int32_t splitKey;
        std::unique_ptr<BTreeNode> right;
    };

    std::optional<InsertSplitResult> insertRecursive(BTreeNode* node, int32_t key,
                                                      RowLocation location);

    std::unique_ptr<BTreeNode> root_;
};

}  // namespace minisql
