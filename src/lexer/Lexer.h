#pragma once

#include <string>
#include <vector>
#include <unordered_map>

#include "lexer/Token.h"

namespace minisql {

class Lexer {
public:
    explicit Lexer(std::string source);

    // THE CORE METHOD — this is the one you implement.
    //
    // Walks through `source_` from start to end and returns the full list
    // of tokens, ending with a single END_OF_FILE token.
    //
    // Suggested approach (a loop that runs until isAtEnd()):
    //   1. Skip whitespace.
    //   2. Look at the current character (peek()).
    //   3. Decide what kind of token starts here:
    //        - letter or '_'      -> consume a whole word, then check
    //                                 keywords_ to see if it's a keyword
    //                                 or a data type, otherwise IDENTIFIER
    //        - digit               -> consume a number; watch for a '.'
    //                                 to distinguish INT_LITERAL vs
    //                                 FLOAT_LITERAL
    //        - '\''                -> consume until the closing quote ->
    //                                 STRING_LITERAL
    //        - '(' ')' ',' ';' '*' -> single-character symbol tokens
    //        - '=' '!' '<' '>'     -> may be one or two characters
    //                                 (e.g. '<=' vs '<'), so peek ahead
    //                                 before deciding
    //        - anything else       -> UNKNOWN (don't crash the lexer on
    //                                 bad input; let the parser or caller
    //                                 report the error later)
    //   4. Append the Token you built to the result vector.
    //   5. Repeat until isAtEnd(), then push an END_OF_FILE token.
    std::vector<Token> tokenize();

private:
    std::string source_;
    size_t pos_ = 0;
    int line_ = 1;

    // Maps lowercase keyword/type text -> its TokenType, e.g.
    // "create" -> TokenType::CREATE, "int" -> TokenType::INT_TYPE.
    // Populated in the constructor. Use this to decide whether a
    // consumed word is a keyword/type or a plain IDENTIFIER.
    std::unordered_map<std::string, TokenType> keywords_;

    // --- Helpers already provided for you ---

    // True if pos_ has reached the end of source_.
    bool isAtEnd() const;

    // Returns the character at pos_ without consuming it.
    // Returns '\0' if isAtEnd().
    char peek() const;

    // Returns the character at pos_ + 1 without consuming it.
    // Useful for deciding between one- and two-character symbols like
    // '<' vs '<='. Returns '\0' if out of range.
    char peekNext() const;

    // Returns the character at pos_ and advances pos_ by one.
    // Updates line_ if the consumed character is '\n'.
    char advance();

    // If the current character matches `expected`, consumes it and
    // returns true. Otherwise leaves pos_ unchanged and returns false.
    // Handy for two-character operators: e.g. after consuming '<',
    // call match('=') to check for "<=".
    bool match(char expected);
};

}  // namespace minisql
