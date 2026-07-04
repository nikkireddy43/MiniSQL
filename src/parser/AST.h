#pragma once

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "lexer/Token.h"

namespace minisql {

// ---------- Shared building blocks ----------

enum class ColumnType { INT, FLOAT, TEXT };

// A single "name TYPE" pair, e.g. "id INT" in a CREATE TABLE.
struct ColumnDefinition {
    std::string name;
    ColumnType type;
};

// A literal value appearing in INSERT VALUES or a WHERE comparison,
// e.g. 42, 8.7, 'Nikki'. Kept as raw text (like Token does) - actual
// type conversion happens later in the Storage/Execution layer.
struct Literal {
    enum class Kind { INT, FLOAT, STRING };
    Kind kind;
    std::string text;
};

// A single WHERE condition: "column OP literal", e.g. "id = 5".
// v1 supports exactly one condition (no AND/OR) - that's a deliberate
// scope cut, not an oversight. Multi-condition WHERE is a natural v3
// extension once you're comfortable with this shape.
struct Condition {
    std::string column;
    TokenType op;  // one of EQUAL, NOT_EQUAL, LESS, LESS_EQUAL, GREATER, GREATER_EQUAL
    Literal value;
};

// ---------- Statement base ----------

enum class StatementType {
    CREATE_TABLE,
    INSERT,
    SELECT,
    UPDATE,
    DELETE,
    DROP_TABLE,
};

// Base class for every parsed statement. `type` lets calling code safely
// downcast (via static_cast, after checking `type`) to the concrete
// statement struct without needing RTTI/dynamic_cast.
struct Statement {
    explicit Statement(StatementType type) : type(type) {}
    virtual ~Statement() = default;
    StatementType type;
};

// ---------- Concrete statements ----------

// CREATE TABLE student(id INT, name TEXT, cg FLOAT);
struct CreateTableStatement : Statement {
    CreateTableStatement() : Statement(StatementType::CREATE_TABLE) {}
    std::string tableName;
    std::vector<ColumnDefinition> columns;
};

// INSERT INTO student VALUES(1, 'Nikki', 8.7);
struct InsertStatement : Statement {
    InsertStatement() : Statement(StatementType::INSERT) {}
    std::string tableName;
    std::vector<Literal> values;
};

// SELECT * FROM student WHERE id = 5;
// SELECT id, name FROM student;
struct SelectStatement : Statement {
    SelectStatement() : Statement(StatementType::SELECT) {}
    std::string tableName;
    bool selectAll = false;             // true for SELECT *
    std::vector<std::string> columns;   // used when selectAll is false
    std::optional<Condition> whereClause;
};

// UPDATE student SET cg = 9.0 WHERE id = 5;
struct UpdateStatement : Statement {
    UpdateStatement() : Statement(StatementType::UPDATE) {}
    std::string tableName;
    std::string setColumn;
    Literal setValue;
    std::optional<Condition> whereClause;
};

// DELETE FROM student WHERE id = 5;
struct DeleteStatement : Statement {
    DeleteStatement() : Statement(StatementType::DELETE) {}
    std::string tableName;
    std::optional<Condition> whereClause;
};

// DROP TABLE student;
struct DropTableStatement : Statement {
    DropTableStatement() : Statement(StatementType::DROP_TABLE) {}
    std::string tableName;
};

}  // namespace minisql
