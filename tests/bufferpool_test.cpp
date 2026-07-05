#include <gtest/gtest.h>
#include <cstdio>
#include "buffer/BufferPool.h"

using namespace minisql;

class BufferPoolTest : public ::testing::Test {
protected:
    std::string testFile = "test_minisql_bufferpool.db";
    std::unique_ptr<DiskManager> disk;

    void SetUp() override {
        disk = std::make_unique<DiskManager>(testFile);
    }
    void TearDown() override {
        disk.reset();
        std::remove(testFile.c_str());
    }
};

TEST_F(BufferPoolTest, FetchNewlyAllocatedPageReturnsEmptyPage) {
    int32_t pageId = disk->allocatePage();
    BufferPool pool(*disk, 10);

    Page* page = pool.fetchPage(pageId);
    ASSERT_NE(page, nullptr);
    EXPECT_EQ(page->numRecords(), 0);
    pool.unpinPage(pageId, false);
}

TEST_F(BufferPoolTest, FetchingSamePageTwiceReturnsSamePointer) {
    int32_t pageId = disk->allocatePage();
    BufferPool pool(*disk, 10);

    Page* first = pool.fetchPage(pageId);
    Page* second = pool.fetchPage(pageId);
    EXPECT_EQ(first, second) << "Expected a cache hit to return the SAME frame, not a fresh copy";
    pool.unpinPage(pageId, false);
    pool.unpinPage(pageId, false);
}

TEST_F(BufferPoolTest, WritingThroughFetchedPageThenUnpinDirtyPersists) {
    int32_t pageId = disk->allocatePage();
    {
        BufferPool pool(*disk, 10);
        Page* page = pool.fetchPage(pageId);
        page->appendRecord({Value::makeInt(42)});
        pool.unpinPage(pageId, /*isDirty=*/true);
        pool.flushAllPages();
    }
    // Fresh read directly from disk (bypassing any pool) should see the write.
    Page rawPage;
    disk->readPage(pageId, rawPage);
    auto records = rawPage.getAllRecords();
    ASSERT_EQ(records.size(), 1);
    EXPECT_EQ(records[0][0].intVal, 42);
}

TEST_F(BufferPoolTest, EvictsLeastRecentlyUsedUnpinnedFrame) {
    int32_t a = disk->allocatePage();
    int32_t b = disk->allocatePage();
    int32_t c = disk->allocatePage();
    BufferPool pool(*disk, 2);  // capacity for only 2 pages

    pool.fetchPage(a);
    pool.unpinPage(a, false);
    pool.fetchPage(b);
    pool.unpinPage(b, false);
    // Pool is full (A, B cached). Fetching C should evict A (LRU).
    pool.fetchPage(c);
    pool.unpinPage(c, false);

    EXPECT_FALSE(pool.isPageCached(a)) << "Expected A (least recently used) to be evicted";
    EXPECT_TRUE(pool.isPageCached(c));
}

TEST_F(BufferPoolTest, PinnedPagesAreNeverEvicted) {
    int32_t a = disk->allocatePage();
    int32_t b = disk->allocatePage();
    int32_t c = disk->allocatePage();
    BufferPool pool(*disk, 2);

    pool.fetchPage(a);              // A stays PINNED - never unpinned
    pool.fetchPage(b);
    pool.unpinPage(b, false);       // B is unpinned, eligible for eviction
    pool.fetchPage(c);              // pool full, must evict - only B is eligible
    pool.unpinPage(c, false);

    EXPECT_TRUE(pool.isPageCached(a)) << "A is pinned and must not be evicted";
    EXPECT_FALSE(pool.isPageCached(b)) << "B was the only unpinned frame, should be evicted";
    pool.unpinPage(a, false);
}

TEST_F(BufferPoolTest, DirtyPageIsWrittenBackWhenEvicted) {
    int32_t a = disk->allocatePage();
    int32_t b = disk->allocatePage();
    int32_t c = disk->allocatePage();
    BufferPool pool(*disk, 2);

    Page* pageA = pool.fetchPage(a);
    pageA->appendRecord({Value::makeInt(7)});
    pool.unpinPage(a, /*isDirty=*/true);  // modified but not yet flushed

    pool.fetchPage(b);
    pool.unpinPage(b, false);

    pool.fetchPage(c);  // forces eviction of A (LRU) - must flush A's write first
    pool.unpinPage(c, false);

    // Read A directly from disk - the eviction should have written it back.
    Page rawA;
    disk->readPage(a, rawA);
    auto records = rawA.getAllRecords();
    ASSERT_EQ(records.size(), 1);
    EXPECT_EQ(records[0][0].intVal, 7);
}

TEST_F(BufferPoolTest, UnpinningUncachedPageThrows) {
    BufferPool pool(*disk, 10);
    EXPECT_THROW(pool.unpinPage(999, false), BufferPoolError);
}

TEST_F(BufferPoolTest, UnpinningAlreadyUnpinnedPageThrows) {
    int32_t a = disk->allocatePage();
    BufferPool pool(*disk, 10);
    pool.fetchPage(a);
    pool.unpinPage(a, false);
    EXPECT_THROW(pool.unpinPage(a, false), BufferPoolError)
        << "Pin count was already 0 - unpinning again is a caller bug";
}

TEST_F(BufferPoolTest, NewPageAllocatesAndPinsIt) {
    BufferPool pool(*disk, 10);
    int32_t newId = -1;
    Page* page = pool.newPage(newId);
    ASSERT_NE(page, nullptr);
    EXPECT_GE(newId, 0);
    EXPECT_TRUE(pool.isPageCached(newId));
    pool.unpinPage(newId, false);
}

TEST_F(BufferPoolTest, PoolFullOfPinnedPagesThrowsOnFetch) {
    int32_t a = disk->allocatePage();
    int32_t b = disk->allocatePage();
    int32_t c = disk->allocatePage();
    BufferPool pool(*disk, 2);

    pool.fetchPage(a);  // pinned, never unpinned
    pool.fetchPage(b);  // pinned, never unpinned
    // Pool is full (capacity 2) and BOTH frames are pinned - nothing to evict.
    EXPECT_THROW(pool.fetchPage(c), BufferPoolError);
}
