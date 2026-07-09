#include "parser/Parser.h"

namespace minisql {

Parser::Parser(std::vector<Token> tokens) : tokens_(std::move(tokens)) {}

const Token& Parser::peek() const {
    return tokens_[pos_];
}

const Token& Parser::previous() const {
    return tokens_[pos_ - 1];
}

bool Parser::isAtEnd() const {
    return peek().type == TokenType::END_OF_FILE;
}

bool Parser::check(TokenType type) const {
    if (isAtEnd()) return type == TokenType::END_OF_FILE;
    return peek().type == type;
}

const Token& Parser::advance() {
    if (!isAtEnd()) pos_++;
    return previous();
}

bool Parser::match(TokenType type) {
    if (!check(type)) return false;
    advance();
    return true;
}

const Token& Parser::consume(TokenType type, const std::string& message) {
    if (check(type)) return advance();
    throw ParseError(message, peek().line);
}

std::unique_ptr<Statement> Parser::parseStatement() {
    if (check(TokenType::CREATE)) {
        // CREATE alone is ambiguous - look one token ahead to tell
        // CREATE TABLE apart from CREATE INDEX.
        if (pos_ + 1 < tokens_.size() && tokens_[pos_ + 1].type == TokenType::INDEX) {
            return parseCreateIndex();
        }
        return parseCreateTable();
    }
    if (check(TokenType::INSERT)) return parseInsert();
    if (check(TokenType::SELECT)) return parseSelect();
    if (check(TokenType::UPDATE)) return parseUpdate();
    if (check(TokenType::DELETE)) return parseDelete();
    if (check(TokenType::DROP)) return parseDropTable();
    if (check(TokenType::BEGIN_TXN)) return parseBegin();
    if (check(TokenType::COMMIT_TXN)) return parseCommit();
    if (check(TokenType::ROLLBACK_TXN)) return parseRollback();

    throw ParseError(
        "Expected a statement (CREATE, INSERT, SELECT, UPDATE, DELETE, or DROP), "
        "got '" + peek().text + "'",
        peek().line);
}

std::unique_ptr<CreateTableStatement> Parser::parseCreateTable() {
    consume(TokenType::CREATE, "Expected 'CREATE'");
    consume(TokenType::TABLE, "Expected 'TABLE' after 'CREATE'");
    Token tableNameToken = consume(TokenType::IDENTIFIER, "Expected table name");
    consume(TokenType::LPAREN, "Expected '(' after table name");

    auto stmt = std::make_unique<CreateTableStatement>();
    stmt->tableName = tableNameToken.text;

    while (true) {
        stmt->columns.push_back(parseColumnDef());
        if (match(TokenType::COMMA)) {
            continue;
        }
        break;
    }

    consume(TokenType::RPAREN, "Expected ')' after column list");
    consume(TokenType::SEMICOLON, "Expected ';' at end of statement");

    return stmt;
}

std::unique_ptr<CreateIndexStatement> Parser::parseCreateIndex() {
    consume(TokenType::CREATE, "Expected 'CREATE'");
    consume(TokenType::INDEX, "Expected 'INDEX' after 'CREATE'");
    Token indexNameToken = consume(TokenType::IDENTIFIER, "Expected index name");
    consume(TokenType::ON, "Expected 'ON' after index name");
    Token tableNameToken = consume(TokenType::IDENTIFIER, "Expected table name");
    consume(TokenType::LPAREN, "Expected '(' after table name");
    Token columnNameToken = consume(TokenType::IDENTIFIER, "Expected column name");
    consume(TokenType::RPAREN, "Expected ')' after column name");
    consume(TokenType::SEMICOLON, "Expected ';' at end of statement");

    auto stmt = std::make_unique<CreateIndexStatement>();
    stmt->indexName = indexNameToken.text;
    stmt->tableName = tableNameToken.text;
    stmt->columnName = columnNameToken.text;
    return stmt;
}

std::unique_ptr<InsertStatement> Parser::parseInsert() {
    consume(TokenType::INSERT, "Expected 'INSERT'");
    consume(TokenType::INTO, "Expected 'INTO' after 'INSERT'");
    Token tableNameToken = consume(TokenType::IDENTIFIER, "Expected table name");
    consume(TokenType::VALUES, "Expected 'VALUES' after table name");
    consume(TokenType::LPAREN, "Expected '(' after 'VALUES'");

    auto stmt = std::make_unique<InsertStatement>();
    stmt->tableName = tableNameToken.text;

    while (true) {
        stmt->values.push_back(parseLiteral());
        if (match(TokenType::COMMA)) continue;
        break;
    }

    consume(TokenType::RPAREN, "Expected ')' after values list");
    consume(TokenType::SEMICOLON, "Expected ';' at end of statement");
    return stmt;
}

std::unique_ptr<SelectStatement> Parser::parseSelect() {
    consume(TokenType::SELECT, "Expected 'SELECT'");

    auto stmt = std::make_unique<SelectStatement>();

    if (match(TokenType::STAR)) {
        stmt->selectAll = true;
    } else {
        while (true) {
            SelectItem item = parseSelectItem();
            stmt->selectItems.push_back(item);
            if (item.aggFunc != AggregateFunc::NONE || item.isCountStar) {
                stmt->hasAggregates = true;
            } else if (item.tableName.empty()) {
                // Plain unqualified column - also populate the pre-v3
                // `columns` field so the original (non-join, non-aggregate)
                // execution path keeps working exactly as it did before.
                stmt->columns.push_back(item.columnName);
            }
            if (match(TokenType::COMMA)) continue;
            break;
        }
    }

    consume(TokenType::FROM, "Expected 'FROM'");
    Token tableNameToken = consume(TokenType::IDENTIFIER, "Expected table name");
    stmt->tableName = tableNameToken.text;

    // Optional JOIN clause: (INNER | LEFT)? JOIN table ON left.col = right.col
    if (check(TokenType::INNER) || check(TokenType::LEFT) || check(TokenType::JOIN)) {
        JoinClause join;
        if (match(TokenType::LEFT)) {
            join.isLeftJoin = true;
        } else {
            match(TokenType::INNER);  // optional, no-op either way
        }
        consume(TokenType::JOIN, "Expected 'JOIN'");
        Token rightTableToken = consume(TokenType::IDENTIFIER, "Expected joined table name");
        join.rightTable = rightTableToken.text;
        consume(TokenType::ON, "Expected 'ON' after joined table name");

        Token leftQualifier = consume(TokenType::IDENTIFIER, "Expected qualified column (table.column) in ON clause");
        consume(TokenType::DOT, "Expected '.' after table name in ON clause");
        Token leftCol = consume(TokenType::IDENTIFIER, "Expected column name after '.'");
        consume(TokenType::EQUAL, "Expected '=' in ON clause");
        Token rightQualifier = consume(TokenType::IDENTIFIER, "Expected qualified column (table.column) in ON clause");
        consume(TokenType::DOT, "Expected '.' after table name in ON clause");
        Token rightCol = consume(TokenType::IDENTIFIER, "Expected column name after '.'");

        join.leftTable = leftQualifier.text;
        join.leftColumn = leftCol.text;
        // rightQualifier is expected to name join.rightTable - not enforced
        // strictly here, the Executor resolves columns by table name anyway.
        (void)rightQualifier;
        join.rightColumn = rightCol.text;

        stmt->join = join;
    }

    if (match(TokenType::WHERE)) {
        stmt->whereClause = parseCondition();
    }

    if (match(TokenType::GROUP)) {
        consume(TokenType::BY, "Expected 'BY' after 'GROUP'");
        Token groupCol = consume(TokenType::IDENTIFIER, "Expected column name after 'GROUP BY'");
        stmt->groupByColumn = groupCol.text;
    }

    if (match(TokenType::ORDER)) {
        consume(TokenType::BY, "Expected 'BY' after 'ORDER'");
        Token orderCol = consume(TokenType::IDENTIFIER, "Expected column name after 'ORDER BY'");
        OrderByClause orderBy;
        orderBy.column = orderCol.text;
        if (match(TokenType::DESC)) {
            orderBy.descending = true;
        } else {
            match(TokenType::ASC);  // optional, default is ascending anyway
        }
        stmt->orderBy = orderBy;
    }

    consume(TokenType::SEMICOLON, "Expected ';' at end of statement");
    return stmt;
}

// One item in a SELECT list: an aggregate call (COUNT(*)/SUM(col)/etc.)
// or a plain column, optionally qualified with "table.".
SelectItem Parser::parseSelectItem() {
    SelectItem item;

    AggregateFunc func = AggregateFunc::NONE;
    if (match(TokenType::COUNT)) func = AggregateFunc::COUNT;
    else if (match(TokenType::SUM)) func = AggregateFunc::SUM;
    else if (match(TokenType::AVG)) func = AggregateFunc::AVG;
    else if (match(TokenType::MIN)) func = AggregateFunc::MIN;
    else if (match(TokenType::MAX)) func = AggregateFunc::MAX;

    if (func != AggregateFunc::NONE) {
        item.aggFunc = func;
        consume(TokenType::LPAREN, "Expected '(' after aggregate function name");
        if (match(TokenType::STAR)) {
            item.isCountStar = true;
        } else {
            Token colToken = consume(TokenType::IDENTIFIER, "Expected column name in aggregate call");
            item.columnName = colToken.text;
        }
        consume(TokenType::RPAREN, "Expected ')' after aggregate argument");
        return item;
    }

    // Plain column, optionally qualified: IDENTIFIER ('.' IDENTIFIER)?
    Token first = consume(TokenType::IDENTIFIER, "Expected column name");
    if (match(TokenType::DOT)) {
        Token second = consume(TokenType::IDENTIFIER, "Expected column name after '.'");
        item.tableName = first.text;
        item.columnName = second.text;
    } else {
        item.columnName = first.text;
    }
    return item;
}

std::unique_ptr<UpdateStatement> Parser::parseUpdate() {
    consume(TokenType::UPDATE, "Expected 'UPDATE'");
    Token tableNameToken = consume(TokenType::IDENTIFIER, "Expected table name");
    consume(TokenType::SET, "Expected 'SET'");
    Token colToken = consume(TokenType::IDENTIFIER, "Expected column name");
    consume(TokenType::EQUAL, "Expected '=' after column name");

    auto stmt = std::make_unique<UpdateStatement>();
    stmt->tableName = tableNameToken.text;
    stmt->setColumn = colToken.text;
    stmt->setValue = parseLiteral();

    if (match(TokenType::WHERE)) {
        stmt->whereClause = parseCondition();
    }

    consume(TokenType::SEMICOLON, "Expected ';' at end of statement");
    return stmt;
}

std::unique_ptr<DeleteStatement> Parser::parseDelete() {
    consume(TokenType::DELETE, "Expected 'DELETE'");
    consume(TokenType::FROM, "Expected 'FROM' after 'DELETE'");
    Token tableNameToken = consume(TokenType::IDENTIFIER, "Expected table name");

    auto stmt = std::make_unique<DeleteStatement>();
    stmt->tableName = tableNameToken.text;

    if (match(TokenType::WHERE)) {
        stmt->whereClause = parseCondition();
    }

    consume(TokenType::SEMICOLON, "Expected ';' at end of statement");
    return stmt;
}

std::unique_ptr<DropTableStatement> Parser::parseDropTable() {
    consume(TokenType::DROP, "Expected 'DROP'");
    consume(TokenType::TABLE, "Expected 'TABLE' after 'DROP'");
    Token tableNameToken = consume(TokenType::IDENTIFIER, "Expected table name");
    consume(TokenType::SEMICOLON, "Expected ';' at end of statement");

    auto stmt = std::make_unique<DropTableStatement>();
    stmt->tableName = tableNameToken.text;
    return stmt;
}

std::unique_ptr<BeginStatement> Parser::parseBegin() {
    consume(TokenType::BEGIN_TXN, "Expected 'BEGIN'");
    consume(TokenType::SEMICOLON, "Expected ';' after 'BEGIN'");
    return std::make_unique<BeginStatement>();
}

std::unique_ptr<CommitStatement> Parser::parseCommit() {
    consume(TokenType::COMMIT_TXN, "Expected 'COMMIT'");
    consume(TokenType::SEMICOLON, "Expected ';' after 'COMMIT'");
    return std::make_unique<CommitStatement>();
}

std::unique_ptr<RollbackStatement> Parser::parseRollback() {
    consume(TokenType::ROLLBACK_TXN, "Expected 'ROLLBACK'");
    consume(TokenType::SEMICOLON, "Expected ';' after 'ROLLBACK'");
    return std::make_unique<RollbackStatement>();
}

ColumnDefinition Parser::parseColumnDef() {
    Token nameToken = consume(TokenType::IDENTIFIER, "Expected column name");

    ColumnType type;
    if (match(TokenType::INT_TYPE)) {
        type = ColumnType::INT;
    } else if (match(TokenType::FLOAT_TYPE)) {
        type = ColumnType::FLOAT;
    } else if (match(TokenType::TEXT_TYPE)) {
        type = ColumnType::TEXT;
    } else {
        throw ParseError("Expected column type (INT, FLOAT, or TEXT)", peek().line);
    }

    return ColumnDefinition{nameToken.text, type};
}

Literal Parser::parseLiteral() {
    if (check(TokenType::INT_LITERAL)) {
        Token t = advance();
        return Literal{Literal::Kind::INT, t.text};
    }
    if (check(TokenType::FLOAT_LITERAL)) {
        Token t = advance();
        return Literal{Literal::Kind::FLOAT, t.text};
    }
    if (check(TokenType::STRING_LITERAL)) {
        Token t = advance();
        return Literal{Literal::Kind::STRING, t.text};
    }
    throw ParseError("Expected a literal value (number or string)", peek().line);
}

Condition Parser::parseCondition() {
    Token colToken = consume(TokenType::IDENTIFIER, "Expected column name in condition");
    std::string columnName = colToken.text;
    if (match(TokenType::DOT)) {
        Token colToken2 = consume(TokenType::IDENTIFIER, "Expected column name after '.'");
        columnName = colToken.text + "." + colToken2.text;
    }

    TokenType op;
    if (match(TokenType::EQUAL)) op = TokenType::EQUAL;
    else if (match(TokenType::NOT_EQUAL)) op = TokenType::NOT_EQUAL;
    else if (match(TokenType::LESS_EQUAL)) op = TokenType::LESS_EQUAL;
    else if (match(TokenType::LESS)) op = TokenType::LESS;
    else if (match(TokenType::GREATER_EQUAL)) op = TokenType::GREATER_EQUAL;
    else if (match(TokenType::GREATER)) op = TokenType::GREATER;
    else throw ParseError("Expected a comparison operator (=, !=, <, <=, >, >=)", peek().line);

    Literal value = parseLiteral();
    return Condition{columnName, op, value};
}

}  // namespace minisql
