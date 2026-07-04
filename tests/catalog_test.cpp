#include <gtest/gtest.h>
#include <cstdio>
#include "catalog/Catalog.h"

using namespace minisql;

class CatalogTest : public ::testing::Test {
protected:
    std::string testFile = "test_minisql_catalog.db";
    void TearDown() override {
        std::remove(testFile.c_str());
    }
};

TEST_F(CatalogTest, NewCatalogIsEmpty) {
    CatalogManager catalog(testFile);
    EXPECT_TRUE(catalog.listTableNames().empty());
    EXPECT_FALSE(catalog.tableExists("student"));
}

TEST(CatalogEncodingTest, EncodeThenDecodeRoundTrips) {
    TableSchema schema;
    schema.tableName = "student";
    schema.columns = {
        {"id", ColumnType::INT},
        {"name", ColumnType::TEXT},
        {"cg", ColumnType::FLOAT},
    };

    Record encoded = CatalogManager::encodeSchema(schema);
    TableSchema decoded = CatalogManager::decodeSchema(encoded);

    EXPECT_EQ(decoded.tableName, "student");
    ASSERT_EQ(decoded.columns.size(), 3);
    EXPECT_EQ(decoded.columns[0].name, "id");
    EXPECT_EQ(decoded.columns[0].type, ColumnType::INT);
    EXPECT_EQ(decoded.columns[1].name, "name");
    EXPECT_EQ(decoded.columns[1].type, ColumnType::TEXT);
    EXPECT_EQ(decoded.columns[2].name, "cg");
    EXPECT_EQ(decoded.columns[2].type, ColumnType::FLOAT);
}

TEST(CatalogEncodingTest, EncodeThenDecodeSingleColumnTable) {
    TableSchema schema;
    schema.tableName = "t";
    schema.columns = {{"x", ColumnType::INT}};

    Record encoded = CatalogManager::encodeSchema(schema);
    TableSchema decoded = CatalogManager::decodeSchema(encoded);

    EXPECT_EQ(decoded.tableName, "t");
    ASSERT_EQ(decoded.columns.size(), 1);
    EXPECT_EQ(decoded.columns[0].name, "x");
}

TEST_F(CatalogTest, CreateTableThenExists) {
    CatalogManager catalog(testFile);
    catalog.createTable("student", {
        {"id", ColumnType::INT},
        {"name", ColumnType::TEXT},
        {"cg", ColumnType::FLOAT},
    });

    EXPECT_TRUE(catalog.tableExists("student"));
    const TableSchema& schema = catalog.getTable("student");
    EXPECT_EQ(schema.tableName, "student");
    ASSERT_EQ(schema.columns.size(), 3);
    EXPECT_EQ(schema.columns[1].name, "name");
}

TEST_F(CatalogTest, CreateDuplicateTableThrows) {
    CatalogManager catalog(testFile);
    catalog.createTable("student", {{"id", ColumnType::INT}});
    EXPECT_THROW(catalog.createTable("student", {{"id", ColumnType::INT}}), CatalogError);
}

TEST_F(CatalogTest, GetNonexistentTableThrows) {
    CatalogManager catalog(testFile);
    EXPECT_THROW(catalog.getTable("ghost"), CatalogError);
}

TEST_F(CatalogTest, DropTableRemovesIt) {
    CatalogManager catalog(testFile);
    catalog.createTable("student", {{"id", ColumnType::INT}});
    ASSERT_TRUE(catalog.tableExists("student"));

    catalog.dropTable("student");
    EXPECT_FALSE(catalog.tableExists("student"));
    EXPECT_THROW(catalog.getTable("student"), CatalogError);
}

TEST_F(CatalogTest, DropNonexistentTableThrows) {
    CatalogManager catalog(testFile);
    EXPECT_THROW(catalog.dropTable("ghost"), CatalogError);
}

TEST_F(CatalogTest, MultipleTablesTrackedIndependently) {
    CatalogManager catalog(testFile);
    catalog.createTable("student", {{"id", ColumnType::INT}, {"name", ColumnType::TEXT}});
    catalog.createTable("course", {{"code", ColumnType::TEXT}, {"credits", ColumnType::INT}});

    EXPECT_TRUE(catalog.tableExists("student"));
    EXPECT_TRUE(catalog.tableExists("course"));
    EXPECT_EQ(catalog.listTableNames().size(), 2);
    EXPECT_EQ(catalog.getTable("course").columns[0].name, "code");
}

TEST_F(CatalogTest, PersistsAcrossCatalogManagerInstances) {
    {
        CatalogManager catalog(testFile);
        catalog.createTable("student", {
            {"id", ColumnType::INT},
            {"name", ColumnType::TEXT},
            {"cg", ColumnType::FLOAT},
        });
    }  // catalog destructed, file closed

    {
        CatalogManager catalog(testFile);
        ASSERT_TRUE(catalog.tableExists("student"));
        const TableSchema& schema = catalog.getTable("student");
        ASSERT_EQ(schema.columns.size(), 3);
        EXPECT_EQ(schema.columns[2].name, "cg");
        EXPECT_EQ(schema.columns[2].type, ColumnType::FLOAT);
    }
}

TEST_F(CatalogTest, DropThenReopenReflectsRemoval) {
    {
        CatalogManager catalog(testFile);
        catalog.createTable("student", {{"id", ColumnType::INT}});
        catalog.createTable("course", {{"code", ColumnType::TEXT}});
        catalog.dropTable("student");
    }

    {
        CatalogManager catalog(testFile);
        EXPECT_FALSE(catalog.tableExists("student"));
        EXPECT_TRUE(catalog.tableExists("course"));
    }
}
