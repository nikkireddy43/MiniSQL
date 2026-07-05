#include <gtest/gtest.h>
#include <algorithm>
#include <random>
#include "index/BTree.h"

using namespace minisql;

TEST(BTreeTest, SearchOnEmptyTreeReturnsNullopt) {
    BPlusTree tree;
    EXPECT_FALSE(tree.search(42).has_value());
}

TEST(BTreeTest, HeightOfEmptyTreeIsOne) {
    BPlusTree tree;
    EXPECT_EQ(tree.height(), 1);
}

TEST(BTreeTest, InsertFewKeysNoSplitStillFindable) {
    BPlusTree tree;
    tree.insert(10, {0, 0});
    tree.insert(20, {0, 1});
    tree.insert(5, {0, 2});

    EXPECT_EQ(tree.height(), 1);  // fewer than MAX_KEYS, no split needed

    auto r1 = tree.search(10);
    ASSERT_TRUE(r1.has_value());
    EXPECT_EQ(r1->recordIndex, 0u);

    auto r2 = tree.search(5);
    ASSERT_TRUE(r2.has_value());
    EXPECT_EQ(r2->recordIndex, 2u);

    EXPECT_FALSE(tree.search(999).has_value());
}

TEST(BTreeTest, InsertExactlyMaxKeysStillNoSplit) {
    BPlusTree tree;
    for (int32_t i = 1; i <= static_cast<int32_t>(BPlusTree::MAX_KEYS); i++) {
        tree.insert(i, {0, static_cast<size_t>(i)});
    }
    EXPECT_EQ(tree.height(), 1);
    for (int32_t i = 1; i <= static_cast<int32_t>(BPlusTree::MAX_KEYS); i++) {
        EXPECT_TRUE(tree.search(i).has_value()) << "missing key " << i;
    }
}

TEST(BTreeTest, InsertingOneMoreThanMaxKeysCausesSplit) {
    BPlusTree tree;
    for (int32_t i = 1; i <= static_cast<int32_t>(BPlusTree::MAX_KEYS) + 1; i++) {
        tree.insert(i, {0, static_cast<size_t>(i)});
    }
    EXPECT_EQ(tree.height(), 2) << "Expected the tree to grow a level after exceeding MAX_KEYS";

    // Every key inserted must still be findable after the split.
    for (int32_t i = 1; i <= static_cast<int32_t>(BPlusTree::MAX_KEYS) + 1; i++) {
        auto result = tree.search(i);
        ASSERT_TRUE(result.has_value()) << "missing key " << i << " after split";
        EXPECT_EQ(result->recordIndex, static_cast<size_t>(i));
    }
}

TEST(BTreeTest, InsertManyKeysSequentialAllFindable) {
    BPlusTree tree;
    const int32_t n = 50;
    for (int32_t i = 0; i < n; i++) {
        tree.insert(i, {i / 10, static_cast<size_t>(i % 10)});
    }

    EXPECT_GT(tree.height(), 1) << "Expected multiple splits with 50 keys and MAX_KEYS="
                                 << BPlusTree::MAX_KEYS;

    for (int32_t i = 0; i < n; i++) {
        auto result = tree.search(i);
        ASSERT_TRUE(result.has_value()) << "missing key " << i;
        EXPECT_EQ(result->pageId, i / 10);
        EXPECT_EQ(result->recordIndex, static_cast<size_t>(i % 10));
    }
    EXPECT_FALSE(tree.search(-1).has_value());
    EXPECT_FALSE(tree.search(n).has_value());
}

TEST(BTreeTest, InsertManyKeysRandomOrderAllFindable) {
    std::vector<int32_t> keys;
    for (int32_t i = 0; i < 40; i++) keys.push_back(i);
    std::mt19937 rng(42);  // fixed seed - deterministic test
    std::shuffle(keys.begin(), keys.end(), rng);

    BPlusTree tree;
    for (int32_t k : keys) {
        tree.insert(k, {0, static_cast<size_t>(k)});
    }

    for (int32_t i = 0; i < 40; i++) {
        auto result = tree.search(i);
        ASSERT_TRUE(result.has_value()) << "missing key " << i;
        EXPECT_EQ(result->recordIndex, static_cast<size_t>(i));
    }
}

TEST(BTreeTest, RemoveKeyMakesItUnsearchable) {
    BPlusTree tree;
    tree.insert(1, {0, 1});
    tree.insert(2, {0, 2});
    tree.insert(3, {0, 3});

    tree.remove(2);

    EXPECT_FALSE(tree.search(2).has_value());
    EXPECT_TRUE(tree.search(1).has_value());
    EXPECT_TRUE(tree.search(3).has_value());
}

TEST(BTreeTest, RemoveNonexistentKeyDoesNotCrash) {
    BPlusTree tree;
    tree.insert(1, {0, 1});
    EXPECT_NO_THROW(tree.remove(999));
    EXPECT_TRUE(tree.search(1).has_value());
}

TEST(BTreeTest, RemoveAfterSplitStillFindsRemainingKeys) {
    BPlusTree tree;
    for (int32_t i = 1; i <= 10; i++) {
        tree.insert(i, {0, static_cast<size_t>(i)});
    }
    tree.remove(5);
    EXPECT_FALSE(tree.search(5).has_value());
    for (int32_t i = 1; i <= 10; i++) {
        if (i == 5) continue;
        EXPECT_TRUE(tree.search(i).has_value()) << "missing key " << i << " after removing 5";
    }
}
