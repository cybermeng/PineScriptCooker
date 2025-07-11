#pragma once
#include "../CompilerCommon.h" // For Token, TokenType
#include <string_view>

class HithinkLexer {
public:
    HithinkLexer(std::string_view source);
    Token scanToken();
    Token peekNextToken();

private:
    // Private helper methods
    bool isAtEnd() const;
    char advance();
    char peek() const;
    char peekNext() const;
    bool match(char expected);
    void skipWhitespace();
    Token identifier();
    Token number();
    Token string();
    Token makeToken(TokenType type) const;
    Token errorToken(const char* message) const;

    // Member variables
    std::string_view source_;
    size_t start_;
    size_t current_;
    int line_;
};