#pragma once

#include <stdexcept>
#include <string>
#include <vector>

#include "catalog/Catalog.h"
#include "parser/AST.h"
#include "storage/DiskManager.h"
#include "storage/Value.h"

namespace minisql {

class ExecutionError : public std::runtime_error {
public:
    explicit ExecutionError(const std::string& message) : std::runtime_error(message) {}
};

// What running a statement produces: a human-readable summary message,
// and (for SELECT only) the resulting column names + rows to display.
struct ExecutionResult {
    std::string message;
    std::vector<std::string> columnNames;  // empty for non-SELECT statements
    std::vector<Record> rows;              // empty for non-SELECT statements
};

// Ties everything together: given a parsed Statement, uses the Catalog
// to know what tables/columns exist, and Page/DiskManager to actually
// read and write row data.
//
// KNOWN LIMITATION (consistent with earlier phases' scope cuts): UPDATE
// and DELETE work by reading every row, keeping/modifying the ones that
// should remain, and rewriting the table's data pages from scratch - not
// modifying pages in place. Simple and correct; not how a real system
// would do it at scale, but a legitimate, explainable v1 tradeoff.
class Executor {
public:
    Executor(CatalogManager& catalog, DiskManager& dataDisk);

    // Dispatches based on stmt->type to the matching executeX() method.
    ExecutionResult execute(Statement* stmt);

    // --- THE CORE METHODS - see the guidance above each one ---

    // Suggested approach:
    //   1. Look up the table's schema via catalog_.getTable(tableName).
    //   2. Convert each Literal in stmt->values to a typed Value using
    //      literalToValue() below, matching each column's declared type
    //      (schema.columns[i].type) in order.
    //   3. Read the table's LAST page (schema.pageIds.back()) via
    //      dataDisk_.readPage().
    //   4. Try page.appendRecord(record). If it fits, write the page
    //      back with dataDisk_.writePage() and you're done.
    //   5. If it doesn't fit: allocate a new page
    //      (dataDisk_.allocatePage()), append the record to THAT page,
    //      write it, and register it with catalog_.addPageToTable().
    ExecutionResult executeInsert(InsertStatement* stmt);

    // Suggested approach:
    //   1. Look up the schema. Read every page in schema.pageIds and
    //      collect all records via page.getAllRecords().
    //   2. If stmt->whereClause has a value, keep only records where
    //      evaluateCondition() (below) returns true.
    //   3. Build columnNames for the result: if stmt->selectAll, use
    //      every column name from the schema in order; otherwise use
    //      stmt->columns as given.
    //   4. If not selecting all columns, project each kept record down
    //      to just the requested columns (look up each requested
    //      column's index in the schema, pull that Value out).
    //   5. Put everything into an ExecutionResult and return it.
    ExecutionResult executeSelect(SelectStatement* stmt);

    // Suggested approach (the "rewrite everything" pattern):
    //   1. Read every record across all of schema.pageIds (same as SELECT).
    //   2. For each record, if evaluateCondition() matches the WHERE
    //      clause (or there's no WHERE clause, meaning ALL rows match),
    //      overwrite the field matching stmt->setColumn with a Value
    //      built via literalToValue(stmt->setValue, ...).
    //   3. Rewrite: build fresh Page(s), appendRecord every row (updated
    //      or not) back in, write them to schema.pageIds in order
    //      (allocating additional pages via catalog_.addPageToTable()
    //      only if you run out of existing pages).
    ExecutionResult executeUpdate(UpdateStatement* stmt);

    // Same "rewrite everything" pattern as UPDATE, but instead of
    // modifying matching rows, you simply DON'T write back the ones
    // that match the WHERE clause (or, with no WHERE clause, write
    // nothing back at all - every row is deleted).
    ExecutionResult executeDelete(DeleteStatement* stmt);

    // --- Helpers (already implemented for you) ---

    // Converts a parsed Literal (raw text, e.g. "8.7") into a typed Value,
    // according to what type the destination column expects.
    static Value literalToValue(const Literal& literal, ColumnType expectedType);

    // Evaluates a single WHERE condition against one record, using the
    // schema to find which column index `condition.column` refers to.
    static bool evaluateCondition(const Record& record, const TableSchema& schema,
                                   const Condition& condition);

private:
    CatalogManager& catalog_;
    DiskManager& dataDisk_;

    // --- Already implemented for you (see Executor.cpp) ---
    ExecutionResult executeCreateTable(CreateTableStatement* stmt);
    ExecutionResult executeDropTable(DropTableStatement* stmt);

    // Reads every record across all of a table's data pages.
    static std::vector<Record> gatherAllRows(DiskManager& disk, const TableSchema& schema);

    // Rewrites a table's ENTIRE data page set from `rows`: repacks them
    // into pages (reusing the table's existing pageIds first, allocating
    // new ones via catalog.addPageToTable only if needed), and - this
    // part matters - explicitly overwrites any leftover old pages with
    // an empty Page if fewer pages are needed now than before. Without
    // that last step, stale records from before the rewrite would still
    // be sitting in those pages and would resurface on a later read.
    static void rewriteTableRows(CatalogManager& catalog, DiskManager& disk,
                                  const std::string& tableName, const std::vector<Record>& rows);
};

}  // namespace minisql
