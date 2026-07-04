#include "catalog/Catalog.h"

#include "storage/Page.h"

namespace minisql {

CatalogManager::CatalogManager(const std::string& catalogFilePath) {
    diskManager_ = std::make_unique<DiskManager>(catalogFilePath);
    loadFromDisk();
}

// ---------- Map operations (already implemented for you) ----------

void CatalogManager::createTable(const std::string& tableName,
                                  const std::vector<ColumnDefinition>& columns) {
    if (tableExists(tableName)) {
        throw CatalogError("Table already exists: " + tableName);
    }
    TableSchema schema;
    schema.tableName = tableName;
    schema.columns = columns;
    tables_[tableName] = std::move(schema);
    persistAll();
}

void CatalogManager::dropTable(const std::string& tableName) {
    if (!tableExists(tableName)) {
        throw CatalogError("Table does not exist: " + tableName);
    }
    tables_.erase(tableName);
    persistAll();
}

bool CatalogManager::tableExists(const std::string& tableName) const {
    return tables_.find(tableName) != tables_.end();
}

const TableSchema& CatalogManager::getTable(const std::string& tableName) const {
    auto it = tables_.find(tableName);
    if (it == tables_.end()) {
        throw CatalogError("Table does not exist: " + tableName);
    }
    return it->second;
}

std::vector<std::string> CatalogManager::listTableNames() const {
    std::vector<std::string> names;
    names.reserve(tables_.size());
    for (const auto& [name, schema] : tables_) {
        names.push_back(name);
    }
    return names;
}

// ---------- Core logic (YOUR TURN - see Catalog.h for detailed guidance) ----------
//
// Suggested build order:
//   1. encodeSchema  - build a Record from a TableSchema. Pure in-memory,
//      no disk involved, easiest to reason about first.
//   2. decodeSchema  - the exact inverse.
//   3. persistAll    - uses encodeSchema + Page::appendRecord + DiskManager.
//   4. loadFromDisk  - uses decodeSchema + Page::getAllRecords + DiskManager.

Record CatalogManager::encodeSchema(const TableSchema& schema) {
    Record record;

    record.push_back(Value::makeText(schema.tableName));
    record.push_back(Value::makeInt(static_cast<int32_t>(schema.columns.size())));

    for (const ColumnDefinition& col : schema.columns) {
        record.push_back(Value::makeText(col.name));
        record.push_back(Value::makeInt(static_cast<int32_t>(col.type)));
    }

    return record;
}

TableSchema CatalogManager::decodeSchema(const Record& record) {
    TableSchema schema;
    schema.tableName = record[0].textVal;

    const int32_t columnCount = record[1].intVal;
    schema.columns.reserve(static_cast<size_t>(columnCount));

    for (int32_t i = 0; i < columnCount; ++i) {
        const size_t idx = 2 + static_cast<size_t>(i) * 2;
        ColumnDefinition col;
        col.name = record[idx].textVal;
        col.type = static_cast<ColumnType>(record[idx + 1].intVal);
        schema.columns.push_back(std::move(col));
    }

    return schema;
}

void CatalogManager::persistAll() {
    Page page;
    for (const auto& [name, schema] : tables_) {
        (void)name;
        page.appendRecord(encodeSchema(schema));
    }

    if (diskManager_->numPages() == 0) {
        diskManager_->allocatePage();
    }
    diskManager_->writePage(0, page);
}

void CatalogManager::loadFromDisk() {
    // Base case already handled for you: a brand new catalog file has no
    // pages yet, so there's nothing to load.
    if (diskManager_->numPages() == 0) {
        return;
    }

    Page page;
    diskManager_->readPage(0, page);
    for (const Record& record : page.getAllRecords()) {
        TableSchema schema = decodeSchema(record);
        tables_[schema.tableName] = std::move(schema);
    }
}

}  // namespace minisql
