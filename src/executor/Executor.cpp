#include "executor/Executor.h"

#include <algorithm>
#include <filesystem>
#include <map>
#include <unordered_map>

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
    if (stmt->join.has_value() && stmt->hasAggregates) {
        throw ExecutionError("Aggregates combined with JOIN are not supported in this version");
    }
    if (stmt->join.has_value()) {
        return executeSelectWithJoin(stmt);
    }
    if (stmt->hasAggregates) {
        return executeSelectWithAggregates(stmt);
    }

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

    if (stmt->orderBy.has_value()) {
        sortResultRows(result, stmt->orderBy.value());
    }

    result.message = std::to_string(result.rows.size()) + " row(s) returned.";
    return result;
}

// ---------- v3: comparison, sorting, aggregates, joins ----------

int Executor::compareValues(const Value& a, const Value& b) {
    if (a.isNull || b.isNull) {
        if (a.isNull && b.isNull) return 0;
        return a.isNull ? -1 : 1;  // null sorts as "smallest"
    }
    switch (a.type) {
        case ValueType::INT:
            return (a.intVal > b.intVal) - (a.intVal < b.intVal);
        case ValueType::FLOAT:
            return (a.floatVal > b.floatVal) - (a.floatVal < b.floatVal);
        case ValueType::TEXT:
            return a.textVal.compare(b.textVal);
    }
    return 0;
}

void Executor::sortResultRows(ExecutionResult& result, const OrderByClause& orderBy) {
    size_t colIndex = result.columnNames.size();
    for (size_t i = 0; i < result.columnNames.size(); i++) {
        if (result.columnNames[i] == orderBy.column) {
            colIndex = i;
            break;
        }
    }
    if (colIndex == result.columnNames.size()) {
        // Fall back to matching just the part after the last '.' -
        // convenient when column headers are qualified (e.g. "student.id")
        // but ORDER BY was written unqualified.
        for (size_t i = 0; i < result.columnNames.size(); i++) {
            const std::string& header = result.columnNames[i];
            size_t dotPos = header.rfind('.');
            std::string unqualified = (dotPos == std::string::npos) ? header : header.substr(dotPos + 1);
            if (unqualified == orderBy.column) {
                colIndex = i;
                break;
            }
        }
    }
    if (colIndex == result.columnNames.size()) {
        throw ExecutionError("Unknown column in ORDER BY: " + orderBy.column);
    }

    std::sort(result.rows.begin(), result.rows.end(),
              [colIndex, &orderBy](const Record& a, const Record& b) {
                  int cmp = compareValues(a[colIndex], b[colIndex]);
                  return orderBy.descending ? cmp > 0 : cmp < 0;
              });
}

std::string Executor::aggregateLabel(const SelectItem& item) {
    std::string funcName;
    switch (item.aggFunc) {
        case AggregateFunc::COUNT: funcName = "COUNT"; break;
        case AggregateFunc::SUM: funcName = "SUM"; break;
        case AggregateFunc::AVG: funcName = "AVG"; break;
        case AggregateFunc::MIN: funcName = "MIN"; break;
        case AggregateFunc::MAX: funcName = "MAX"; break;
        case AggregateFunc::NONE: funcName = ""; break;
    }
    std::string arg = item.isCountStar ? "*" : item.columnName;
    return funcName + "(" + arg + ")";
}

Value Executor::computeAggregate(const std::vector<Record>& groupRows, const SelectItem& item,
                                  const TableSchema& schema) {
    if (item.isCountStar) {
        return Value::makeInt(static_cast<int32_t>(groupRows.size()));
    }

    size_t colIdx = schema.columns.size();
    for (size_t i = 0; i < schema.columns.size(); i++) {
        if (schema.columns[i].name == item.columnName) {
            colIdx = i;
            break;
        }
    }
    if (colIdx == schema.columns.size()) {
        throw ExecutionError("Unknown column in aggregate: " + item.columnName);
    }
    ColumnType colType = schema.columns[colIdx].type;

    if (item.aggFunc == AggregateFunc::COUNT) {
        return Value::makeInt(static_cast<int32_t>(groupRows.size()));
    }

    if (groupRows.empty()) {
        if (item.aggFunc == AggregateFunc::SUM) {
            return colType == ColumnType::FLOAT ? Value::makeFloat(0.0) : Value::makeInt(0);
        }
        if (item.aggFunc == AggregateFunc::AVG) {
            return Value::makeFloat(0.0);
        }
        return Value::makeNull();  // MIN/MAX of an empty group is undefined
    }

    if (item.aggFunc == AggregateFunc::SUM || item.aggFunc == AggregateFunc::AVG) {
        double total = 0.0;
        for (const Record& row : groupRows) {
            total += (colType == ColumnType::FLOAT) ? row[colIdx].floatVal
                                                      : static_cast<double>(row[colIdx].intVal);
        }
        if (item.aggFunc == AggregateFunc::AVG) {
            return Value::makeFloat(total / static_cast<double>(groupRows.size()));
        }
        return colType == ColumnType::FLOAT ? Value::makeFloat(total)
                                             : Value::makeInt(static_cast<int32_t>(total));
    }

    // MIN / MAX
    Value best = groupRows[0][colIdx];
    for (size_t i = 1; i < groupRows.size(); i++) {
        int cmp = compareValues(groupRows[i][colIdx], best);
        if ((item.aggFunc == AggregateFunc::MIN && cmp < 0)
            || (item.aggFunc == AggregateFunc::MAX && cmp > 0)) {
            best = groupRows[i][colIdx];
        }
    }
    return best;
}

ExecutionResult Executor::executeSelectWithAggregates(SelectStatement* stmt) {
    for (const SelectItem& item : stmt->selectItems) {
        if (item.aggFunc == AggregateFunc::NONE && !item.isCountStar) {
            throw ExecutionError("Mixing plain columns with aggregates in SELECT is not supported "
                                 "(the GROUP BY column is included automatically)");
        }
    }

    const TableSchema& schema = catalog_.getTable(stmt->tableName);
    std::vector<Record> allRows = gatherAllRows(bufferPool_, schema);

    std::vector<Record> filtered;
    for (const Record& row : allRows) {
        if (!stmt->whereClause.has_value() || evaluateCondition(row, schema, stmt->whereClause.value())) {
            filtered.push_back(row);
        }
    }

    ExecutionResult result;

    if (stmt->groupByColumn.has_value()) {
        size_t groupColIdx = schema.columns.size();
        for (size_t i = 0; i < schema.columns.size(); i++) {
            if (schema.columns[i].name == stmt->groupByColumn.value()) {
                groupColIdx = i;
                break;
            }
        }
        if (groupColIdx == schema.columns.size()) {
            throw ExecutionError("Unknown column in GROUP BY: " + stmt->groupByColumn.value());
        }

        // Group by a string key derived from the value - works uniformly
        // for INT/FLOAT/TEXT. std::map keeps groups in a deterministic,
        // sorted-by-key order.
        std::map<std::string, std::pair<Value, std::vector<Record>>> groups;
        for (const Record& row : filtered) {
            const Value& groupVal = row[groupColIdx];
            std::string key = (groupVal.type == ValueType::INT) ? std::to_string(groupVal.intVal)
                             : (groupVal.type == ValueType::FLOAT) ? std::to_string(groupVal.floatVal)
                                                                    : groupVal.textVal;
            auto it = groups.find(key);
            if (it == groups.end()) {
                groups[key] = {groupVal, {row}};
            } else {
                it->second.second.push_back(row);
            }
        }

        result.columnNames.push_back(stmt->groupByColumn.value());
        for (const SelectItem& item : stmt->selectItems) {
            result.columnNames.push_back(aggregateLabel(item));
        }

        for (const auto& [key, groupData] : groups) {
            (void)key;
            Record outRow;
            outRow.push_back(groupData.first);
            for (const SelectItem& item : stmt->selectItems) {
                outRow.push_back(computeAggregate(groupData.second, item, schema));
            }
            result.rows.push_back(std::move(outRow));
        }
    } else {
        for (const SelectItem& item : stmt->selectItems) {
            result.columnNames.push_back(aggregateLabel(item));
        }
        Record outRow;
        for (const SelectItem& item : stmt->selectItems) {
            outRow.push_back(computeAggregate(filtered, item, schema));
        }
        result.rows.push_back(std::move(outRow));
    }

    if (stmt->orderBy.has_value()) {
        sortResultRows(result, stmt->orderBy.value());
    }

    result.message = std::to_string(result.rows.size()) + " row(s) returned.";
    return result;
}

bool Executor::evaluateConditionOnJoinedRow(const Record& row, const JoinedSchema& joinedSchema,
                                             const Condition& condition) {
    std::string wantTable, wantColumn = condition.column;
    size_t dotPos = condition.column.find('.');
    if (dotPos != std::string::npos) {
        wantTable = condition.column.substr(0, dotPos);
        wantColumn = condition.column.substr(dotPos + 1);
    }

    int colIndex = -1;
    ColumnType colType = ColumnType::TEXT;
    for (size_t i = 0; i < joinedSchema.columns.size(); i++) {
        const auto& [tableName, colDef] = joinedSchema.columns[i];
        bool tableMatches = wantTable.empty() || tableName == wantTable;
        if (tableMatches && colDef.name == wantColumn) {
            colIndex = static_cast<int>(i);
            colType = colDef.type;
            break;
        }
    }
    if (colIndex == -1) {
        throw ExecutionError("Unknown column in WHERE clause: " + condition.column);
    }

    const Value& fieldValue = row[static_cast<size_t>(colIndex)];
    Value compareValue = Executor::literalToValue(condition.value, colType);
    int cmp = Executor::compareValues(fieldValue, compareValue);

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

ExecutionResult Executor::executeSelectWithJoin(SelectStatement* stmt) {
    const JoinClause& join = stmt->join.value();
    const TableSchema& leftSchema = catalog_.getTable(stmt->tableName);
    const TableSchema& rightSchema = catalog_.getTable(join.rightTable);

    size_t leftColIdx = leftSchema.columns.size();
    for (size_t i = 0; i < leftSchema.columns.size(); i++) {
        if (leftSchema.columns[i].name == join.leftColumn) { leftColIdx = i; break; }
    }
    size_t rightColIdx = rightSchema.columns.size();
    for (size_t i = 0; i < rightSchema.columns.size(); i++) {
        if (rightSchema.columns[i].name == join.rightColumn) { rightColIdx = i; break; }
    }
    if (leftColIdx == leftSchema.columns.size() || rightColIdx == rightSchema.columns.size()) {
        throw ExecutionError("Unknown column in JOIN ON clause");
    }

    std::vector<Record> leftRows = gatherAllRows(bufferPool_, leftSchema);
    std::vector<Record> rightRows = gatherAllRows(bufferPool_, rightSchema);

    JoinedSchema joinedSchema;
    for (const auto& col : leftSchema.columns) joinedSchema.columns.push_back({stmt->tableName, col});
    for (const auto& col : rightSchema.columns) joinedSchema.columns.push_back({join.rightTable, col});
    size_t rightWidth = rightSchema.columns.size();

    // Simple cost-based decision: hash join scales better than nested
    // loop once both sides are reasonably large, but LEFT JOIN keeps its
    // unmatched-row bookkeeping simpler with nested loop, so we restrict
    // hash join to INNER JOIN here.
    bool useHashJoin = !join.isLeftJoin && leftRows.size() > 50 && rightRows.size() > 50;

    std::vector<Record> joined;

    if (useHashJoin) {
        // Build the hash table on whichever side is smaller.
        bool buildOnLeft = leftRows.size() <= rightRows.size();
        const std::vector<Record>& buildRows = buildOnLeft ? leftRows : rightRows;
        size_t buildColIdx = buildOnLeft ? leftColIdx : rightColIdx;

        std::unordered_map<std::string, std::vector<size_t>> hashTable;
        for (size_t i = 0; i < buildRows.size(); i++) {
            const Value& v = buildRows[i][buildColIdx];
            std::string key = (v.type == ValueType::INT) ? std::to_string(v.intVal)
                             : (v.type == ValueType::FLOAT) ? std::to_string(v.floatVal) : v.textVal;
            hashTable[key].push_back(i);
        }

        const std::vector<Record>& probeRows = buildOnLeft ? rightRows : leftRows;
        size_t probeColIdx = buildOnLeft ? rightColIdx : leftColIdx;

        for (const Record& probeRow : probeRows) {
            const Value& v = probeRow[probeColIdx];
            std::string key = (v.type == ValueType::INT) ? std::to_string(v.intVal)
                             : (v.type == ValueType::FLOAT) ? std::to_string(v.floatVal) : v.textVal;
            auto it = hashTable.find(key);
            if (it == hashTable.end()) continue;
            for (size_t buildIdx : it->second) {
                Record combined;
                const Record& leftRow = buildOnLeft ? buildRows[buildIdx] : probeRow;
                const Record& rightRow = buildOnLeft ? probeRow : buildRows[buildIdx];
                combined.insert(combined.end(), leftRow.begin(), leftRow.end());
                combined.insert(combined.end(), rightRow.begin(), rightRow.end());
                joined.push_back(std::move(combined));
            }
        }
    } else {
        // Nested loop join - also handles LEFT JOIN's unmatched rows.
        for (const Record& leftRow : leftRows) {
            bool matchedAny = false;
            for (const Record& rightRow : rightRows) {
                if (compareValues(leftRow[leftColIdx], rightRow[rightColIdx]) == 0) {
                    matchedAny = true;
                    Record combined;
                    combined.insert(combined.end(), leftRow.begin(), leftRow.end());
                    combined.insert(combined.end(), rightRow.begin(), rightRow.end());
                    joined.push_back(std::move(combined));
                }
            }
            if (!matchedAny && join.isLeftJoin) {
                Record combined;
                combined.insert(combined.end(), leftRow.begin(), leftRow.end());
                for (size_t i = 0; i < rightWidth; i++) combined.push_back(Value::makeNull());
                joined.push_back(std::move(combined));
            }
        }
    }

    std::vector<Record> matched;
    for (const Record& row : joined) {
        if (!stmt->whereClause.has_value()
            || evaluateConditionOnJoinedRow(row, joinedSchema, stmt->whereClause.value())) {
            matched.push_back(row);
        }
    }

    ExecutionResult result;
    if (stmt->selectAll) {
        for (const auto& [tableName, colDef] : joinedSchema.columns) {
            result.columnNames.push_back(tableName + "." + colDef.name);
        }
        result.rows = std::move(matched);
    } else {
        for (const SelectItem& item : stmt->selectItems) {
            result.columnNames.push_back(item.tableName.empty() ? item.columnName
                                                                   : item.tableName + "." + item.columnName);
        }
        for (const Record& row : matched) {
            Record projected;
            for (const SelectItem& item : stmt->selectItems) {
                int colIndex = -1;
                for (size_t i = 0; i < joinedSchema.columns.size(); i++) {
                    const auto& [tableName, colDef] = joinedSchema.columns[i];
                    bool tableMatches = item.tableName.empty() || tableName == item.tableName;
                    if (tableMatches && colDef.name == item.columnName) {
                        colIndex = static_cast<int>(i);
                        break;
                    }
                }
                if (colIndex == -1) {
                    throw ExecutionError("Unknown column in SELECT: " + item.columnName);
                }
                projected.push_back(row[static_cast<size_t>(colIndex)]);
            }
            result.rows.push_back(std::move(projected));
        }
    }

    if (stmt->orderBy.has_value()) {
        sortResultRows(result, stmt->orderBy.value());
    }

    std::string strategy = useHashJoin ? "hash join" : "nested loop join";
    result.message = std::to_string(result.rows.size()) + " row(s) returned (used " + strategy + ").";
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
