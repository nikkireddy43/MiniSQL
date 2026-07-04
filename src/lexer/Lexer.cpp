#include "lexer/Lexer.h"

#include <cctype>
#include <algorithm>
#include <stdexcept>

namespace minisql {

Lexer::Lexer(std::string source) : source_(std::move(source)) {
    keywords_ = {
        {"create", TokenType::CREATE},
        {"table", TokenType::TABLE},
        {"insert", TokenType::INSERT},
        {"into", TokenType::INTO},
        {"values", TokenType::VALUES},
        {"select", TokenType::SELECT},
        {"from", TokenType::FROM},
        {"where", TokenType::WHERE},
        {"update", TokenType::UPDATE},
        {"set", TokenType::SET},
        {"delete", TokenType::DELETE},
        {"drop", TokenType::DROP},
        {"int", TokenType::INT_TYPE},
        {"float", TokenType::FLOAT_TYPE},
        {"text", TokenType::TEXT_TYPE},
    };
}

bool Lexer::isAtEnd() const {
    return pos_ >= source_.size();
}

char Lexer::peek() const {
    if (isAtEnd()) return '\0';
    return source_[pos_];
}

char Lexer::peekNext() const {
    if (pos_ + 1 >= source_.size()) return '\0';
    return source_[pos_ + 1];
}

char Lexer::advance() {
    char c = source_[pos_++];
    if (c == '\n') line_++;
    return c;
}

bool Lexer::match(char expected) {
    if (isAtEnd() || source_[pos_] != expected) return false;
    pos_++;
    return true;
}

std::vector<Token> Lexer::tokenize() {
    std::vector<Token> tokens;

    while (!isAtEnd()) {
        char c = peek();

        // --- Whitespace: skip, don't emit a token ---
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
            advance();
            continue;
        }

        // --- Single-character symbols ---
        if (c == '(') { advance(); tokens.emplace_back(TokenType::LPAREN, "(", line_); continue; }
        if (c == ')') { advance(); tokens.emplace_back(TokenType::RPAREN, ")", line_); continue; }
        if (c == ',') { advance(); tokens.emplace_back(TokenType::COMMA, ",", line_); continue; }
        if (c == ';') { advance(); tokens.emplace_back(TokenType::SEMICOLON, ";", line_); continue; }
        if (c == '*') { advance(); tokens.emplace_back(TokenType::STAR, "*", line_); continue; }
        if (c == '=') { advance(); tokens.emplace_back(TokenType::EQUAL, "=", line_); continue; }

        // --- One-or-two-character symbols: need lookahead ---
        if (c == '<') {
            advance();
            if (match('=')) tokens.emplace_back(TokenType::LESS_EQUAL, "<=", line_);
            else tokens.emplace_back(TokenType::LESS, "<", line_);
            continue;
        }
        if (c == '>') {
            advance();
            if (match('=')) tokens.emplace_back(TokenType::GREATER_EQUAL, ">=", line_);
            else tokens.emplace_back(TokenType::GREATER, ">", line_);
            continue;
        }
        if (c == '!') {
            advance();
            if (match('=')) tokens.emplace_back(TokenType::NOT_EQUAL, "!=", line_);
            else tokens.emplace_back(TokenType::UNKNOWN, "!", line_);  // '!' alone isn't valid here
            continue;
        }

        // --- String literals: 'text' ---
        if (c == '\'') {
            advance();  // consume opening quote
            std::string value;
            while (!isAtEnd() && peek() != '\'') {
                value += advance();
            }
            if (isAtEnd()) {
                // Unterminated string — no closing quote found before EOF.
                tokens.emplace_back(TokenType::UNKNOWN, value, line_);
            } else {
                advance();  // consume closing quote
                tokens.emplace_back(TokenType::STRING_LITERAL, value, line_);
            }
            continue;
        }

        // --- Numbers: 42 or 8.7 ---
        if (std::isdigit(static_cast<unsigned char>(c))) {
            std::string value;
            while (!isAtEnd() && std::isdigit(static_cast<unsigned char>(peek()))) {
                value += advance();
            }
            bool isFloat = false;
            if (!isAtEnd() && peek() == '.' && std::isdigit(static_cast<unsigned char>(peekNext()))) {
                isFloat = true;
                value += advance();  // consume '.'
                while (!isAtEnd() && std::isdigit(static_cast<unsigned char>(peek()))) {
                    value += advance();
                }
            }
            tokens.emplace_back(isFloat ? TokenType::FLOAT_LITERAL : TokenType::INT_LITERAL, value, line_);
            continue;
        }

        // --- Identifiers and keywords: student, id, SELECT, int, ... ---
        if (std::isalpha(static_cast<unsigned char>(c)) || c == '_') {
            std::string word;
            while (!isAtEnd() && (std::isalnum(static_cast<unsigned char>(peek())) || peek() == '_')) {
                word += advance();
            }
            std::string lower = word;
            std::transform(lower.begin(), lower.end(), lower.begin(),
                           [](unsigned char ch) { return std::tolower(ch); });
            auto it = keywords_.find(lower);
            TokenType type = (it != keywords_.end()) ? it->second : TokenType::IDENTIFIER;
            tokens.emplace_back(type, word, line_);
            continue;
        }

        // --- Anything else we don't recognize ---
        tokens.emplace_back(TokenType::UNKNOWN, std::string(1, c), line_);
        advance();
    }

    tokens.emplace_back(TokenType::END_OF_FILE, "", line_);
    return tokens;
}

}  // namespace minisql