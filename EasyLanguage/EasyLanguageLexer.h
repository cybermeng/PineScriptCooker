#pragma once
#include "../CompilerCommon.h"
#include <string>
#include <vector>

class EasyLanguageLexer {
public:
    EasyLanguageLexer(const std::string& source);
    Token scanToken();
    Token peekNextToken();

private:
    std::string source;
    int start = 0;
    int current = 0;
    int line = 1;

    bool isAtEnd();
    char advance();
    char peek();
    bool match(char expected);
    Token makeToken(TokenType type);
    Token errorToken(const std::string& message);
    void skipWhitespace();
    Token string();
    Token number();
    Token identifier();
    TokenType identifierType(const std::string& text);
    char peekNext();
};