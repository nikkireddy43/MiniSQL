#include <gtest/gtest.h>
#include "lexer/Lexer.h"

using namespace minisql;

// Small helper to reduce boilerplate in each test.
static std::vector<Token> lex(const std::string& source) {
    Lexer lexer(source);
    return lexer.tokenize();
}

TEST(LexerTest, EmptyInputProducesOnlyEOF) {
    auto tokens = lex("");
    ASSERT_EQ(tokens.size(), 1);
    EXPECT_EQ(tokens[0].type, TokenType::END_OF_FILE);
}

TEST(LexerTest, SingleKeyword) {
    auto tokens = lex("SELECT");
    ASSERT_EQ(tokens.size(), 2);  // SELECT + EOF
    EXPECT_EQ(tokens[0].type, TokenType::SELECT);
}

TEST(LexerTest, KeywordsAreCaseInsensitive) {
    auto tokens = lex("select Select SELECT");
    ASSERT_EQ(tokens.size(), 4);
    EXPECT_EQ(tokens[0].type, TokenType::SELECT);
    EXPECT_EQ(tokens[1].type, TokenType::SELECT);
    EXPECT_EQ(tokens[2].type, TokenType::SELECT);
}

TEST(LexerTest, IdentifierVsKeyword) {
    auto tokens = lex("student");
    ASSERT_EQ(tokens.size(), 2);
    EXPECT_EQ(tokens[0].type, TokenType::IDENTIFIER);
    EXPECT_EQ(tokens[0].text, "student");
}

TEST(LexerTest, IntegerLiteral) {
    auto tokens = lex("42");
    ASSERT_EQ(tokens.size(), 2);
    EXPECT_EQ(tokens[0].type, TokenType::INT_LITERAL);
    EXPECT_EQ(tokens[0].text, "42");
}

TEST(LexerTest, FloatLiteral) {
    auto tokens = lex("8.7");
    ASSERT_EQ(tokens.size(), 2);
    EXPECT_EQ(tokens[0].type, TokenType::FLOAT_LITERAL);
    EXPECT_EQ(tokens[0].text, "8.7");
}

TEST(LexerTest, StringLiteral) {
    auto tokens = lex("'Nikki'");
    ASSERT_EQ(tokens.size(), 2);
    EXPECT_EQ(tokens[0].type, TokenType::STRING_LITERAL);
    // Design choice: text stores the content WITHOUT the surrounding quotes.
    EXPECT_EQ(tokens[0].text, "Nikki");
}

TEST(LexerTest, Symbols) {
    auto tokens = lex("(),;*");
    ASSERT_EQ(tokens.size(), 6);  // 5 symbols + EOF
    EXPECT_EQ(tokens[0].type, TokenType::LPAREN);
    EXPECT_EQ(tokens[1].type, TokenType::RPAREN);
    EXPECT_EQ(tokens[2].type, TokenType::COMMA);
    EXPECT_EQ(tokens[3].type, TokenType::SEMICOLON);
    EXPECT_EQ(tokens[4].type, TokenType::STAR);
}

TEST(LexerTest, ComparisonOperators) {
    auto tokens = lex("= != < <= > >=");
    ASSERT_EQ(tokens.size(), 7);  // 6 operators + EOF
    EXPECT_EQ(tokens[0].type, TokenType::EQUAL);
    EXPECT_EQ(tokens[1].type, TokenType::NOT_EQUAL);
    EXPECT_EQ(tokens[2].type, TokenType::LESS);
    EXPECT_EQ(tokens[3].type, TokenType::LESS_EQUAL);
    EXPECT_EQ(tokens[4].type, TokenType::GREATER);
    EXPECT_EQ(tokens[5].type, TokenType::GREATER_EQUAL);
}

TEST(LexerTest, WhitespaceIsSkipped) {
    auto tokens = lex("SELECT   *\tFROM\nstudent");
    ASSERT_EQ(tokens.size(), 5);  // SELECT * FROM student EOF
    EXPECT_EQ(tokens[0].type, TokenType::SELECT);
    EXPECT_EQ(tokens[1].type, TokenType::STAR);
    EXPECT_EQ(tokens[2].type, TokenType::FROM);
    EXPECT_EQ(tokens[3].type, TokenType::IDENTIFIER);
}

TEST(LexerTest, LineTrackingAcrossNewlines) {
    auto tokens = lex("SELECT\n*\nFROM");
    ASSERT_EQ(tokens.size(), 4);
    EXPECT_EQ(tokens[0].line, 1);
    EXPECT_EQ(tokens[1].line, 2);
    EXPECT_EQ(tokens[2].line, 3);
}

// The full example from the project spec, end to end.
TEST(LexerTest, FullCreateTableStatement) {
    auto tokens = lex("CREATE TABLE student(id INT, name TEXT, cg FLOAT);");

    std::vector<TokenType> expected = {
        TokenType::CREATE, TokenType::TABLE, TokenType::IDENTIFIER,
        TokenType::LPAREN, TokenType::IDENTIFIER, TokenType::INT_TYPE,
        TokenType::COMMA, TokenType::IDENTIFIER, TokenType::TEXT_TYPE,
        TokenType::COMMA, TokenType::IDENTIFIER, TokenType::FLOAT_TYPE,
        TokenType::RPAREN, TokenType::SEMICOLON, TokenType::END_OF_FILE,
    };

    ASSERT_EQ(tokens.size(), expected.size());
    for (size_t i = 0; i < expected.size(); i++) {
        EXPECT_EQ(tokens[i].type, expected[i])
            << "Mismatch at index " << i << ": expected "
            << tokenTypeToString(expected[i]) << ", got "
            << tokenTypeToString(tokens[i].type);
    }
}

TEST(LexerTest, FullInsertStatement) {
    auto tokens = lex("INSERT INTO student VALUES(1,'Nikki',8.7);");

    std::vector<TokenType> expected = {
        TokenType::INSERT, TokenType::INTO, TokenType::IDENTIFIER,
        TokenType::VALUES, TokenType::LPAREN, TokenType::INT_LITERAL,
        TokenType::COMMA, TokenType::STRING_LITERAL, TokenType::COMMA,
        TokenType::FLOAT_LITERAL, TokenType::RPAREN, TokenType::SEMICOLON,
        TokenType::END_OF_FILE,
    };

    ASSERT_EQ(tokens.size(), expected.size());
    for (size_t i = 0; i < expected.size(); i++) {
        EXPECT_EQ(tokens[i].type, expected[i])
            << "Mismatch at index " << i << ": expected "
            << tokenTypeToString(expected[i]) << ", got "
            << tokenTypeToString(tokens[i].type);
    }
}
