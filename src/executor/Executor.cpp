#include "executor/Executor.h"

#include <filesystem>

#include "storage/Page.h"

namespace minisql {

Executor::Executor(CatalogManager& catalog, BufferPool& bufferPool, WriteAheadLog& wal,
                    std::string dataFilePath, std::string catalogFilePath)
    : catalog_(catalog), bufferPool_(bufferPool), wal_(wal),
      dataFilePath_(std::move(dataFilePath)), catalogFilePath_(std::move(catalogFilePath)) {}

ExecutionResult Executor::execute(Statement* stmt) {
    switch (stmt->type) {
        case StatementType::CREATE_TABLE:
            return executeCreateTable(static_cast<CreateTableStatement*>(stmt));
        case StatementType::CREATE_INDEX:
            return executeCreateIndex(static_cast<CreateIndexStatement*>(stmt));
        case StatementType::INSERT:
            return executeInsert(static_cast<InsertStatement*>(stmt));
        case StatementType::SELECT:
            return executeSelect(static_cast<SelectStatement*>(stmt));
        case StatementType::UPDATE:
            return executeUpdate(static_cast<UpdateStatement*>(stmt));
        case StatementType::DELETE:
            return executeDelete(static_cast<DeleteStatement*>(stmt));
        case StatementType::DROP_TABLE:
            return executeDropTable(static_cast<DropTableStatement*>(stmt));
        case StatementType::BEGIN_TXN:
            return executeBegin(static_cast<BeginStatement*>(stmt));
        case StatementType::COMMIT_TXN:
            return executeCommit(static_cast<CommitStatement*>(stmt));
        case StatementType::ROLLBACK_TXN:
            return executeRollback(static_cast<RollbackStatement*>(stmt));
    }
    throw ExecutionError("Unknown statement type");
}

ExecutionResult Executor::executeCreateTable(CreateTableStatement* stmt) {
    catalog_.createTable(stmt->tableName, stmt->columns);

    int32_t pageId;
    bufferPool_.newPage(pageId);
    bufferPool_.unpinPage(pageId, false);  // freshly allocated page already matches disk
    catalog_.addPageToTable(stmt->tableName, pageId);

    ExecutionResult result;
    result.message = "Table '" + stmt->tableName + "' created.";
    return result;
}

ExecutionResult Executor::executeDropTable(DropTableStatement* stmt) {
    catalog_.dropTable(stmt->tableName);
    ExecutionResult result;
    result.message = "Table '" + stmt->tableName + "' dropped.";
    return result;
}

// ---------- Transactions ----------

ExecutionResult Executor::executeBegin(BeginStatement*) {
    if (inTransaction_) {
        throw ExecutionError("A transaction is already in progress");
    }

    // Make sure everything currently in memory is actually on disk before
    // we snapshot the files - otherwise the snapshot could miss recent
    // dirty pages still sitting in the buffer pool.
    bufferPool_.flushAllPages();

    namespace fs = std::filesystem;
    fs::copy_file(dataFilePath_, dataFilePath_ + ".snapshot",
                   fs::copy_options::overwrite_existing);
    fs::copy_file(catalogFilePath_, catalogFilePath_ + ".snapshot",
                   fs::copy_options::overwrite_existing);

    inTransaction_ = true;

    ExecutionResult result;
    result.message = "Transaction started.";
    return result;
}

ExecutionResult Executor::executeCommit(CommitStatement*) {
    if (!inTransaction_) {
        throw ExecutionError("No transaction is in progress");
    }

    // Nothing to persist here - every statement already flushed eagerly
    // as it ran. Committing just means "stop tracking the undo snapshot."
    namespace fs = std::filesystem;
    fs::remove(dataFilePath_ + ".snapshot");
    fs::remove(catalogFilePath_ + ".snapshot");

    inTransaction_ = false;

    ExecutionResult result;
    result.message = "Transaction committed.";
    return result;
}

// THE EXERCISE. Undo everything since BEGIN by restoring the snapshot
// files taken there, then making sure every in-memory structure that
// might now be stale gets reset to match.
//
// Suggested approach, in this exact order (order matters here):
//   1. If !inTransaction_, throw ExecutionError("No transaction is in
//      progress") - same guard as executeCommit.
//   2. Copy the snapshot files back OVER the live files:
//        namespace fs = std::filesystem;
//        fs::copy_file(dataFilePath_ + ".snapshot", dataFilePath_,
//                       fs::copy_options::overwrite_existing);
//        fs::copy_file(catalogFilePath_ + ".snapshot", catalogFilePath_,
//                       fs::copy_options::overwrite_existing);
//   3. Now the files on disk are back to how they were at BEGIN, but
//      several things IN MEMORY still reflect the (now-undone) changes
//      and would give wrong answers if left alone:
//        - bufferPool_ may have cached pages that no longer match what's
//          on disk -> call bufferPool_.resetCache().
//        - catalog_'s in-memory table metadata still reflects the undone
//          changes -> call catalog_.reloadFromDisk().
//        - indexes_ and indexedColumnsByTable_ were built against data
//          that may no longer exist in the same locations (or may not
//          exist at all if a CREATE TABLE got rolled back) -> the
//          simplest correct move is indexes_.clear() and
//          indexedColumnsByTable_.clear(). (Documented limitation: any
//          indexes created during the rolled-back transaction must be
//          recreated manually afterward with CREATE INDEX - rebuilding
//          them automatically is a reasonable improvement, not required
//          to prove rollback works correctly.)
//   4. Delete the now-consumed snapshot files (fs::remove, same paths as
//      executeCommit) and set inTransaction_ = false.
//   5. Return an ExecutionResult with a message like "Transaction rolled
//      back."
ExecutionResult Executor::executeRollback(RollbackStatement*) {
    if (!inTransaction_) {
        throw ExecutionError("No transaction is in progress");
    }

    namespace fs = std::filesystem;
    fs::copy_file(dataFilePath_ + ".snapshot", dataFilePath_,
                   fs::copy_options::overwrite_existing);
    fs::copy_file(catalogFilePath_ + ".snapshot", catalogFilePath_,
                   fs::copy_options::overwrite_existing);

    bufferPool_.resetCache();
    catalog_.reloadFromDisk();
    indexes_.clear();
    indexedColumnsByTable_.clear();

    fs::remove(dataFilePath_ + ".snapshot");
    fs::remove(catalogFilePath_ + ".snapshot");

    inTransaction_ = false;

    ExecutionResult result;
    result.message = "Transaction rolled back.";
    return result;
}

void Executor::checkpoint() {
    bufferPool_.flushAllPages();
    wal_.clear();
}

std::vector<Record> Executor::gatherAllRows(BufferPool& bufferPool, const TableSchema& schema) {
    std::vector<Record> allRows;
    for (int32_t pageId : schema.pageIds) {
        Page* page = bufferPool.fetchPage(pageId);
        auto pageRecords = page->getAllRecords();
        bufferPool.unpinPage(pageId, false);  // read-only, not modified
        allRows.insert(allRows.end(), pageRecords.begin(), pageRecords.end());
    }
    return allRows;
}

void Executor::rewriteTableRows(CatalogManager& catalog, BufferPool& bufferPool, WriteAheadLog& wal,
                                 const std::string& tableName, const std::vector<Record>& rows) {
    std::vector<int32_t> pageIds = catalog.getTable(tableName).pageIds;
    size_t pageIdx = 0;
    Page currentPage;

    // Writes `currentPage` into either an existing page slot (reusing the
    // table's current pageIds first) or a newly allocated one, then resets
    // currentPage for the next batch of rows.
    auto flushCurrentPage = [&]() {
        bool isNewPage = (pageIdx >= pageIds.size());
        int32_t targetPageId;
        Page* cached;

        if (!isNewPage) {
            targetPageId = pageIds[pageIdx];
            cached = bufferPool.fetchPage(targetPageId);
        } else {
            cached = bufferPool.newPage(targetPageId);
            pageIds.push_back(targetPageId);
        }

        *cached = currentPage;
        // Log the write BEFORE unpinning, while `cached` is still valid -
        // this is what makes the write durable now, instead of an eager
        // full-pool flush after every statement.
        wal.logPageWrite(targetPageId, *cached);
        bufferPool.unpinPage(targetPageId, true);

        if (isNewPage) {
            catalog.addPageToTable(tableName, targetPageId);
        }
        pageIdx++;
        currentPage = Page();
    };

    for (const Record& row : rows) {
        if (!currentPage.appendRecord(row)) {
            flushCurrentPage();
            if (!currentPage.appendRecord(row)) {
                throw ExecutionError("Row too large to fit in a single page");
            }
        }
    }
    flushCurrentPage();  // flush the final (possibly partial, possibly empty) page

    // Clear any pages that existed before but aren't needed anymore -
    // otherwise their old contents would resurface on a future read.
    for (; pageIdx < pageIds.size(); pageIdx++) {
        Page* cached = bufferPool.fetchPage(pageIds[pageIdx]);
        *cached = Page();
        wal.logPageWrite(pageIds[pageIdx], *cached);
        bufferPool.unpinPage(pageIds[pageIdx], true);
    }

    // No eager flushAllPages() here anymore - the WAL entries above are
    // what guarantee durability now. The buffer pool is free to keep
    // these pages cached and defer the real disk write to whenever a
    // frame naturally gets evicted or checkpoint() is called.
}

// ---------- Index support ----------

std::string Executor::indexKey(const std::string& tableName, const std::string& columnName) {
    return tableName + "." + columnName;
}

bool Executor::hasIndex(const std::string& tableName, const std::string& columnName) const {
    return indexes_.find(indexKey(tableName, columnName)) != indexes_.end();
}

BPlusTree Executor::buildIndexFromScan(const std::string& tableName, const std::string& columnName) {
    const TableSchema& schema = catalog_.getTable(tableName);

    size_t colIndex = schema.columns.size();
    for (size_t i = 0; i < schema.columns.size(); i++) {
        if (schema.columns[i].name == columnName) {
            colIndex = i;
            break;
        }
    }
    if (colIndex == schema.columns.size()) {
        throw ExecutionError("Unknown column for index: " + columnName);
    }
    if (schema.columns[colIndex].type != ColumnType::INT) {
        throw ExecutionError("Indexes are only supported on INT columns (column '" +
                              columnName + "' is not INT)");
    }

    BPlusTree tree;
    for (int32_t pageId : schema.pageIds) {
        Page* page = bufferPool_.fetchPage(pageId);
        auto records = page->getAllRecords();
        bufferPool_.unpinPage(pageId, false);
        for (size_t recordIndex = 0; recordIndex < records.size(); recordIndex++) {
            int32_t key = records[recordIndex][colIndex].intVal;
            tree.insert(key, RowLocation{pageId, recordIndex});
        }
    }
    return tree;
}

void Executor::rebuildIndexesForTable(const std::string& tableName) {
    auto it = indexedColumnsByTable_.find(tableName);
    if (it == indexedColumnsByTable_.end()) return;

    for (const std::string& columnName : it->second) {
        indexes_[indexKey(tableName, columnName)] = buildIndexFromScan(tableName, columnName);
    }
}

ExecutionResult Executor::executeCreateIndex(CreateIndexStatement* stmt) {
    BPlusTree tree = buildIndexFromScan(stmt->tableName, stmt->columnName);
    indexes_[indexKey(stmt->tableName, stmt->columnName)] = std::move(tree);
    indexedColumnsByTable_[stmt->tableName].push_back(stmt->columnName);

    ExecutionResult result;
    result.message = "Index '" + stmt->indexName + "' created on " +
                      stmt->tableName + "(" + stmt->columnName + ").";
    return result;
}

// ---------- Core logic ----------

ExecutionResult Executor::executeInsert(InsertStatement* stmt) {
    const TableSchema& schema = catalog_.getTable(stmt->tableName);

    Record record;
    record.reserve(schema.columns.size());
    for (size_t i = 0; i < schema.columns.size(); ++i) {
        record.push_back(literalToValue(stmt->values[i], schema.columns[i].type));
    }

    int32_t targetPageId = schema.pageIds.back();
    Page* page = bufferPool_.fetchPage(targetPageId);
    size_t newRecordIndex = page->numRecords();  // capture BEFORE appending

    if (page->appendRecord(record)) {
        wal_.logPageWrite(targetPageId, *page);  // durable via the log now, not an eager flush
        bufferPool_.unpinPage(targetPageId, true);
    } else {
        bufferPool_.unpinPage(targetPageId, false);  // unmodified, roll back the pin cleanly
        Page* newPagePtr = bufferPool_.newPage(targetPageId);
        newRecordIndex = 0;
        newPagePtr->appendRecord(record);
        wal_.logPageWrite(targetPageId, *newPagePtr);
        bufferPool_.unpinPage(targetPageId, true);
        catalog_.addPageToTable(stmt->tableName, targetPageId);
    }

    // Keep any existing indexes on this table in sync incrementally,
    // rather than rebuilding from scratch on every insert.
    auto it = indexedColumnsByTable_.find(stmt->tableName);
    if (it != indexedColumnsByTable_.end()) {
        for (const std::string& columnName : it->second) {
            size_t colIdx = 0;
            for (; colIdx < schema.columns.size(); colIdx++) {
                if (schema.columns[colIdx].name == columnName) break;
            }
            int32_t key = record[colIdx].intVal;
            indexes_[indexKey(stmt->tableName, columnName)].insert(
                key, RowLocation{targetPageId, newRecordIndex});
        }
    }

    ExecutionResult result;
    result.message = "1 row inserted.";
    return result;
}

ExecutionResult Executor::executeSelect(SelectStatement* stmt) {
    const TableSchema& schema = catalog_.getTable(stmt->tableName);

    std::vector<Record> matched;

    if (stmt->whereClause.has_value()
        && stmt->whereClause->op == TokenType::EQUAL
        && hasIndex(stmt->tableName, stmt->whereClause->column)) {
        int32_t key = std::stoi(stmt->whereClause->value.text);
        auto location = indexes_[indexKey(stmt->tableName, stmt->whereClause->column)].search(key);
        if (location.has_value()) {
            Page* page = bufferPool_.fetchPage(location->pageId);
            auto records = page->getAllRecords();
            bufferPool_.unpinPage(location->pageId, false);
            matched.push_back(records[location->recordIndex]);
        }
    } else {
        std::vector<Record> allRows = gatherAllRows(bufferPool_, schema);
        for (const Record& row : allRows) {
            if (!stmt->whereClause.has_value()
                || evaluateCondition(row, schema, stmt->whereClause.value())) {
                matched.push_back(row);
            }
        }
    }

    ExecutionResult result;
    if (stmt->selectAll) {
        result.columnNames.reserve(schema.columns.size());
        for (const ColumnDefinition& col : schema.columns) {
            result.columnNames.push_back(col.name);
        }
        result.rows = std::move(matched);
    } else {
        result.columnNames = stmt->columns;
        result.rows.reserve(matched.size());
        for (const Record& row : matched) {
            Record projected;
            projected.reserve(stmt->columns.size());
            for (const std::string& colName : stmt->columns) {
                size_t colIndex = 0;
                for (; colIndex < schema.columns.size(); ++colIndex) {
                    if (schema.columns[colIndex].name == colName) break;
                }
                if (colIndex == schema.columns.size()) {
                    throw ExecutionError("Unknown column in SELECT: " + colName);
                }
                projected.push_back(row[colIndex]);
            }
            result.rows.push_back(std::move(projected));
        }
    }

    result.message = std::to_string(result.rows.size()) + " row(s) returned.";
    return result;
}

ExecutionResult Executor::executeUpdate(UpdateStatement* stmt) {
    const TableSchema& schema = catalog_.getTable(stmt->tableName);
    std::vector<Record> allRows = gatherAllRows(bufferPool_, schema);

    size_t setColumnIndex = schema.columns.size();
    for (size_t i = 0; i < schema.columns.size(); ++i) {
        if (schema.columns[i].name == stmt->setColumn) {
            setColumnIndex = i;
            break;
        }
    }
    if (setColumnIndex == schema.columns.size()) {
        throw ExecutionError("Unknown column in SET clause: " + stmt->setColumn);
    }

    const ColumnType setColumnType = schema.columns[setColumnIndex].type;
    size_t updatedCount = 0;

    for (Record& row : allRows) {
        const bool matches = !stmt->whereClause.has_value()
            || evaluateCondition(row, schema, stmt->whereClause.value());
        if (matches) {
            row[setColumnIndex] = literalToValue(stmt->setValue, setColumnType);
            updatedCount++;
        }
    }

    rewriteTableRows(catalog_, bufferPool_, wal_, stmt->tableName, allRows);
    rebuildIndexesForTable(stmt->tableName);

    ExecutionResult result;
    result.message = std::to_string(updatedCount) + " row(s) updated.";
    return result;
}

ExecutionResult Executor::executeDelete(DeleteStatement* stmt) {
    const TableSchema& schema = catalog_.getTable(stmt->tableName);
    std::vector<Record> allRows = gatherAllRows(bufferPool_, schema);

    std::vector<Record> toKeep;
    for (const Record& row : allRows) {
        bool matches = stmt->whereClause.has_value()
            && evaluateCondition(row, schema, stmt->whereClause.value());
        if (!stmt->whereClause.has_value()) matches = true;
        if (!matches) toKeep.push_back(row);
    }

    rewriteTableRows(catalog_, bufferPool_, wal_, stmt->tableName, toKeep);
    rebuildIndexesForTable(stmt->tableName);

    ExecutionResult result;
    result.message = std::to_string(allRows.size() - toKeep.size()) + " row(s) deleted.";
    return result;
}

Value Executor::literalToValue(const Literal& literal, ColumnType expectedType) {
    switch (expectedType) {
        case ColumnType::INT: return Value::makeInt(std::stoi(literal.text));
        case ColumnType::FLOAT: return Value::makeFloat(std::stod(literal.text));
        case ColumnType::TEXT: return Value::makeText(literal.text);
    }
    throw ExecutionError("Unknown column type in literalToValue()");
}

bool Executor::evaluateCondition(const Record& record, const TableSchema& schema,
                                  const Condition& condition) {
    int columnIndex = -1;
    for (size_t i = 0; i < schema.columns.size(); i++) {
        if (schema.columns[i].name == condition.column) {
            columnIndex = static_cast<int>(i);
            break;
        }
    }
    if (columnIndex == -1) {
        throw ExecutionError("Unknown column in WHERE clause: " + condition.column);
    }

    const Value& fieldValue = record[static_cast<size_t>(columnIndex)];
    Value compareValue = literalToValue(condition.value, schema.columns[columnIndex].type);

    int cmp = 0;
    switch (fieldValue.type) {
        case ValueType::INT:
            cmp = (fieldValue.intVal > compareValue.intVal) - (fieldValue.intVal < compareValue.intVal);
            break;
        case ValueType::FLOAT:
            cmp = (fieldValue.floatVal > compareValue.floatVal) - (fieldValue.floatVal < compareValue.floatVal);
            break;
        case ValueType::TEXT:
            cmp = fieldValue.textVal.compare(compareValue.textVal);
            break;
    }

    switch (condition.op) {
        case TokenType::EQUAL: return cmp == 0;
        case TokenType::NOT_EQUAL: return cmp != 0;
        case TokenType::LESS: return cmp < 0;
        case TokenType::LESS_EQUAL: return cmp <= 0;
        case TokenType::GREATER: return cmp > 0;
        case TokenType::GREATER_EQUAL: return cmp >= 0;
        default: throw ExecutionError("Unsupported operator in WHERE clause");
    }
}

}  // namespace minisql
