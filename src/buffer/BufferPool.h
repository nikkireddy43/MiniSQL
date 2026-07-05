#pragma once

#include <cstdint>
#include <list>
#include <stdexcept>
#include <unordered_map>
#include <vector>

#include "storage/DiskManager.h"
#include "storage/Page.h"

namespace minisql {

class BufferPoolError : public std::runtime_error {
public:
    explicit BufferPoolError(const std::string& message) : std::runtime_error(message) {}
};

// One in-memory slot holding a cached Page plus its bookkeeping.
struct Frame {
    Page page;
    int32_t pageId = -1;   // -1 means this frame is currently unused
    int pinCount = 0;      // > 0 means "in use, do not evict"
    bool dirty = false;    // true means "modified in memory, must write back before discarding"
};

// An in-memory cache of Pages sitting between callers and DiskManager.
// Fixed capacity; when full, evicts the Least Recently Used UNPINNED
// frame to make room for a new page.
class BufferPool {
public:
    BufferPool(DiskManager& diskManager, size_t poolSize);

    // THE CORE METHODS - see the guidance above each one.

    // Returns a pointer to the (possibly newly-loaded) Page for `pageId`,
    // and increments its pin count. The caller MUST call unpinPage() when
    // done with it - forgetting to unpin permanently "leaks" a frame,
    // since a pinned frame can never be evicted.
    //
    // Suggested approach:
    //   1. Cache hit: if pageId is already in pageTable_, that frame's
    //      pinCount++ (and if it was previously unpinned/in lruList_,
    //      remove it from there - see removeFromLru() below - since a
    //      pinned frame is not an eviction candidate). Return &frame.page.
    //   2. Cache miss: you need a frame to load into. If frames_.size()
    //      is still less than poolSize_, you can grow frames_ by one
    //      (push_back a default Frame) and use that new slot. Otherwise
    //      the pool is full - you must evict: take lruList_.front() (the
    //      least-recently-used unpinned frame), remove it from lruList_
    //      and lruIterators_, and if THAT frame is dirty, write it back
    //      to disk before reusing it (diskManager_.writePage(...)) - also
    //      erase its old pageId from pageTable_. If lruList_ is empty,
    //      every frame is pinned and there's nothing to evict - throw
    //      BufferPoolError("buffer pool full, no unpinned frames").
    //   3. Whichever frame index you end up with: diskManager_.readPage
    //      into it, set pageId/pinCount=1/dirty=false, register it in
    //      pageTable_, and return &frames_[idx].page.
    Page* fetchPage(int32_t pageId);

    // Decrements the pin count for `pageId`. If `isDirty` is true, marks
    // the frame dirty (once dirty, stays dirty until flushed - don't
    // un-mark it just because this particular caller's change was read-
    // only). Once pinCount reaches 0, the frame becomes an eviction
    // candidate again - add its frame index to the BACK of lruList_ (so
    // eviction from the FRONT correctly picks the least-recently-used).
    // Throw BufferPoolError if pageId isn't currently cached, or if its
    // pinCount is already 0 (unpinning something not pinned is a bug in
    // the caller, worth catching loudly rather than silently ignoring).
    void unpinPage(int32_t pageId, bool isDirty);

    // Allocates a brand new page via diskManager_, loads it into a frame
    // (following the same "find a frame, evict if needed" logic as
    // fetchPage), pins it, and returns both the new page's ID (via
    // `outPageId`) and a pointer to it. The caller must unpinPage() it
    // once done, same as fetchPage.
    //
    // The simplest correct approach: allocate the page on disk first via
    // diskManager_.allocatePage(), then just call fetchPage() on that
    // new page ID - it'll naturally load the (freshly zero-initialized)
    // page from disk into a frame using logic you already wrote.
    Page* newPage(int32_t& outPageId);

    // --- Helpers (already implemented for you) ---

    // Writes a specific cached page back to disk if it's dirty, then
    // clears its dirty flag. Does nothing if the page isn't cached.
    void flushPage(int32_t pageId);

    // Flushes every currently-cached dirty page.
    void flushAllPages();

    bool isPageCached(int32_t pageId) const;
    size_t numFramesInUse() const;

private:
    DiskManager& diskManager_;
    size_t poolSize_;

    std::vector<Frame> frames_;                    // grows up to poolSize_
    std::unordered_map<int32_t, size_t> pageTable_;  // pageId -> frame index

    // Frame indices of currently UNPINNED frames only, ordered least- to
    // most-recently-used (front = next eviction victim).
    std::list<size_t> lruList_;
    // frame index -> its position in lruList_, for O(1) removal when a
    // frame becomes pinned again.
    std::unordered_map<size_t, std::list<size_t>::iterator> lruIterators_;

    // Removes `frameIndex` from the LRU eviction-candidate list, if
    // present. Safe to call even if it isn't in the list.
    void removeFromLru(size_t frameIndex);

    // Marks `frameIndex` as an eviction candidate (call this when a
    // frame's pin count drops to 0).
    void addToLru(size_t frameIndex);
};

}  // namespace minisql
