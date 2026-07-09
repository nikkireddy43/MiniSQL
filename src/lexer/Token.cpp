#include "lexer/Token.h"

namespace minisql {

std::string tokenTypeToString(TokenType type) {
    switch (type) {
        case TokenType::CREATE: return "CREATE";
        case TokenType::TABLE: return "TABLE";
        case TokenType::INSERT: return "INSERT";
        case TokenType::INTO: return "INTO";
        case TokenType::VALUES: return "VALUES";
        case TokenType::SELECT: return "SELECT";
        case TokenType::FROM: return "FROM";
        case TokenType::WHERE: return "WHERE";
        case TokenType::UPDATE: return "UPDATE";
        case TokenType::SET: return "SET";
        case TokenType::DELETE: return "DELETE";
        case TokenType::DROP: return "DROP";
        case TokenType::INDEX: return "INDEX";
        case TokenType::ON: return "ON";
        case TokenType::BEGIN_TXN: return "BEGIN";
        case TokenType::COMMIT_TXN: return "COMMIT";
        case TokenType::ROLLBACK_TXN: return "ROLLBACK";
        case TokenType::JOIN: return "JOIN";
        case TokenType::INNER: return "INNER";
        case TokenType::LEFT: return "LEFT";
        case TokenType::GROUP: return "GROUP";
        case TokenType::BY: return "BY";
        case TokenType::ORDER: return "ORDER";
        case TokenType::ASC: return "ASC";
        case TokenType::DESC: return "DESC";
        case TokenType::COUNT: return "COUNT";
        case TokenType::SUM: return "SUM";
        case TokenType::AVG: return "AVG";
        case TokenType::MIN: return "MIN";
        case TokenType::MAX: return "MAX";
        case TokenType::INT_TYPE: return "INT_TYPE";
        case TokenType::FLOAT_TYPE: return "FLOAT_TYPE";
        case TokenType::TEXT_TYPE: return "TEXT_TYPE";
        case TokenType::IDENTIFIER: return "IDENTIFIER";
        case TokenType::INT_LITERAL: return "INT_LITERAL";
        case TokenType::FLOAT_LITERAL: return "FLOAT_LITERAL";
        case TokenType::STRING_LITERAL: return "STRING_LITERAL";
        case TokenType::LPAREN: return "LPAREN";
        case TokenType::RPAREN: return "RPAREN";
        case TokenType::COMMA: return "COMMA";
        case TokenType::SEMICOLON: return "SEMICOLON";
        case TokenType::STAR: return "STAR";
        case TokenType::DOT: return "DOT";
        case TokenType::EQUAL: return "EQUAL";
        case TokenType::NOT_EQUAL: return "NOT_EQUAL";
        case TokenType::LESS: return "LESS";
        case TokenType::LESS_EQUAL: return "LESS_EQUAL";
        case TokenType::GREATER: return "GREATER";
        case TokenType::GREATER_EQUAL: return "GREATER_EQUAL";
        case TokenType::END_OF_FILE: return "END_OF_FILE";
        case TokenType::UNKNOWN: return "UNKNOWN";
    }
    return "INVALID_TOKEN_TYPE";
}

}  // namespace minisql
