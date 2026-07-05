#include "wal/WriteAheadLog.h"

#include <stdexcept>

namespace minisql {

WriteAheadLog::WriteAheadLog(const std::string& logFilePath) : logFilePath_(logFilePath) {
    // Open in append mode - every write adds to the end, never overwrites
    // earlier records. Create the file if it doesn't exist yet.
    logFile_.open(logFilePath_, std::ios::app | std::ios::binary);
    if (!logFile_.is_open()) {
        throw std::runtime_error("WriteAheadLog: could not open log file: " + logFilePath_);
    }
}

void WriteAheadLog::logPageWrite(int32_t pageId, const Page& page) {
    logFile_.write(reinterpret_cast<const char*>(&pageId), sizeof(pageId));
    logFile_.write(reinterpret_cast<const char*>(page.data()), PAGE_SIZE);
    logFile_.flush();  // push out of the C++ stream buffer - see note below
    // NOTE: flush() here empties the ofstream's internal buffer into the
    // OS's file buffer, but doesn't force the OS to write it to physical
    // disk (that needs an explicit fsync()/fdatasync() syscall, which
    // C++ streams don't expose directly). A production WAL implementation
    // calls fsync() after every log append - otherwise a crash could
    // still lose recently-logged records sitting in OS-level buffers.
    // Documented limitation: flush() is the right idea and correct within
    // this project's testing model, just not a full real-world durability
    // guarantee without that extra syscall.
}

void WriteAheadLog::clear() {
    logFile_.close();
    logFile_.open(logFilePath_, std::ios::out | std::ios::binary | std::ios::trunc);
    logFile_.close();
    logFile_.open(logFilePath_, std::ios::app | std::ios::binary);
}

// ---------- Core logic (see WriteAheadLog.h for detailed guidance) ----------

void WriteAheadLog::recover(const std::string& logFilePath, DiskManager& diskManager) {
    std::ifstream logFile(logFilePath, std::ios::binary);
    if (!logFile.is_open()) {
        return;
    }

    while (true) {
        int32_t pageId;
        logFile.read(reinterpret_cast<char*>(&pageId), sizeof(pageId));
        if (!logFile) {
            break;
        }

        Page page;
        logFile.read(reinterpret_cast<char*>(page.data()), PAGE_SIZE);
        if (!logFile) {
            break;
        }

        diskManager.writePage(pageId, page);
    }
}

}  // namespace minisql
