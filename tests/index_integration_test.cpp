#include <gtest/gtest.h>
#include <cstdio>
#include "lexer/Lexer.h"
#include "parser/Parser.h"
#include "buffer/BufferPool.h"
#include "executor/Executor.h"
#include "wal/WriteAheadLog.h"

using namespace minisql;

class IndexTest : public ::testing::Test {
protected:
    std::string catalogFile = "test_minisql_idx_catalog.db";
    std::string dataFile = "test_minisql_idx_data.db";
    std::string walFile = "test_minisql_idx.wal";
    std::unique_ptr<CatalogManager> catalog;
    std::unique_ptr<DiskManager> dataDisk;
    std::unique_ptr<BufferPool> bufferPool;
    std::unique_ptr<WriteAheadLog> wal;
    std::unique_ptr<Executor> executor;

    void SetUp() override {
        catalog = std::make_unique<CatalogManager>(catalogFile);
        dataDisk = std::make_unique<DiskManager>(dataFile);
        wal = std::make_unique<WriteAheadLog>(walFile);
        bufferPool = std::make_unique<BufferPool>(*dataDisk, 64);
        executor = std::make_unique<Executor>(*catalog, *bufferPool, *wal, dataFile, catalogFile);
    }

    void TearDown() override {
        std::remove(walFile.c_str());
        std::remove(catalogFile.c_str());
        std::remove(dataFile.c_str());
    }

    ExecutionResult run(const std::string& sql) {
        Lexer lexer(sql);
        Parser parser(lexer.tokenize());
        auto stmt = parser.parseStatement();
        return executor->execute(stmt.get());
    }
};

TEST_F(IndexTest, CreateIndexOnExistingRowsSucceeds) {
    run("CREATE TABLE student(id INT, name TEXT);");
    run("INSERT INTO student VALUES(1, 'Nikki');");
    run("INSERT INTO student VALUES(2, 'Sam');");

    ExecutionResult result = run("CREATE INDEX idx_id ON student(id);");
    EXPECT_NE(result.message.find("idx_id"), std::string::npos);
    EXPECT_TRUE(executor->hasIndex("student", "id"));
    EXPECT_FALSE(executor->hasIndex("student", "name"));
}

TEST_F(IndexTest, CreateIndexOnNonIntColumnThrows) {
    run("CREATE TABLE student(id INT, name TEXT);");
    EXPECT_THROW(run("CREATE INDEX idx_name ON student(name);"), ExecutionError);
}

TEST_F(IndexTest, CreateIndexOnUnknownColumnThrows) {
    run("CREATE TABLE student(id INT);");
    EXPECT_THROW(run("CREATE INDEX idx_ghost ON student(ghost);"), ExecutionError);
}

TEST_F(IndexTest, SelectWithIndexStillReturnsCorrectRows) {
    // Regardless of whether executeSelect actually USES the index yet,
    // results must stay correct once one exists.
    run("CREATE TABLE student(id INT, name TEXT);");
    run("INSERT INTO student VALUES(1, 'Nikki');");
    run("INSERT INTO student VALUES(2, 'Sam');");
    run("INSERT INTO student VALUES(3, 'Alex');");
    run("CREATE INDEX idx_id ON student(id);");

    ExecutionResult result = run("SELECT * FROM student WHERE id = 2;");
    ASSERT_EQ(result.rows.size(), 1);
    EXPECT_EQ(result.rows[0][1].textVal, "Sam");
}

TEST_F(IndexTest, InsertAfterIndexCreationKeepsIndexUsable) {
    run("CREATE TABLE student(id INT, name TEXT);");
    run("INSERT INTO student VALUES(1, 'Nikki');");
    run("CREATE INDEX idx_id ON student(id);");
    run("INSERT INTO student VALUES(2, 'Sam');");  // inserted AFTER index exists

    ExecutionResult result = run("SELECT * FROM student WHERE id = 2;");
    ASSERT_EQ(result.rows.size(), 1);
    EXPECT_EQ(result.rows[0][1].textVal, "Sam");
}

TEST_F(IndexTest, DeleteThenSelectStaysCorrectAfterIndexRebuild) {
    run("CREATE TABLE student(id INT, name TEXT);");
    run("INSERT INTO student VALUES(1, 'Nikki');");
    run("INSERT INTO student VALUES(2, 'Sam');");
    run("INSERT INTO student VALUES(3, 'Alex');");
    run("CREATE INDEX idx_id ON student(id);");

    run("DELETE FROM student WHERE id = 2;");

    // Row locations for id=1 and id=3 may have shifted after the delete's
    // rewrite - this only passes if rebuildIndexesForTable() ran and the
    // index reflects the NEW locations, not stale ones.
    ExecutionResult result1 = run("SELECT * FROM student WHERE id = 1;");
    ASSERT_EQ(result1.rows.size(), 1);
    EXPECT_EQ(result1.rows[0][1].textVal, "Nikki");

    ExecutionResult result3 = run("SELECT * FROM student WHERE id = 3;");
    ASSERT_EQ(result3.rows.size(), 1);
    EXPECT_EQ(result3.rows[0][1].textVal, "Alex");

    ExecutionResult resultDeleted = run("SELECT * FROM student WHERE id = 2;");
    EXPECT_EQ(resultDeleted.rows.size(), 0);
}

TEST_F(IndexTest, UpdateThenSelectStaysCorrectAfterIndexRebuild) {
    run("CREATE TABLE student(id INT, cg FLOAT);");
    run("INSERT INTO student VALUES(1, 7.0);");
    run("INSERT INTO student VALUES(2, 7.0);");
    run("CREATE INDEX idx_id ON student(id);");

    run("UPDATE student SET cg = 9.5 WHERE id = 1;");

    ExecutionResult result = run("SELECT * FROM student WHERE id = 1;");
    ASSERT_EQ(result.rows.size(), 1);
    EXPECT_DOUBLE_EQ(result.rows[0][1].floatVal, 9.5);
}
