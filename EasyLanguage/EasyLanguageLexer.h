#pragma once
#include "../CompilerCommon.h"
#include <string_view>

class EasyLanguageLexer {
public:
    EasyLanguageLexer(std::string_view source);
    Token scanToken();

private:
    bool isAtEnd() const;
    char advance();
    char peek() const;
    char peekNext() const;
    bool match(char expected);
    void skipWhitespace();
    Token identifier();
    Token number();
    Token string(char quote_char);
    Token makeToken(TokenType type) const;
    Token errorToken(const char* message) const;

    std::string_view source_;
    size_t start_;
    size_t current_;
    int line_;
};