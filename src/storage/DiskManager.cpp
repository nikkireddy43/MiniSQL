#include "storage/DiskManager.h"

#include <stdexcept>

namespace minisql {

DiskManager::DiskManager(const std::string& dbFilePath) : filePath_(dbFilePath) {
    // Open for read+write, in binary mode. If the file doesn't exist yet,
    // create it first (std::fstream with 'in | out' won't create a
    // missing file on its own).
    file_.open(filePath_, std::ios::in | std::ios::out | std::ios::binary);
    if (!file_.is_open()) {
        // File likely doesn't exist - create it, then reopen for read+write.
        std::ofstream createFile(filePath_, std::ios::binary);
        createFile.close();
        file_.open(filePath_, std::ios::in | std::ios::out | std::ios::binary);
    }
    if (!file_.is_open()) {
        throw std::runtime_error("DiskManager: could not open or create file: " + filePath_);
    }
}

DiskManager::~DiskManager() {
    if (file_.is_open()) {
        file_.close();
    }
}

void DiskManager::readPage(int pageId, Page& page) {
    file_.seekg(static_cast<std::streamoff>(pageId) * PAGE_SIZE);
    file_.read(reinterpret_cast<char*>(page.data()), PAGE_SIZE);
    if (file_.fail() && !file_.eof()) {
        throw std::runtime_error("DiskManager: failed to read page " + std::to_string(pageId));
    }
    file_.clear();  // reset any eof/fail flags for subsequent operations
}

void DiskManager::writePage(int pageId, const Page& page) {
    file_.seekp(static_cast<std::streamoff>(pageId) * PAGE_SIZE);
    file_.write(reinterpret_cast<const char*>(page.data()), PAGE_SIZE);
    if (file_.fail()) {
        throw std::runtime_error("DiskManager: failed to write page " + std::to_string(pageId));
    }
    file_.flush();
}

int DiskManager::allocatePage() {
    int newPageId = numPages();
    Page blankPage;  // constructor zero-fills and sets up an empty header
    writePage(newPageId, blankPage);
    return newPageId;
}

int DiskManager::numPages() const {
    file_.seekg(0, std::ios::end);
    std::streampos size = file_.tellg();
    if (size < 0) return 0;
    return static_cast<int>(static_cast<size_t>(size) / PAGE_SIZE);
}

}  // namespace minisql
