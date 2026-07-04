#pragma once

#include <fstream>
#include <string>

#include "storage/Page.h"

namespace minisql {

// Reads and writes fixed-size Pages to a .db file by page number.
// This class is fully implemented for you - it's mechanical file I/O,
// not database theory. The interesting design decisions live in Page's
// record serialization, which you're building.
class DiskManager {
public:
    explicit DiskManager(const std::string& dbFilePath);
    ~DiskManager();

    // Reads the page at `pageId` into `page`. pageId 0 is the first
    // PAGE_SIZE bytes of the file, pageId 1 the next PAGE_SIZE, etc.
    void readPage(int pageId, Page& page);

    // Writes `page`'s contents to the file at `pageId`'s position.
    void writePage(int pageId, const Page& page);

    // Extends the file by one page (zero-filled) and returns its page
    // number. Call this before writePage() when you need a brand new page.
    int allocatePage();

    // Total number of pages currently in the file.
    int numPages() const;

private:
    std::string filePath_;
    mutable std::fstream file_;
};

}  // namespace minisql
