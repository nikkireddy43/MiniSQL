#include <gtest/gtest.h>
#include "lexer/Lexer.h"
#include "parser/Parser.h"

using namespace minisql;

// Helper: run source through the real Lexer, then the Parser.
// This also means these tests double-check Lexer + Parser integration,
// not just the Parser in isolation.
static std::unique_ptr<Statement> parse(const std::string& source) {
    Lexer lexer(source);
    Parser parser(lexer.tokenize());
    return parser.parseStatement();
}

TEST(ParserTest, CreateTableBasic) {
    auto stmt = parse("CREATE TABLE student(id INT, name TEXT, cg FLOAT);");
    ASSERT_EQ(stmt->type, StatementType::CREATE_TABLE);

    auto* create = static_cast<CreateTableStatement*>(stmt.get());
    EXPECT_EQ(create->tableName, "student");
    ASSERT_EQ(create->columns.size(), 3);
    EXPECT_EQ(create->columns[0].name, "id");
    EXPECT_EQ(create->columns[0].type, ColumnType::INT);
    EXPECT_EQ(create->columns[1].name, "name");
    EXPECT_EQ(create->columns[1].type, ColumnType::TEXT);
    EXPECT_EQ(create->columns[2].name, "cg");
    EXPECT_EQ(create->columns[2].type, ColumnType::FLOAT);
}

TEST(ParserTest, CreateTableSingleColumn) {
    auto stmt = parse("CREATE TABLE t(x INT);");
    auto* create = static_cast<CreateTableStatement*>(stmt.get());
    EXPECT_EQ(create->tableName, "t");
    ASSERT_EQ(create->columns.size(), 1);
    EXPECT_EQ(create->columns[0].name, "x");
}

TEST(ParserTest, InsertBasic) {
    auto stmt = parse("INSERT INTO student VALUES(1, 'Nikki', 8.7);");
    ASSERT_EQ(stmt->type, StatementType::INSERT);

    auto* insert = static_cast<InsertStatement*>(stmt.get());
    EXPECT_EQ(insert->tableName, "student");
    ASSERT_EQ(insert->values.size(), 3);
    EXPECT_EQ(insert->values[0].kind, Literal::Kind::INT);
    EXPECT_EQ(insert->values[0].text, "1");
    EXPECT_EQ(insert->values[1].kind, Literal::Kind::STRING);
    EXPECT_EQ(insert->values[1].text, "Nikki");
    EXPECT_EQ(insert->values[2].kind, Literal::Kind::FLOAT);
    EXPECT_EQ(insert->values[2].text, "8.7");
}

TEST(ParserTest, SelectStar) {
    auto stmt = parse("SELECT * FROM student;");
    ASSERT_EQ(stmt->type, StatementType::SELECT);

    auto* select = static_cast<SelectStatement*>(stmt.get());
    EXPECT_TRUE(select->selectAll);
    EXPECT_EQ(select->tableName, "student");
    EXPECT_FALSE(select->whereClause.has_value());
}

TEST(ParserTest, SelectColumnList) {
    auto stmt = parse("SELECT id, name FROM student;");
    auto* select = static_cast<SelectStatement*>(stmt.get());
    EXPECT_FALSE(select->selectAll);
    ASSERT_EQ(select->columns.size(), 2);
    EXPECT_EQ(select->columns[0], "id");
    EXPECT_EQ(select->columns[1], "name");
}

TEST(ParserTest, SelectWithWhere) {
    auto stmt = parse("SELECT * FROM student WHERE id = 5;");
    auto* select = static_cast<SelectStatement*>(stmt.get());
    ASSERT_TRUE(select->whereClause.has_value());
    EXPECT_EQ(select->whereClause->column, "id");
    EXPECT_EQ(select->whereClause->op, TokenType::EQUAL);
    EXPECT_EQ(select->whereClause->value.text, "5");
}

TEST(ParserTest, SelectWithWhereInequality) {
    auto stmt = parse("SELECT * FROM student WHERE cg >= 8.5;");
    auto* select = static_cast<SelectStatement*>(stmt.get());
    ASSERT_TRUE(select->whereClause.has_value());
    EXPECT_EQ(select->whereClause->column, "cg");
    EXPECT_EQ(select->whereClause->op, TokenType::GREATER_EQUAL);
    EXPECT_EQ(select->whereClause->value.text, "8.5");
}

TEST(ParserTest, UpdateBasic) {
    auto stmt = parse("UPDATE student SET cg = 9.0 WHERE id = 5;");
    ASSERT_EQ(stmt->type, StatementType::UPDATE);

    auto* update = static_cast<UpdateStatement*>(stmt.get());
    EXPECT_EQ(update->tableName, "student");
    EXPECT_EQ(update->setColumn, "cg");
    EXPECT_EQ(update->setValue.text, "9.0");
    ASSERT_TRUE(update->whereClause.has_value());
    EXPECT_EQ(update->whereClause->column, "id");
}

TEST(ParserTest, UpdateWithoutWhere) {
    auto stmt = parse("UPDATE student SET cg = 9.0;");
    auto* update = static_cast<UpdateStatement*>(stmt.get());
    EXPECT_FALSE(update->whereClause.has_value());
}

TEST(ParserTest, DeleteWithWhere) {
    auto stmt = parse("DELETE FROM student WHERE id = 5;");
    ASSERT_EQ(stmt->type, StatementType::DELETE);

    auto* del = static_cast<DeleteStatement*>(stmt.get());
    EXPECT_EQ(del->tableName, "student");
    ASSERT_TRUE(del->whereClause.has_value());
    EXPECT_EQ(del->whereClause->column, "id");
}

TEST(ParserTest, DeleteWithoutWhere) {
    auto stmt = parse("DELETE FROM student;");
    auto* del = static_cast<DeleteStatement*>(stmt.get());
    EXPECT_FALSE(del->whereClause.has_value());
}

TEST(ParserTest, DropTableBasic) {
    auto stmt = parse("DROP TABLE student;");
    ASSERT_EQ(stmt->type, StatementType::DROP_TABLE);

    auto* drop = static_cast<DropTableStatement*>(stmt.get());
    EXPECT_EQ(drop->tableName, "student");
}

TEST(ParserTest, MissingSemicolonThrows) {
    EXPECT_THROW(parse("DROP TABLE student"), ParseError);
}

TEST(ParserTest, UnknownStatementThrows) {
    EXPECT_THROW(parse("FOO BAR;"), ParseError);
}

TEST(ParserTest, MalformedCreateTableThrows) {
    // Missing closing paren
    EXPECT_THROW(parse("CREATE TABLE student(id INT;"), ParseError);
}
