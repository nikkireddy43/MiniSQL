#include <gtest/gtest.h>
#include <cstdio>
#include "lexer/Lexer.h"
#include "parser/Parser.h"
#include "buffer/BufferPool.h"
#include "executor/Executor.h"

using namespace minisql;

class ExecutorTest : public ::testing::Test {
protected:
    std::string catalogFile = "test_minisql_exec_catalog.db";
    std::string dataFile = "test_minisql_exec_data.db";
    std::unique_ptr<CatalogManager> catalog;
    std::unique_ptr<DiskManager> dataDisk;
    std::unique_ptr<BufferPool> bufferPool;
    std::unique_ptr<Executor> executor;

    void SetUp() override {
        catalog = std::make_unique<CatalogManager>(catalogFile);
        dataDisk = std::make_unique<DiskManager>(dataFile);
        bufferPool = std::make_unique<BufferPool>(*dataDisk, 64);
        executor = std::make_unique<Executor>(*catalog, *bufferPool, dataFile, catalogFile);
    }

    void TearDown() override {
        std::remove(catalogFile.c_str());
        std::remove(dataFile.c_str());
    }

    // Helper: lex + parse + execute one SQL statement in one call.
    ExecutionResult run(const std::string& sql) {
        Lexer lexer(sql);
        Parser parser(lexer.tokenize());
        auto stmt = parser.parseStatement();
        return executor->execute(stmt.get());
    }
};

TEST_F(ExecutorTest, CreateTableRegistersInCatalogAndAllocatesPage) {
    run("CREATE TABLE student(id INT, name TEXT, cg FLOAT);");
    EXPECT_TRUE(catalog->tableExists("student"));
    EXPECT_EQ(catalog->getTable("student").pageIds.size(), 1);
}

TEST_F(ExecutorTest, InsertThenSelectStarRoundTrips) {
    run("CREATE TABLE student(id INT, name TEXT, cg FLOAT);");
    run("INSERT INTO student VALUES(1, 'Nikki', 8.7);");

    ExecutionResult result = run("SELECT * FROM student;");
    ASSERT_EQ(result.rows.size(), 1);
    EXPECT_EQ(result.rows[0][0].intVal, 1);
    EXPECT_EQ(result.rows[0][1].textVal, "Nikki");
    EXPECT_DOUBLE_EQ(result.rows[0][2].floatVal, 8.7);
}

TEST_F(ExecutorTest, InsertMultipleRowsThenSelectAll) {
    run("CREATE TABLE student(id INT, name TEXT);");
    run("INSERT INTO student VALUES(1, 'Nikki');");
    run("INSERT INTO student VALUES(2, 'Sam');");
    run("INSERT INTO student VALUES(3, 'Alex');");

    ExecutionResult result = run("SELECT * FROM student;");
    ASSERT_EQ(result.rows.size(), 3);
}

TEST_F(ExecutorTest, SelectWithWhereFiltersRows) {
    run("CREATE TABLE student(id INT, name TEXT);");
    run("INSERT INTO student VALUES(1, 'Nikki');");
    run("INSERT INTO student VALUES(2, 'Sam');");

    ExecutionResult result = run("SELECT * FROM student WHERE id = 2;");
    ASSERT_EQ(result.rows.size(), 1);
    EXPECT_EQ(result.rows[0][1].textVal, "Sam");
}

TEST_F(ExecutorTest, SelectSpecificColumnsProjectsFields) {
    run("CREATE TABLE student(id INT, name TEXT, cg FLOAT);");
    run("INSERT INTO student VALUES(1, 'Nikki', 8.7);");

    ExecutionResult result = run("SELECT name FROM student;");
    ASSERT_EQ(result.columnNames.size(), 1);
    EXPECT_EQ(result.columnNames[0], "name");
    ASSERT_EQ(result.rows.size(), 1);
    ASSERT_EQ(result.rows[0].size(), 1);
    EXPECT_EQ(result.rows[0][0].textVal, "Nikki");
}

TEST_F(ExecutorTest, InsertAcrossPageBoundaryStillReadsAllRows) {
    run("CREATE TABLE t(x TEXT);");
    // A long string, inserted many times, to force at least one new page
    // allocation - exercises the "page is full, allocate another" path.
    std::string longText(300, 'a');
    for (int i = 0; i < 30; i++) {
        run("INSERT INTO t VALUES('" + longText + "');");
    }
    ExecutionResult result = run("SELECT * FROM t;");
    EXPECT_EQ(result.rows.size(), 30);
    EXPECT_GT(catalog->getTable("t").pageIds.size(), 1)
        << "Expected more than 1 page to have been allocated";
}

TEST_F(ExecutorTest, DeleteWithWhereRemovesMatchingRows) {
    run("CREATE TABLE student(id INT, name TEXT);");
    run("INSERT INTO student VALUES(1, 'Nikki');");
    run("INSERT INTO student VALUES(2, 'Sam');");

    run("DELETE FROM student WHERE id = 1;");

    ExecutionResult result = run("SELECT * FROM student;");
    ASSERT_EQ(result.rows.size(), 1);
    EXPECT_EQ(result.rows[0][1].textVal, "Sam");
}

TEST_F(ExecutorTest, DeleteWithoutWhereRemovesAllRows) {
    run("CREATE TABLE student(id INT);");
    run("INSERT INTO student VALUES(1);");
    run("INSERT INTO student VALUES(2);");

    run("DELETE FROM student;");

    ExecutionResult result = run("SELECT * FROM student;");
    EXPECT_EQ(result.rows.size(), 0);
}

TEST_F(ExecutorTest, UpdateWithWhereModifiesMatchingRows) {
    run("CREATE TABLE student(id INT, cg FLOAT);");
    run("INSERT INTO student VALUES(1, 7.0);");
    run("INSERT INTO student VALUES(2, 7.0);");

    run("UPDATE student SET cg = 9.5 WHERE id = 1;");

    ExecutionResult result = run("SELECT * FROM student WHERE id = 1;");
    ASSERT_EQ(result.rows.size(), 1);
    EXPECT_DOUBLE_EQ(result.rows[0][1].floatVal, 9.5);

    ExecutionResult unchanged = run("SELECT * FROM student WHERE id = 2;");
    ASSERT_EQ(unchanged.rows.size(), 1);
    EXPECT_DOUBLE_EQ(unchanged.rows[0][1].floatVal, 7.0);
}

TEST_F(ExecutorTest, DataPersistsAcrossExecutorInstances) {
    run("CREATE TABLE student(id INT, name TEXT);");
    run("INSERT INTO student VALUES(1, 'Nikki');");

    // Simulate a restart: fresh Catalog/DiskManager/BufferPool/Executor
    // over the same files.
    catalog = std::make_unique<CatalogManager>(catalogFile);
    dataDisk = std::make_unique<DiskManager>(dataFile);
    bufferPool = std::make_unique<BufferPool>(*dataDisk, 64);
    executor = std::make_unique<Executor>(*catalog, *bufferPool, dataFile, catalogFile);

    ExecutionResult result = run("SELECT * FROM student;");
    ASSERT_EQ(result.rows.size(), 1);
    EXPECT_EQ(result.rows[0][1].textVal, "Nikki");
}
