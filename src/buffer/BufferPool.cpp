#include "buffer/BufferPool.h"

namespace minisql {

BufferPool::BufferPool(DiskManager& diskManager, size_t poolSize)
    : diskManager_(diskManager), poolSize_(poolSize) {
    if (poolSize_ == 0) {
        throw BufferPoolError("BufferPool poolSize must be at least 1");
    }
}

// ---------- LRU list bookkeeping (already implemented for you) ----------

void BufferPool::removeFromLru(size_t frameIndex) {
    auto it = lruIterators_.find(frameIndex);
    if (it != lruIterators_.end()) {
        lruList_.erase(it->second);
        lruIterators_.erase(it);
    }
}

void BufferPool::addToLru(size_t frameIndex) {
    lruList_.push_back(frameIndex);
    lruIterators_[frameIndex] = std::prev(lruList_.end());
}

// ---------- Simple helpers (already implemented for you) ----------

void BufferPool::flushPage(int32_t pageId) {
    auto it = pageTable_.find(pageId);
    if (it == pageTable_.end()) return;  // not cached, nothing to flush

    Frame& frame = frames_[it->second];
    if (frame.dirty) {
        diskManager_.writePage(pageId, frame.page);
        frame.dirty = false;
    }
}

void BufferPool::flushAllPages() {
    for (const auto& [pageId, frameIndex] : pageTable_) {
        (void)frameIndex;
        flushPage(pageId);
    }
}

bool BufferPool::isPageCached(int32_t pageId) const {
    return pageTable_.find(pageId) != pageTable_.end();
}

size_t BufferPool::numFramesInUse() const {
    return frames_.size();
}

// ---------- Core logic (YOUR TURN - see BufferPool.h for detailed guidance) ----------
//
// Suggested build order: fetchPage -> unpinPage -> newPage (newPage is
// short once fetchPage works, since it just allocates then delegates).

Page* BufferPool::fetchPage(int32_t pageId) {
    auto it = pageTable_.find(pageId);
    if (it != pageTable_.end()) {
        size_t frameIndex = it->second;
        Frame& frame = frames_[frameIndex];
        if (frame.pinCount == 0) {
            removeFromLru(frameIndex);
        }
        frame.pinCount++;
        return &frame.page;
    }

    size_t frameIndex;
    if (frames_.size() < poolSize_) {
        frameIndex = frames_.size();
        frames_.push_back(Frame{});
    } else {
        if (lruList_.empty()) {
            throw BufferPoolError("buffer pool full, no unpinned frames");
        }

        frameIndex = lruList_.front();
        removeFromLru(frameIndex);

        Frame& victim = frames_[frameIndex];
        if (victim.dirty) {
            diskManager_.writePage(victim.pageId, victim.page);
            victim.dirty = false;
        }
        pageTable_.erase(victim.pageId);
    }

    Frame& frame = frames_[frameIndex];
    diskManager_.readPage(pageId, frame.page);
    frame.pageId = pageId;
    frame.pinCount = 1;
    frame.dirty = false;
    pageTable_[pageId] = frameIndex;
    return &frame.page;
}

void BufferPool::unpinPage(int32_t pageId, bool isDirty) {
    auto it = pageTable_.find(pageId);
    if (it == pageTable_.end()) {
        throw BufferPoolError("Cannot unpin a page that isn't cached: " + std::to_string(pageId));
    }

    Frame& frame = frames_[it->second];
    if (frame.pinCount == 0) {
        throw BufferPoolError("Cannot unpin page " + std::to_string(pageId) + " - pin count is already 0");
    }

    if (isDirty) {
        frame.dirty = true;
    }

    frame.pinCount--;
    if (frame.pinCount == 0) {
        addToLru(it->second);
    }
}

Page* BufferPool::newPage(int32_t& outPageId) {
    outPageId = diskManager_.allocatePage();
    return fetchPage(outPageId);
}

}  // namespace minisql
