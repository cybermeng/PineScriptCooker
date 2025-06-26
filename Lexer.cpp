#include "Lexer.h"
#include <cctype> // for isalpha, isdigit

Lexer::Lexer(const std::string& source) : source(source) {}

// (为简洁起见，这里省略 Lexer 的完整实现，它是一个标准的扫描器，
// 包含处理数字、字符串、标识符、操作符和跳过空白的逻辑)
// 核心函数是 scanToken()，它会返回下一个 Token。
Token Lexer::scanToken() {
    skipWhitespace();

    start = current;

    if (isAtEnd()) return makeToken(TokenType::END_OF_FILE);

    char c = advance();

    if (isalpha(c) || c == '_') return identifier();
    if (isdigit(c)) return number();

    switch (c) {
        case '(': return makeToken(TokenType::LEFT_PAREN);
        case ')': return makeToken(TokenType::RIGHT_PAREN);
        case '{': return makeToken(TokenType::LEFT_BRACE);
        case '}': return makeToken(TokenType::RIGHT_BRACE);
        case ',': return makeToken(TokenType::COMMA);
        case '.': return makeToken(TokenType::DOT);
        case '-': return makeToken(TokenType::MINUS);
        case '+': return makeToken(TokenType::PLUS);
        case '/': return makeToken(TokenType::SLASH);
        case '*': return makeToken(TokenType::STAR);
        case '>': return match('=') ? makeToken(TokenType::GREATER_EQUAL) : makeToken(TokenType::GREATER);
        case '<': return match('=') ? makeToken(TokenType::LESS_EQUAL) : makeToken(TokenType::LESS);
        case '=': return match('=') ? makeToken(TokenType::EQUAL_EQUAL) : makeToken(TokenType::EQUAL);
        case '!': return match('=') ? makeToken(TokenType::BANG_EQUAL) : errorToken("Expect '=' after '!'.");


        case '"': return string();
    }

    return errorToken("Unexpected character.");
}

bool Lexer::isAtEnd() {
    return current >= source.length();
}

char Lexer::advance() {
    return source[current++];
}

char Lexer::peek() {
    if (isAtEnd()) return '\0';
    return source[current];
}

bool Lexer::match(char expected) {
    if (isAtEnd()) return false;
    if (source[current] != expected) return false;
    current++;
    return true;
}

Token Lexer::makeToken(TokenType type) {
    return Token{type, source.substr(start, current - start), line};
}

Token Lexer::errorToken(const std::string& message) {
    return Token{TokenType::ERROR, message, line};
}

void Lexer::skipWhitespace() {
    for (;;) {
        char c = peek();
        switch (c) {
            case ' ':
            case '\r':
            case '\t':
                advance();
                break;
            case '\n':
                line++;
                advance();
                break;
            case '/':
                if (peek() == '/') {
                    // A // comment goes until the end of the line.
                    while (peek() != '\n' && !isAtEnd()) {
                        advance();
                    }
                    break; // Go back to the loop to skip the newline or EOF
                }
                // If it's not a // comment, fall through to default.
                break;
            default:
                return;
        }
    }
}

Token Lexer::string() {
    while (peek() != '"' && !isAtEnd()) {
        if (peek() == '\n') line++;
        advance();
    }

    if (isAtEnd()) return errorToken("Unterminated string.");

    // The closing quote.
    advance();
    return makeToken(TokenType::STRING);
}

Token Lexer::number() {
    while (isdigit(peek())) advance();

    // Look for a fractional part.
    if (peek() == '.' && isdigit(peekNext())) {
        // Consume the ".".
        advance();

        while (isdigit(peek())) advance();
    }

    return makeToken(TokenType::NUMBER);
}

char Lexer::peekNext() {
    if (current + 1 >= source.length()) return '\0';
    return source[current + 1];
}

Token Lexer::identifier() {
    while (isalnum(peek()) || peek() == '_') advance();
    return makeToken(identifierType());
}

TokenType Lexer::identifierType() {
    // For now, no keywords, all identifiers are just IDENTIFIER
    return TokenType::IDENTIFIER;
}

// Added implementation for peekNextToken
Token Lexer::peekNextToken() {
    int original_start = start;
    int original_current = current;
    int original_line = line;

    Token next_token = scanToken();

    start = original_start;
    current = original_current;
    line = original_line;
    return next_token;
}