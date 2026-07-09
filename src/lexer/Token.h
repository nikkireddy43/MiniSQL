#pragma once

#include <string>

namespace minisql {

// Every distinct kind of token the lexer can produce.
// Grouped by category to match the SQL subset in Phase 1 of the roadmap.
enum class TokenType {
    // Keywords
    CREATE, TABLE, INSERT, INTO, VALUES, SELECT, FROM, WHERE,
    UPDATE, SET, DELETE, DROP, INDEX, ON,
    BEGIN_TXN, COMMIT_TXN, ROLLBACK_TXN,
    JOIN, INNER, LEFT, GROUP, BY, ORDER, ASC, DESC,
    COUNT, SUM, AVG, MIN, MAX,

    // Data types
    INT_TYPE, FLOAT_TYPE, TEXT_TYPE,

    // Literals
    IDENTIFIER,     // table names, column names
    INT_LITERAL,    // e.g. 42
    FLOAT_LITERAL,  // e.g. 8.7
    STRING_LITERAL, // e.g. 'Nikki'

    // Symbols
    LPAREN, RPAREN, COMMA, SEMICOLON, STAR, DOT,
    EQUAL, NOT_EQUAL, LESS, LESS_EQUAL, GREATER, GREATER_EQUAL,

    // Control
    END_OF_FILE,
    UNKNOWN  // lexer couldn't classify this character/sequence
};

// A single token produced by the Lexer.
// `text` holds the raw source text (e.g. "student", "8.7", "'Nikki'").
// Later phases (the Parser) will convert `text` into typed values as needed.
struct Token {
    TokenType type;
    std::string text;
    int line;

    Token(TokenType type, std::string text, int line)
        : type(type), text(std::move(text)), line(line) {}
};

// Human-readable name for a TokenType, useful for debugging and test failure
// messages (e.g. "expected IDENTIFIER, got KEYWORD_CREATE").
std::string tokenTypeToString(TokenType type);

}  // namespace minisql
