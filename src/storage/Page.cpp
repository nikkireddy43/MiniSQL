#include "storage/Page.h"

#include <stdexcept>

namespace minisql {

Page::Page() {
    std::memset(buffer_, 0, PAGE_SIZE);
    setNumRecords(0);
    setFreeSpaceOffset(HEADER_SIZE);
}

// ---------- Byte-level helpers (already implemented for you) ----------

void Page::writeUInt16(size_t offset, uint16_t value) {
    std::memcpy(buffer_ + offset, &value, sizeof(value));
}

uint16_t Page::readUInt16(size_t offset) const {
    uint16_t value;
    std::memcpy(&value, buffer_ + offset, sizeof(value));
    return value;
}

void Page::writeUInt32(size_t offset, uint32_t value) {
    std::memcpy(buffer_ + offset, &value, sizeof(value));
}

uint32_t Page::readUInt32(size_t offset) const {
    uint32_t value;
    std::memcpy(&value, buffer_ + offset, sizeof(value));
    return value;
}

void Page::writeInt32(size_t offset, int32_t value) {
    std::memcpy(buffer_ + offset, &value, sizeof(value));
}

int32_t Page::readInt32(size_t offset) const {
    int32_t value;
    std::memcpy(&value, buffer_ + offset, sizeof(value));
    return value;
}

void Page::writeDouble(size_t offset, double value) {
    std::memcpy(buffer_ + offset, &value, sizeof(value));
}

double Page::readDouble(size_t offset) const {
    double value;
    std::memcpy(&value, buffer_ + offset, sizeof(value));
    return value;
}

void Page::writeBytes(size_t offset, const uint8_t* src, size_t len) {
    std::memcpy(buffer_ + offset, src, len);
}

void Page::readBytes(size_t offset, uint8_t* dst, size_t len) const {
    std::memcpy(dst, buffer_ + offset, len);
}

// ---------- Core logic (YOUR TURN - see Page.h for detailed guidance) ----------
//
// Suggested build order:
//   1. serializeRecord  - pure byte-packing, no page concepts, easiest
//      to reason about in isolation. Write a small throwaway test in
//      your head: for a record {INT(1), TEXT("Nikki"), FLOAT(8.7)},
//      what exact byte sequence should come out?
//   2. deserializeRecord - the exact inverse. If you serialize then
//      immediately deserialize, you should get the original record back.
//   3. appendRecord - uses serializeRecord + the header fields.
//   4. getAllRecords - uses deserializeRecord + the header fields.

void Page::serializeRecord(const Record& record, std::vector<uint8_t>& out) {
    uint32_t fieldCount = static_cast<uint32_t>(record.size());
    const uint8_t* countBytes = reinterpret_cast<const uint8_t*>(&fieldCount);
    out.insert(out.end(), countBytes, countBytes + sizeof(fieldCount));

    for (const Value& val : record) {
        out.push_back(static_cast<uint8_t>(val.type));

        switch (val.type) {
            case ValueType::INT: {
                const uint8_t* bytes = reinterpret_cast<const uint8_t*>(&val.intVal);
                out.insert(out.end(), bytes, bytes + sizeof(val.intVal));
                break;
            }
            case ValueType::FLOAT: {
                const uint8_t* bytes = reinterpret_cast<const uint8_t*>(&val.floatVal);
                out.insert(out.end(), bytes, bytes + sizeof(val.floatVal));
                break;
            }
            case ValueType::TEXT: {
                uint32_t len = static_cast<uint32_t>(val.textVal.size());
                const uint8_t* lenBytes = reinterpret_cast<const uint8_t*>(&len);
                out.insert(out.end(), lenBytes, lenBytes + sizeof(len));
                out.insert(out.end(), val.textVal.begin(), val.textVal.end());
                break;
            }
        }
    }
}

size_t Page::deserializeRecord(const uint8_t* data, Record& outRecord) {
    size_t offset = 0;

    uint32_t fieldCount;
    std::memcpy(&fieldCount, data + offset, sizeof(fieldCount));
    offset += sizeof(fieldCount);

    outRecord.clear();
    outRecord.reserve(fieldCount);

    for (uint32_t i = 0; i < fieldCount; ++i) {
        ValueType type = static_cast<ValueType>(data[offset]);
        offset += 1;

        Value val;
        val.type = type;

        switch (type) {
            case ValueType::INT: {
                int32_t intVal;
                std::memcpy(&intVal, data + offset, sizeof(intVal));
                offset += sizeof(intVal);
                val.intVal = intVal;
                break;
            }
            case ValueType::FLOAT: {
                double floatVal;
                std::memcpy(&floatVal, data + offset, sizeof(floatVal));
                offset += sizeof(floatVal);
                val.floatVal = floatVal;
                break;
            }
            case ValueType::TEXT: {
                uint32_t len;
                std::memcpy(&len, data + offset, sizeof(len));
                offset += sizeof(len);
                val.textVal.assign(reinterpret_cast<const char*>(data + offset), len);
                offset += len;
                break;
            }
            default:
                throw std::logic_error("Page::deserializeRecord() unknown ValueType");
        }

        outRecord.push_back(std::move(val));
    }

    return offset;
}

bool Page::appendRecord(const Record& record) {
    std::vector<uint8_t> serialized;
    serializeRecord(record, serialized);

    const size_t start = freeSpaceOffset();
    const size_t needed = 4 + serialized.size();
    if (start + needed > PAGE_SIZE) {
        return false;
    }

    writeUInt32(start, static_cast<uint32_t>(serialized.size()));
    writeBytes(start + 4, serialized.data(), serialized.size());

    writeUInt16(0, numRecords() + 1);
    writeUInt16(2, static_cast<uint16_t>(start + needed));

    return true;
}

std::vector<Record> Page::getAllRecords() const {
    std::vector<Record> records;
    records.reserve(numRecords());

    size_t offset = HEADER_SIZE;
    for (uint16_t i = 0; i < numRecords(); ++i) {
        const uint32_t length = readUInt32(offset);
        Record record;
        deserializeRecord(data() + offset + 4, record);
        records.push_back(std::move(record));
        offset += 4 + length;
    }

    return records;
}

}  // namespace minisql
