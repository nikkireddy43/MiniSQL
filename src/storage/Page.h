#pragma once

#include <cstdint>
#include <cstring>
#include <vector>

#include "storage/Value.h"

namespace minisql {

// A fixed-size block of bytes, the unit of I/O between memory and disk.
// 4096 matches common OS page/disk-block size - not a magic number, a
// real systems convention worth knowing if asked about it.
constexpr size_t PAGE_SIZE = 4096;

// Page layout:
//   [0..1]   uint16_t numRecords       - how many records are stored here
//   [2..3]   uint16_t freeSpaceOffset  - byte offset where the next
//                                        record's data should be written
//   [4..]    record data, back-to-back, each as:
//              [uint32_t recordLength][serialized record bytes...]
//
// KNOWN LIMITATION (deliberate v1 scope cut, not an oversight): records
// are appended sequentially and never deleted in place. UPDATE/DELETE
// are handled at the Execution Engine level by rewriting the table's
// data rather than editing pages in place. This keeps v1 focused on
// getting persistence working correctly before optimizing it.
class Page {
public:
    static constexpr size_t HEADER_SIZE = 4;

    Page();

    // THE CORE METHODS - see the detailed guidance above each declaration
    // below. These are what you'll implement.

    // Attempts to append `record` to this page. Returns false (and
    // leaves the page unchanged) if there isn't enough remaining space -
    // the caller (DiskManager-using code, later) is responsible for then
    // trying the next page or allocating a new one.
    //
    // Suggested approach:
    //   1. Serialize `record` into a temporary byte buffer using
    //      writeInt32/writeDouble/writeString-style logic (see
    //      serializeRecord below - you'll build that first and call it
    //      here).
    //   2. Check whether 4 (for the record's length prefix) + the
    //      serialized size fits before PAGE_SIZE, starting from
    //      freeSpaceOffset().
    //   3. If it fits: write the uint32_t length prefix at
    //      freeSpaceOffset(), then the serialized bytes right after,
    //      then update numRecords and freeSpaceOffset in the header.
    //   4. Return true. If it doesn't fit, return false without
    //      modifying anything.
    bool appendRecord(const Record& record);

    // Reads and deserializes every record currently stored in this page,
    // in the order they were appended.
    //
    // Suggested approach: read numRecords() from the header. Starting at
    // offset HEADER_SIZE, loop that many times: read a uint32_t length
    // prefix, then deserialize that many bytes into a Record (see
    // deserializeRecord below), advancing your read position by
    // 4 + length each time.
    std::vector<Record> getAllRecords() const;

    // Converts a single Record into its flat byte representation.
    // Format:
    //   [uint32_t fieldCount]
    //   then per Value, back-to-back:
    //   [uint8_t typeTag]
    //   then, depending on typeTag:
    //     INT:   [int32_t value]                       (4 bytes)
    //     FLOAT: [double value]                         (8 bytes)
    //     TEXT:  [uint32_t length][length bytes of text] (4 + length bytes)
    //
    // Append bytes to `out` (don't clear it first - callers may reuse
    // a buffer). This is pure byte-packing, no page/disk concepts here.
    static void serializeRecord(const Record& record, std::vector<uint8_t>& out);

    // The inverse of serializeRecord: reads a Record's worth of Values
    // starting at `data`, matching the exact format serializeRecord
    // produces. Returns the number of bytes consumed, so the caller
    // (getAllRecords) knows where the next record starts.
    static size_t deserializeRecord(const uint8_t* data, Record& outRecord);

    // --- Byte-level helpers (already implemented for you) ---
    // Small, mechanical read/write-primitive-at-offset operations.
    // serializeRecord/deserializeRecord will be built out of these.

    void writeUInt16(size_t offset, uint16_t value);
    uint16_t readUInt16(size_t offset) const;
    void writeUInt32(size_t offset, uint32_t value);
    uint32_t readUInt32(size_t offset) const;
    void writeInt32(size_t offset, int32_t value);
    int32_t readInt32(size_t offset) const;
    void writeDouble(size_t offset, double value);
    double readDouble(size_t offset) const;
    void writeBytes(size_t offset, const uint8_t* src, size_t len);
    void readBytes(size_t offset, uint8_t* dst, size_t len) const;

    uint16_t numRecords() const { return readUInt16(0); }
    uint16_t freeSpaceOffset() const { return readUInt16(2); }

    const uint8_t* data() const { return buffer_; }
    uint8_t* data() { return buffer_; }

private:
    uint8_t buffer_[PAGE_SIZE];

    void setNumRecords(uint16_t n) { writeUInt16(0, n); }
    void setFreeSpaceOffset(uint16_t offset) { writeUInt16(2, offset); }
};

}  // namespace minisql
