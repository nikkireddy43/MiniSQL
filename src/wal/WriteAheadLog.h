#pragma once

#include <cstdint>
#include <fstream>
#include <string>

#include "storage/DiskManager.h"
#include "storage/Page.h"

namespace minisql {

// An append-only log of page writes, used to guarantee durability
// without needing to flush every dirty page to disk immediately.
//
// The idea: instead of writing a full Page to the DATA file the moment
// it changes (expensive - could be many pages), append a small record
// to the LOG file describing that write. The log append is cheap and
// sequential. If the process crashes before the real page write ever
// happens, the log still has a record of it - replaying the log on
// restart (recover()) brings the data file back up to date with
// everything that was ever logged, regardless of what actually made it
// to the data file before the crash.
//
// Record format (fixed size, back-to-back in the log file):
//   [int32_t pageId][PAGE_SIZE bytes of page content]
//
// KNOWN LIMITATION (deliberate scope cut, consistent with earlier
// phases): this logs committed page WRITES for redo purposes only. It
// does not implement undo logging for in-flight transactions - that's
// handled separately by this project's existing snapshot-based
// transaction rollback. A production WAL (e.g. following the ARIES
// design) unifies both concerns; keeping them separate here is simpler
// and still correctly demonstrates the core redo/recovery guarantee.
class WriteAheadLog {
public:
    explicit WriteAheadLog(const std::string& logFilePath);

    // THE CORE METHOD - see the guidance below.
    //
    // Replays every record in the log file at `logFilePath`, in the
    // order they were written, applying each one to `diskManager` via
    // writePage(). Call this ONCE, at startup, BEFORE the data file is
    // used for anything else - it's how a crashed-and-recovered process
    // catches the data file up to everything that was ever logged.
    //
    // Suggested approach: open the log file for binary reading. If it
    // doesn't open (e.g. first-ever run, no log exists yet), that's not
    // an error - there's simply nothing to replay, just return. Otherwise
    // loop:
    //   1. Read an int32_t pageId. If the read fails (EOF), you're done.
    //   2. Read PAGE_SIZE bytes into a Page's buffer (Page::data() gives
    //      you a writable uint8_t* to read directly into).
    //   3. diskManager.writePage(pageId, page).
    // Later records for the same pageId will simply overwrite earlier
    // ones as you go - that's correct, the final state should reflect
    // whatever was logged last for that page.
    static void recover(const std::string& logFilePath, DiskManager& diskManager);

    // --- Helpers (already implemented for you) ---

    // Appends a record logging that `page`'s current contents are now
    // associated with `pageId`. Call this BEFORE relying on the page
    // being durable - the log entry, not the eventual real page write,
    // is what survives a crash.
    void logPageWrite(int32_t pageId, const Page& page);

    // Erases the log's contents (e.g. after a checkpoint confirms every
    // logged write has been safely applied to the data file for real -
    // the log entries are no longer needed for recovery at that point).
    void clear();

private:
    std::string logFilePath_;
    std::ofstream logFile_;  // opened in append + binary mode
};

}  // namespace minisql
