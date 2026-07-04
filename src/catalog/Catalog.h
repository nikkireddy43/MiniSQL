#pragma once

#include <memory>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

#include "parser/AST.h"       // reuses ColumnDefinition, ColumnType from the Parser
#include "storage/DiskManager.h"
#include "storage/Value.h"

namespace minisql {

// A table's metadata: its name and column definitions. This is the
// in-memory representation the Execution Engine (next phase) will query
// to make sense of a table before reading/writing its rows.
struct TableSchema {
    std::string tableName;
    std::vector<ColumnDefinition> columns;
};

class CatalogError : public std::runtime_error {
public:
    explicit CatalogError(const std::string& message) : std::runtime_error(message) {}
};

// Tracks every table's schema and persists that metadata to disk, reusing
// the same Page/DiskManager machinery built for table row data. This is
// exactly what real databases do - table/column metadata lives in
// "system tables" stored the same way as user data.
//
// KNOWN LIMITATION (deliberate v1 scope cut, consistent with Page's
// append-only design): all schemas are kept in a single page. Fine for
// a handful of tables; a real system would let the catalog span multiple
// pages. Worth being able to explain this tradeoff if asked.
class CatalogManager {
public:
    explicit CatalogManager(const std::string& catalogFilePath);

    // Registers a new table and immediately persists the updated catalog
    // to disk. Throws CatalogError if a table with this name already exists.
    void createTable(const std::string& tableName, const std::vector<ColumnDefinition>& columns);

    // Removes a table's schema and persists the change. Throws CatalogError
    // if no such table exists.
    void dropTable(const std::string& tableName);

    bool tableExists(const std::string& tableName) const;

    // Throws CatalogError if not found.
    const TableSchema& getTable(const std::string& tableName) const;

    std::vector<std::string> listTableNames() const;

    // --- THE CORE METHODS - see the detailed guidance above each one ---

    // Encodes a TableSchema into a single flat Record, using the layout:
    //   [0] TEXT  tableName
    //   [1] INT   columnCount (n)
    //   [2..]     for each of the n columns, TWO consecutive Values:
    //               TEXT columnName
    //               INT  columnType   (cast the ColumnType enum to int32_t)
    //
    // This reuses Value::makeText/makeInt - no new byte-packing, you're
    // just choosing how to represent a TableSchema as a list of Values.
    static Record encodeSchema(const TableSchema& schema);

    // The exact inverse of encodeSchema - reads a Record built in that
    // layout and reconstructs the TableSchema. Remember record[1].intVal
    // tells you how many columns to expect, and each column occupies
    // TWO consecutive slots (name, then type) in the record.
    static TableSchema decodeSchema(const Record& record);

    // Reads every record from page 0 (if the catalog file has any pages
    // yet), decodes each with decodeSchema, and populates `tables_`.
    // Called once, from the constructor. If the file has zero pages,
    // there's nothing to load - just return.
    void loadFromDisk();

    // Rewrites the ENTIRE catalog page from the current contents of
    // `tables_`: build a fresh Page, encodeSchema + appendRecord for
    // every table, then write it to page 0 (allocating page 0 first via
    // diskManager_->allocatePage() if the file currently has zero pages).
    // Called after every createTable/dropTable to keep disk in sync.
    //
    // This "rewrite everything" approach mirrors the same tradeoff you
    // already made for UPDATE/DELETE in the Storage Engine: simple and
    // correct, at the cost of being O(all tables) per change. Fine for
    // catalog-sized data.
    void persistAll();

private:
    std::unique_ptr<DiskManager> diskManager_;
    std::unordered_map<std::string, TableSchema> tables_;
};

}  // namespace minisql
