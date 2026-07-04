#include "lexer/Lexer.h"

#include <cctype>
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

// ---------- Helpers (already implemented for you) ----------

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

// ---------- Core logic (YOUR TURN) ----------
//
// See the detailed walkthrough in Lexer.h above tokenize()'s declaration.
// Delete the throw below once you start implementing, and build the
// result vector incrementally — it's fine (encouraged, even) to get this
// working for one token category at a time and re-run the tests as you go.
//
// A useful pattern for the keyword-vs-identifier step:
//
//   std::string word = ...; // the consumed letters/digits/underscores
//   std::string lower = word;
//   std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
//   auto it = keywords_.find(lower);
//   TokenType type = (it != keywords_.end()) ? it->second : TokenType::IDENTIFIER;

std::vector<Token> Lexer::tokenize() {
    throw std::logic_error(
        "Lexer::tokenize() is not implemented yet. "
        "See the guidance comment in Lexer.h.");
}

}  // namespace minisql
