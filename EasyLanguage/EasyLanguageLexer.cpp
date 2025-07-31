#include "EasyLanguageLexer.h"
#include <algorithm> // Required for std::transform
#include <cctype> // for isalpha, isdigit

EasyLanguageLexer::EasyLanguageLexer(const std::string& source) : source(source) {}

Token EasyLanguageLexer::scanToken() {
    skipWhitespace();

    start = current;

    if (isAtEnd()) return makeToken(TokenType::END_OF_FILE);

    char c = advance();

    if (isalpha(c) || c == '_') return identifier();
    if (isdigit(c)) return number();

    switch (c) {
        case '(': return makeToken(TokenType::LEFT_PAREN);
        case ')': return makeToken(TokenType::RIGHT_PAREN);
        case '{': // EasyLanguage uses { } for comments, but also for blocks in some contexts.
                  // For now, treat as comments. If used for blocks, parser will handle.
            // This is a simplified lexer. Real EL lexer would distinguish.
            // For now, let's assume it's a comment.
            while (peek() != '}' && !isAtEnd()) {
                if (peek() == '\n') line++;
                advance();
            }
            if (isAtEnd()) return errorToken("Unterminated comment block.");
            advance(); // Consume '}'
            return scanToken(); // Scan next token after comment
        case '}': return errorToken("Unexpected '}'."); // Should be consumed by comment handling
        case ',': return makeToken(TokenType::COMMA);
        case '.': return makeToken(TokenType::DOT);
        case '-': return makeToken(TokenType::MINUS);
        case '+': return makeToken(TokenType::PLUS);
        case '/': return makeToken(TokenType::SLASH);
        case '*': return makeToken(TokenType::STAR);
        case '>': return match('=') ? makeToken(TokenType::GREATER_EQUAL) : makeToken(TokenType::GREATER);
        case '<': return match('=') ? makeToken(TokenType::LESS_EQUAL) : makeToken(TokenType::LESS);
        case '=': return makeToken(TokenType::EQUAL); // EasyLanguage uses '=' for assignment
        case '!': return match('=') ? makeToken(TokenType::BANG_EQUAL) : errorToken("Expect '=' after '!'.");
        case ':': return makeToken(TokenType::COLON); // For Inputs: and Variables:
        case ';': return makeToken(TokenType::SEMICOLON); // For statement termination

        case '"': return string();
    }

    return errorToken("Unexpected character.");
}

bool EasyLanguageLexer::isAtEnd() {
    return current >= source.length();
}

char EasyLanguageLexer::advance() {
    return source[current++];
}

char EasyLanguageLexer::peek() {
    if (isAtEnd()) return '\0';
    return source[current];
}

bool EasyLanguageLexer::match(char expected) {
    if (isAtEnd()) return false;
    if (source[current] != expected) return false;
    current++;
    return true;
}

Token EasyLanguageLexer::makeToken(TokenType type) {
    return Token{type, source.substr(start, current - start), line};
}

Token EasyLanguageLexer::errorToken(const std::string& message) {
    return Token{TokenType::ERROR, message, line};
}

void EasyLanguageLexer::skipWhitespace() {
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

Token EasyLanguageLexer::string() {
    while (peek() != '"' && !isAtEnd()) {
        if (peek() == '\n') line++;
        advance();
    }

    if (isAtEnd()) return errorToken("Unterminated string.");

    // The closing quote.
    advance();
    return makeToken(TokenType::STRING);
}

Token EasyLanguageLexer::number() {
    while (isdigit(peek())) advance();

    // Look for a fractional part.
    if (peek() == '.' && isdigit(peekNext())) {
        // Consume the ".".
        advance();

        while (isdigit(peek())) advance();
    }

    return makeToken(TokenType::NUMBER);
}

char EasyLanguageLexer::peekNext() {
    if (current + 1 >= source.length()) return '\0';
    return source[current + 1];
}

Token EasyLanguageLexer::identifier() {
    while (isalnum(peek()) || peek() == '_') advance();
    std::string text = source.substr(start, current - start);
    return makeToken(identifierType(text));
}

TokenType EasyLanguageLexer::identifierType(const std::string& text) {
    // EasyLanguage keywords are typically case-insensitive.
    // Convert the text to lowercase for comparison.
    std::string lowerText = text;
    std::transform(lowerText.begin(), lowerText.end(), lowerText.begin(),
                   [](unsigned char c){ return std::tolower(c); });

    if (lowerText == "inputs") return TokenType::INPUTS;

    if (lowerText == "variables") return TokenType::VARIABLES;
    if (lowerText == "if") return TokenType::IF;
    if (lowerText == "then") return TokenType::THEN;
    if (lowerText == "else") return TokenType::ELSE;
    if (lowerText == "begin") return TokenType::BEGIN;
    if (lowerText == "end") return TokenType::END;
    return TokenType::IDENTIFIER;
}

Token EasyLanguageLexer::peekNextToken() {
    int original_start = start;
    int original_current = current;
    int original_line = line;

    Token next_token = scanToken();

    start = original_start;
    current = original_current;
    line = original_line;
    return next_token;
}