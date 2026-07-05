#include "catalog/Catalog.h"

#include "storage/Page.h"

namespace minisql {

CatalogManager::CatalogManager(const std::string& catalogFilePath) {
    diskManager_ = std::make_unique<DiskManager>(catalogFilePath);
    loadFromDisk();
}

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

void CatalogManager::reloadFromDisk() {
    tables_.clear();
    loadFromDisk();
}

Record CatalogManager::encodeSchema(const TableSchema& schema) {
    Record record;
    record.push_back(Value::makeText(schema.tableName));
    record.push_back(Value::makeInt(static_cast<int32_t>(schema.columns.size())));
    for (const ColumnDefinition& col : schema.columns) {
        record.push_back(Value::makeText(col.name));
        record.push_back(Value::makeInt(static_cast<int32_t>(col.type)));
    }
    // NEW: append the page list - same pattern as the column list above,
    // a count followed by that many values.
    record.push_back(Value::makeInt(static_cast<int32_t>(schema.pageIds.size())));
    for (int32_t pageId : schema.pageIds) {
        record.push_back(Value::makeInt(pageId));
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

    // NEW: read the trailing page list, right after the columns.
    const size_t pageCountIdx = 2 + static_cast<size_t>(columnCount) * 2;
    const int32_t pageCount = record[pageCountIdx].intVal;
    schema.pageIds.reserve(static_cast<size_t>(pageCount));
    for (int32_t i = 0; i < pageCount; ++i) {
        schema.pageIds.push_back(record[pageCountIdx + 1 + static_cast<size_t>(i)].intVal);
    }

    return schema;
}

void CatalogManager::addPageToTable(const std::string& tableName, int32_t pageId) {
    if (!tableExists(tableName)) {
        throw CatalogError("Table does not exist: " + tableName);
    }
    tables_[tableName].pageIds.push_back(pageId);
    persistAll();
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
