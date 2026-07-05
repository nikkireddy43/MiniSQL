#include "index/BTree.h"

#include <algorithm>
#include <stdexcept>

namespace minisql {

BPlusTree::BPlusTree() {
    // An empty tree is a single empty leaf - the simplest possible valid state.
    root_ = std::make_unique<BTreeNode>(true);
}

// ---------- Helpers    ----------

size_t BPlusTree::upperBoundIndex(const std::vector<int32_t>& keys, int32_t key) {
    auto it = std::upper_bound(keys.begin(), keys.end(), key);
    return static_cast<size_t>(it - keys.begin());
}

void BPlusTree::insertIntoLeafSorted(BTreeNode* leaf, int32_t key, RowLocation location) {
    size_t pos = upperBoundIndex(leaf->keys, key);
    leaf->keys.insert(leaf->keys.begin() + pos, key);
    leaf->values.insert(leaf->values.begin() + pos, location);
}

void BPlusTree::insertIntoInternalSorted(BTreeNode* node, int32_t key,
                                         std::unique_ptr<BTreeNode> child) {
    size_t pos = upperBoundIndex(node->keys, key);
    node->keys.insert(node->keys.begin() + pos, key);
    // The new child goes immediately to the right of the new key.
    node->children.insert(node->children.begin() + pos + 1, std::move(child));
}

size_t BPlusTree::height() const {
    size_t levels = 1;
    const BTreeNode* current = root_.get();
    while (!current->isLeaf) {
        current = current->children[0].get();
        levels++;
    }
    return levels;
}

// ---------- Core logic (YOUR TURN - see BTree.h for detailed guidance) ----------
//
// Suggested build order:
//   1. search      - pure tree descent, no mutation, good warm-up.
//   2. insert       - START by getting insertion into a non-splitting
//      leaf correct and tested. THEN tackle the split case. Consider
//      writing a private recursive helper for this (declare it in
//      BTree.h under `private:`).
//   3. remove       - simple leaf removal, no rebalancing.

std::optional<RowLocation> BPlusTree::search(int32_t key) const {
    const BTreeNode* current = root_.get();

    while (!current->isLeaf) {
        size_t childIndex = upperBoundIndex(current->keys, key);
        current = current->children[childIndex].get();
    }

    for (size_t i = 0; i < current->keys.size(); i++) {
        if (current->keys[i] == key) {
            return current->values[i];
        }
    }
    return std::nullopt;
}

std::optional<BPlusTree::InsertSplitResult> BPlusTree::insertRecursive(BTreeNode* node,
                                                                        int32_t key,
                                                                        RowLocation location) {
    if (node->isLeaf) {
        insertIntoLeafSorted(node, key, location);
        if (node->keys.size() <= MAX_KEYS) {
            return std::nullopt;
        }

        const size_t splitIndex = node->keys.size() / 2;
        auto right = std::make_unique<BTreeNode>(true);
        right->keys.assign(node->keys.begin() + splitIndex, node->keys.end());
        right->values.assign(node->values.begin() + splitIndex, node->values.end());

        const int32_t promoteKey = node->keys[splitIndex];
        node->keys.resize(splitIndex);
        node->values.resize(splitIndex);
        return InsertSplitResult{promoteKey, std::move(right)};
    }

    const size_t childIndex = upperBoundIndex(node->keys, key);
    auto split = insertRecursive(node->children[childIndex].get(), key, location);
    if (!split.has_value()) {
        return std::nullopt;
    }

    insertIntoInternalSorted(node, split->splitKey, std::move(split->right));
    if (node->keys.size() <= MAX_KEYS) {
        return std::nullopt;
    }

    const size_t splitIndex = node->keys.size() / 2;
    const int32_t promoteKey = node->keys[splitIndex];
    auto right = std::make_unique<BTreeNode>(false);
    right->keys.assign(node->keys.begin() + splitIndex + 1, node->keys.end());
    node->keys.resize(splitIndex);

    right->children.insert(
        right->children.end(),
        std::make_move_iterator(node->children.begin() + splitIndex + 1),
        std::make_move_iterator(node->children.end()));
    node->children.resize(splitIndex + 1);

    return InsertSplitResult{promoteKey, std::move(right)};
}

void BPlusTree::insert(int32_t key, RowLocation location) {
    auto split = insertRecursive(root_.get(), key, location);
    if (split.has_value()) {
        auto newRoot = std::make_unique<BTreeNode>(false);
        newRoot->keys.push_back(split->splitKey);
        newRoot->children.push_back(std::move(root_));
        newRoot->children.push_back(std::move(split->right));
        root_ = std::move(newRoot);
    }
}

void BPlusTree::remove(int32_t key) {
    BTreeNode* current = root_.get();
    while (!current->isLeaf) {
        size_t childIndex = upperBoundIndex(current->keys, key);
        current = current->children[childIndex].get();
    }

    for (size_t i = 0; i < current->keys.size(); ++i) {
        if (current->keys[i] == key) {
            current->keys.erase(current->keys.begin() + static_cast<std::ptrdiff_t>(i));
            current->values.erase(current->values.begin() + static_cast<std::ptrdiff_t>(i));
            return;
        }
    }
}

}  // namespace minisql
