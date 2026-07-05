#include <gtest/gtest.h>
#include <cstdio>
#include <fstream>
#include "lexer/Lexer.h"
#include "parser/Parser.h"
#include "buffer/BufferPool.h"
#include "executor/Executor.h"
#include "wal/WriteAheadLog.h"

using namespace minisql;

class TransactionTest : public ::testing::Test {
protected:
    std::string catalogFile = "test_minisql_txn_catalog.db";
    std::string dataFile = "test_minisql_txn_data.db";
    std::string walFile = "test_minisql_txn.wal";
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
        std::remove(catalogFile.c_str());
        std::remove(dataFile.c_str());
        std::remove(walFile.c_str());
        std::remove((catalogFile + ".snapshot").c_str());
        std::remove((dataFile + ".snapshot").c_str());
    }

    ExecutionResult run(const std::string& sql) {
        Lexer lexer(sql);
        Parser parser(lexer.tokenize());
        auto stmt = parser.parseStatement();
        return executor->execute(stmt.get());
    }
};

TEST_F(TransactionTest, BeginCreatesSnapshotFiles) {
    run("CREATE TABLE t(id INT);");
    run("BEGIN;");
    EXPECT_TRUE(std::ifstream(dataFile + ".snapshot").good());
    EXPECT_TRUE(std::ifstream(catalogFile + ".snapshot").good());
}

TEST_F(TransactionTest, CommitRemovesSnapshotFiles) {
    run("CREATE TABLE t(id INT);");
    run("BEGIN;");
    run("INSERT INTO t VALUES(1);");
    run("COMMIT;");
    EXPECT_FALSE(std::ifstream(dataFile + ".snapshot").good());
    EXPECT_FALSE(std::ifstream(catalogFile + ".snapshot").good());
}

TEST_F(TransactionTest, CommitKeepsChanges) {
    run("CREATE TABLE t(id INT);");
    run("BEGIN;");
    run("INSERT INTO t VALUES(1);");
    run("INSERT INTO t VALUES(2);");
    run("COMMIT;");

    auto result = run("SELECT * FROM t;");
    EXPECT_EQ(result.rows.size(), 2);
}

TEST_F(TransactionTest, DoubleBeginThrows) {
    run("BEGIN;");
    EXPECT_THROW(run("BEGIN;"), ExecutionError);
}

TEST_F(TransactionTest, CommitWithoutBeginThrows) {
    EXPECT_THROW(run("COMMIT;"), ExecutionError);
}

TEST_F(TransactionTest, RollbackWithoutBeginThrows) {
    EXPECT_THROW(run("ROLLBACK;"), ExecutionError);
}

TEST_F(TransactionTest, RollbackUndoesInserts) {
    run("CREATE TABLE t(id INT);");
    run("INSERT INTO t VALUES(1);");  // outside any transaction - permanent

    run("BEGIN;");
    run("INSERT INTO t VALUES(2);");
    run("INSERT INTO t VALUES(3);");
    run("ROLLBACK;");

    auto result = run("SELECT * FROM t;");
    ASSERT_EQ(result.rows.size(), 1);
    EXPECT_EQ(result.rows[0][0].intVal, 1);
}

TEST_F(TransactionTest, RollbackUndoesCreateTable) {
    run("BEGIN;");
    run("CREATE TABLE ghost(id INT);");
    run("ROLLBACK;");

    EXPECT_THROW(run("SELECT * FROM ghost;"), std::exception);
}

TEST_F(TransactionTest, RollbackUndoesDeleteAndUpdate) {
    run("CREATE TABLE t(id INT, val TEXT);");
    run("INSERT INTO t VALUES(1, 'a');");
    run("INSERT INTO t VALUES(2, 'b');");

    run("BEGIN;");
    run("DELETE FROM t WHERE id = 1;");
    run("UPDATE t SET val = 'changed' WHERE id = 2;");
    run("ROLLBACK;");

    auto result = run("SELECT * FROM t;");
    ASSERT_EQ(result.rows.size(), 2);
    // Original values restored, in some order - check both are present.
    bool foundOriginal1 = false, foundOriginal2 = false;
    for (const auto& row : result.rows) {
        if (row[0].intVal == 1 && row[1].textVal == "a") foundOriginal1 = true;
        if (row[0].intVal == 2 && row[1].textVal == "b") foundOriginal2 = true;
    }
    EXPECT_TRUE(foundOriginal1);
    EXPECT_TRUE(foundOriginal2);
}

TEST_F(TransactionTest, RollbackRemovesSnapshotFiles) {
    run("CREATE TABLE t(id INT);");
    run("BEGIN;");
    run("INSERT INTO t VALUES(1);");
    run("ROLLBACK;");
    EXPECT_FALSE(std::ifstream(dataFile + ".snapshot").good());
    EXPECT_FALSE(std::ifstream(catalogFile + ".snapshot").good());
}

TEST_F(TransactionTest, AfterRollbackNewTransactionCanBegin) {
    run("CREATE TABLE t(id INT);");
    run("BEGIN;");
    run("INSERT INTO t VALUES(1);");
    run("ROLLBACK;");

    // Should not throw "already in progress" - inTransaction_ must have
    // been reset.
    EXPECT_NO_THROW(run("BEGIN;"));
    run("INSERT INTO t VALUES(99);");
    run("COMMIT;");

    auto result = run("SELECT * FROM t;");
    ASSERT_EQ(result.rows.size(), 1);
    EXPECT_EQ(result.rows[0][0].intVal, 99);
}
