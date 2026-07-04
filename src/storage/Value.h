#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace minisql {

enum class ValueType : uint8_t { INT = 0, FLOAT = 1, TEXT = 2 };

// A single typed field value, e.g. one cell in a row.
// Unlike the Parser's Literal (which stores raw text like "8.7"), Value
// holds the actually-converted typed data - a real int32_t/double/string
// you can compute with. Converting Literal -> Value happens in the
// Execution Engine (later phase), using the Catalog's column types to
// know which conversion to apply.
struct Value {
    ValueType type;
    int32_t intVal = 0;
    double floatVal = 0.0;
    std::string textVal;

    static Value makeInt(int32_t v) {
        Value val;
        val.type = ValueType::INT;
        val.intVal = v;
        return val;
    }

    static Value makeFloat(double v) {
        Value val;
        val.type = ValueType::FLOAT;
        val.floatVal = v;
        return val;
    }

    static Value makeText(std::string v) {
        Value val;
        val.type = ValueType::TEXT;
        val.textVal = std::move(v);
        return val;
    }

    bool operator==(const Value& other) const {
        if (type != other.type) return false;
        switch (type) {
            case ValueType::INT: return intVal == other.intVal;
            case ValueType::FLOAT: return floatVal == other.floatVal;
            case ValueType::TEXT: return textVal == other.textVal;
        }
        return false;
    }
};

// A row is just a sequence of typed field values, in column order.
using Record = std::vector<Value>;

}  // namespace minisql
