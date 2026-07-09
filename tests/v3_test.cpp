#include <gtest/gtest.h>
#include <cstdio>
#include <cmath>
#include "lexer/Lexer.h"
#include "parser/Parser.h"
#include "buffer/BufferPool.h"
#include "executor/Executor.h"
#include "wal/WriteAheadLog.h"

using namespace minisql;

class V3Test : public ::testing::Test {
protected:
    std::string catalogFile = "test_minisql_v3_catalog.db";
    std::string dataFile = "test_minisql_v3_data.db";
    std::string walFile = "test_minisql_v3.wal";
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
    }

    ExecutionResult run(const std::string& sql) {
        Lexer lexer(sql);
        Parser parser(lexer.tokenize());
        auto stmt = parser.parseStatement();
        return executor->execute(stmt.get());
    }
};

// ---------- Regression: pre-v3 queries behave identically ----------

TEST_F(V3Test, PlainSelectStillWorksUnchanged) {
    run("CREATE TABLE student(id INT, name TEXT, cg FLOAT);");
    run("INSERT INTO student VALUES(1, 'Nikki', 8.7);");
    run("INSERT INTO student VALUES(2, 'Sam', 7.5);");

    auto all = run("SELECT * FROM student;");
    EXPECT_EQ(all.rows.size(), 2);

    auto filtered = run("SELECT name FROM student WHERE id = 2;");
    ASSERT_EQ(filtered.rows.size(), 1);
    EXPECT_EQ(filtered.rows[0][0].textVal, "Sam");
}

// ---------- ORDER BY ----------

TEST_F(V3Test, OrderByAscending) {
    run("CREATE TABLE t(id INT, score FLOAT);");
    run("INSERT INTO t VALUES(1, 9.0);");
    run("INSERT INTO t VALUES(2, 7.5);");
    run("INSERT INTO t VALUES(3, 8.7);");

    auto r = run("SELECT * FROM t ORDER BY score ASC;");
    ASSERT_EQ(r.rows.size(), 3);
    EXPECT_DOUBLE_EQ(r.rows[0][1].floatVal, 7.5);
    EXPECT_DOUBLE_EQ(r.rows[1][1].floatVal, 8.7);
    EXPECT_DOUBLE_EQ(r.rows[2][1].floatVal, 9.0);
}

TEST_F(V3Test, OrderByDescending) {
    run("CREATE TABLE t(id INT, score FLOAT);");
    run("INSERT INTO t VALUES(1, 9.0);");
    run("INSERT INTO t VALUES(2, 7.5);");

    auto r = run("SELECT * FROM t ORDER BY score DESC;");
    ASSERT_EQ(r.rows.size(), 2);
    EXPECT_DOUBLE_EQ(r.rows[0][1].floatVal, 9.0);
    EXPECT_DOUBLE_EQ(r.rows[1][1].floatVal, 7.5);
}

// ---------- Aggregates ----------

TEST_F(V3Test, CountStar) {
    run("CREATE TABLE t(id INT);");
    run("INSERT INTO t VALUES(1);");
    run("INSERT INTO t VALUES(2);");
    run("INSERT INTO t VALUES(3);");

    auto r = run("SELECT COUNT(*) FROM t;");
    ASSERT_EQ(r.rows.size(), 1);
    EXPECT_EQ(r.rows[0][0].intVal, 3);
}

TEST_F(V3Test, SumAvgMinMax) {
    run("CREATE TABLE t(id INT, score FLOAT);");
    run("INSERT INTO t VALUES(1, 10.0);");
    run("INSERT INTO t VALUES(2, 20.0);");
    run("INSERT INTO t VALUES(3, 30.0);");

    auto r = run("SELECT SUM(score), AVG(score), MIN(score), MAX(score) FROM t;");
    ASSERT_EQ(r.rows.size(), 1);
    EXPECT_DOUBLE_EQ(r.rows[0][0].floatVal, 60.0);
    EXPECT_DOUBLE_EQ(r.rows[0][1].floatVal, 20.0);
    EXPECT_DOUBLE_EQ(r.rows[0][2].floatVal, 10.0);
    EXPECT_DOUBLE_EQ(r.rows[0][3].floatVal, 30.0);
}

TEST_F(V3Test, AggregateWithWhere) {
    run("CREATE TABLE t(id INT, score INT);");
    run("INSERT INTO t VALUES(1, 10);");
    run("INSERT INTO t VALUES(2, 20);");
    run("INSERT INTO t VALUES(3, 30);");

    auto r = run("SELECT COUNT(*) FROM t WHERE score > 15;");
    ASSERT_EQ(r.rows.size(), 1);
    EXPECT_EQ(r.rows[0][0].intVal, 2);
}

TEST_F(V3Test, MixingPlainColumnWithAggregateThrows) {
    run("CREATE TABLE t(id INT, score INT);");
    EXPECT_THROW(run("SELECT id, COUNT(*) FROM t;"), ExecutionError);
}

// ---------- GROUP BY ----------

TEST_F(V3Test, GroupByCountAndSum) {
    run("CREATE TABLE enroll(id INT, major TEXT, score INT);");
    run("INSERT INTO enroll VALUES(1, 'CS', 90);");
    run("INSERT INTO enroll VALUES(2, 'CS', 80);");
    run("INSERT INTO enroll VALUES(3, 'Math', 70);");

    auto r = run("SELECT COUNT(*), SUM(score) FROM enroll GROUP BY major;");
    ASSERT_EQ(r.rows.size(), 2);
    // std::map keeps groups sorted by key - "CS" < "Math"
    EXPECT_EQ(r.rows[0][0].textVal, "CS");
    EXPECT_EQ(r.rows[0][1].intVal, 2);
    EXPECT_EQ(r.rows[0][2].intVal, 170);
    EXPECT_EQ(r.rows[1][0].textVal, "Math");
    EXPECT_EQ(r.rows[1][1].intVal, 1);
    EXPECT_EQ(r.rows[1][2].intVal, 70);
}

// ---------- JOIN ----------

TEST_F(V3Test, InnerJoinBasic) {
    run("CREATE TABLE student(id INT, name TEXT);");
    run("INSERT INTO student VALUES(1, 'Nikki');");
    run("INSERT INTO student VALUES(2, 'Sam');");
    run("CREATE TABLE course(id INT, studentId INT, code TEXT);");
    run("INSERT INTO course VALUES(1, 1, 'CS101');");
    run("INSERT INTO course VALUES(2, 2, 'CS102');");
    run("INSERT INTO course VALUES(3, 1, 'CS103');");

    auto r = run("SELECT student.name, course.code FROM student JOIN course "
                  "ON student.id = course.studentId;");
    EXPECT_EQ(r.rows.size(), 3);
    for (const auto& row : r.rows) {
        EXPECT_FALSE(row[0].textVal.empty());
        EXPECT_FALSE(row[1].textVal.empty());
    }
}

TEST_F(V3Test, InnerJoinWithWhere) {
    run("CREATE TABLE student(id INT, name TEXT);");
    run("INSERT INTO student VALUES(1, 'Nikki');");
    run("INSERT INTO student VALUES(2, 'Sam');");
    run("CREATE TABLE course(id INT, studentId INT, code TEXT);");
    run("INSERT INTO course VALUES(1, 1, 'CS101');");
    run("INSERT INTO course VALUES(2, 2, 'CS102');");

    auto r = run("SELECT student.name, course.code FROM student JOIN course "
                  "ON student.id = course.studentId WHERE course.code = 'CS101';");
    ASSERT_EQ(r.rows.size(), 1);
    EXPECT_EQ(r.rows[0][0].textVal, "Nikki");
}

TEST_F(V3Test, LeftJoinIncludesUnmatchedWithNull) {
    run("CREATE TABLE student(id INT, name TEXT);");
    run("INSERT INTO student VALUES(1, 'Nikki');");
    run("INSERT INTO student VALUES(2, 'Sam');");
    run("INSERT INTO student VALUES(3, 'Alex');");  // no matching course
    run("CREATE TABLE course(id INT, studentId INT, code TEXT);");
    run("INSERT INTO course VALUES(1, 1, 'CS101');");
    run("INSERT INTO course VALUES(2, 2, 'CS102');");

    auto r = run("SELECT student.name, course.code FROM student LEFT JOIN course "
                  "ON student.id = course.studentId;");
    ASSERT_EQ(r.rows.size(), 3);

    bool foundAlexWithNull = false;
    for (const auto& row : r.rows) {
        if (row[0].textVal == "Alex") {
            EXPECT_TRUE(row[1].isNull);
            foundAlexWithNull = true;
        }
    }
    EXPECT_TRUE(foundAlexWithNull);
}

TEST_F(V3Test, SelectStarWithJoinProducesQualifiedHeaders) {
    run("CREATE TABLE student(id INT, name TEXT);");
    run("INSERT INTO student VALUES(1, 'Nikki');");
    run("CREATE TABLE course(id INT, studentId INT, code TEXT);");
    run("INSERT INTO course VALUES(1, 1, 'CS101');");

    auto r = run("SELECT * FROM student JOIN course ON student.id = course.studentId;");
    ASSERT_EQ(r.rows.size(), 1);
    EXPECT_EQ(r.columnNames[0], "student.id");
    EXPECT_EQ(r.columnNames.back(), "course.code");
}

TEST_F(V3Test, HashJoinUsedForLargeTables) {
    run("CREATE TABLE big_a(id INT, val TEXT);");
    run("CREATE TABLE big_b(id INT, aId INT);");
    for (int i = 0; i < 60; i++) {
        run("INSERT INTO big_a VALUES(" + std::to_string(i) + ", 'a" + std::to_string(i) + "');");
        run("INSERT INTO big_b VALUES(" + std::to_string(i) + ", " + std::to_string(i) + ");");
    }

    auto r = run("SELECT big_a.val, big_b.id FROM big_a JOIN big_b ON big_a.id = big_b.aId;");
    EXPECT_EQ(r.rows.size(), 60);
    EXPECT_NE(r.message.find("hash join"), std::string::npos);
}

TEST_F(V3Test, SmallJoinUsesNestedLoop) {
    run("CREATE TABLE student(id INT, name TEXT);");
    run("INSERT INTO student VALUES(1, 'Nikki');");
    run("CREATE TABLE course(id INT, studentId INT, code TEXT);");
    run("INSERT INTO course VALUES(1, 1, 'CS101');");

    auto r = run("SELECT student.name, course.code FROM student JOIN course "
                  "ON student.id = course.studentId;");
    EXPECT_NE(r.message.find("nested loop join"), std::string::npos);
}

TEST_F(V3Test, AggregateCombinedWithJoinThrows) {
    run("CREATE TABLE student(id INT, name TEXT);");
    run("CREATE TABLE course(id INT, studentId INT, code TEXT);");
    EXPECT_THROW(
        run("SELECT COUNT(*) FROM student JOIN course ON student.id = course.studentId;"),
        ExecutionError);
}
