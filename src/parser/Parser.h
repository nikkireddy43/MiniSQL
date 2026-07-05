#pragma once

#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#include "lexer/Token.h"
#include "parser/AST.h"

namespace minisql {

// Thrown when the token stream doesn't match the expected grammar.
// Carries the line number so the CLI can print a useful location.
class ParseError : public std::runtime_error {
public:
    ParseError(const std::string& message, int line)
        : std::runtime_error("Line " + std::to_string(line) + ": " + message) {}
};

class Parser {
public:
    explicit Parser(std::vector<Token> tokens);

    // THE CORE METHOD you'll build out, one statement type at a time.
    //
    // Looks at the current token to decide which kind of statement this
    // is (CREATE -> createTableStmt, INSERT -> insertStmt, SELECT ->
    // selectStmt, UPDATE -> updateStmt, DELETE -> deleteStmt,
    // DROP -> dropTableStmt), then delegates to the matching parseX()
    // method below. Throw a ParseError if the leading token doesn't
    // match any known statement.
    std::unique_ptr<Statement> parseStatement();

    // --- One method per grammar rule (see AST.h for what each builds) ---
    //
    // Grammar reference:
    //   createTableStmt -> CREATE TABLE IDENTIFIER LPAREN columnDef
    //                       (COMMA columnDef)* RPAREN SEMICOLON
    //   columnDef       -> IDENTIFIER (INT_TYPE | FLOAT_TYPE | TEXT_TYPE)
    //   insertStmt      -> INSERT INTO IDENTIFIER VALUES LPAREN literal
    //                       (COMMA literal)* RPAREN SEMICOLON
    //   selectStmt      -> SELECT (STAR | IDENTIFIER (COMMA IDENTIFIER)*)
    //                       FROM IDENTIFIER (WHERE condition)? SEMICOLON
    //   updateStmt      -> UPDATE IDENTIFIER SET IDENTIFIER EQUAL literal
    //                       (WHERE condition)? SEMICOLON
    //   deleteStmt      -> DELETE FROM IDENTIFIER (WHERE condition)? SEMICOLON
    //   dropTableStmt   -> DROP TABLE IDENTIFIER SEMICOLON
    //   condition       -> IDENTIFIER comparisonOp literal
    //   literal         -> INT_LITERAL | FLOAT_LITERAL | STRING_LITERAL
    //
    // Suggested build order: dropTableStmt (shortest - good warm-up) ->
    // createTableStmt -> insertStmt -> deleteStmt -> selectStmt ->
    // updateStmt (longest, reuses condition + literal like the others).
    std::unique_ptr<CreateTableStatement> parseCreateTable();
    std::unique_ptr<CreateIndexStatement> parseCreateIndex();
    std::unique_ptr<InsertStatement> parseInsert();
    std::unique_ptr<SelectStatement> parseSelect();
    std::unique_ptr<UpdateStatement> parseUpdate();
    std::unique_ptr<DeleteStatement> parseDelete();
    std::unique_ptr<DropTableStatement> parseDropTable();

    // Shared helpers you'll call from the parseX() methods above:

    // columnDef -> IDENTIFIER (INT_TYPE | FLOAT_TYPE | TEXT_TYPE)
    ColumnDefinition parseColumnDef();

    // literal -> INT_LITERAL | FLOAT_LITERAL | STRING_LITERAL
    Literal parseLiteral();

    // condition -> IDENTIFIER comparisonOp literal
    // Call this AFTER consuming the WHERE keyword.
    Condition parseCondition();

private:
    std::vector<Token> tokens_;
    size_t pos_ = 0;

    // --- Token-level helpers (already implemented for you) ---
    // These are the token-stream equivalent of the Lexer's character-level
    // peek()/advance()/match() - same shape, one level up.

    // Returns the current token without consuming it.
    const Token& peek() const;

    // Returns the most recently consumed token.
    const Token& previous() const;

    // True if the current token is END_OF_FILE.
    bool isAtEnd() const;

    // True if the current token's type equals `type` (does not consume).
    bool check(TokenType type) const;

    // Consumes and returns the current token, advancing pos_.
    const Token& advance();

    // If the current token's type is `type`, consumes it and returns true.
    // Otherwise leaves pos_ unchanged and returns false.
    bool match(TokenType type);

    // If the current token's type is `type`, consumes and returns it.
    // Otherwise throws ParseError with `message`. Use this for tokens
    // that MUST be present per the grammar (e.g. the SEMICOLON at the
    // end of every statement).
    const Token& consume(TokenType type, const std::string& message);
};

}  // namespace minisql
