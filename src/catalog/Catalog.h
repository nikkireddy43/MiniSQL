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
    // Which pages (in the shared data file) hold this table's rows.
    // Populated by the Execution Engine as it allocates pages for INSERTs.
    std::vector<int32_t> pageIds;
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

    // Registers a newly-allocated data page as belonging to `tableName`,
    // and persists the change. Called by the Execution Engine whenever
    // it needs to allocate a new page for a table's rows (at CREATE TABLE
    // time for the first page, or later if a page fills up).
    void addPageToTable(const std::string& tableName, int32_t pageId);

    // --- THE CORE METHODS - layout now includes pageIds (see below) ---

    // Encodes a TableSchema into a single flat Record, using the layout:
    //   [0] TEXT  tableName
    //   [1] INT   columnCount (n)
    //   [2..]     n columns, each as TWO consecutive Values:
    //               TEXT columnName, INT columnType
    //   [2+2n]    INT   pageCount (p)
    //   [2+2n+1..] p page IDs, each as one INT Value
    //
    // This is an ADDITIVE extension to the layout you already built -
    // everything up through the columns is unchanged, we're just
    // appending the page list at the end.
    static Record encodeSchema(const TableSchema& schema);

    // The inverse of encodeSchema, now also reading the trailing page list.
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
