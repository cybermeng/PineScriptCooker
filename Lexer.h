#pragma once
#include "CompilerCommon.h"

class Lexer {
public:
    Lexer(){}
    Lexer(const std::string& source);
    Token scanToken();
    Token peekNextToken(); // Added declaration

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
    TokenType identifierType();
    char peekNext();

};