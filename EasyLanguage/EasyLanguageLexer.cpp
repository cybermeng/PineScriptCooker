#include "EasyLanguageLexer.h"
#include <cctype>
#include <string>
#include <string_view>
#include <algorithm>
#include <unordered_map>

static inline bool isIdentifierStart(char c) {
    unsigned char uc = static_cast<unsigned char>(c);
    return std::isalpha(uc) || uc == '_' || (uc & 0x80);
}

static inline bool isIdentifierChar(char c) {
    unsigned char uc = static_cast<unsigned char>(c);
    return std::isalnum(uc) || uc == '_' || (uc & 0x80);
}

// Forward declaration of the new helper function
static Token string(EasyLanguageLexer* lexer, char quote_char);


EasyLanguageLexer::EasyLanguageLexer(std::string_view source)
    : source_(source), start_(0), current_(0), line_(1) {}

// Helper function to create a generalized string token parser
Token EasyLanguageLexer::string(char quote_char) {
    while (peek() != quote_char && !isAtEnd()) {
        if (peek() == '\n') line_++;
        advance();
    }
    if (isAtEnd()) return errorToken("Unterminated string.");
    advance(); // The closing quote.
    return makeToken(TokenType::STRING);
}


Token EasyLanguageLexer::scanToken() {
    skipWhitespace();
    start_ = current_;

    if (isAtEnd()) return makeToken(TokenType::END_OF_FILE);

    char c = advance();

    if (isIdentifierStart(c)) return identifier();
    if (std::isdigit(static_cast<unsigned char>(c))) return number();

    switch (c) {
        case '(': return makeToken(TokenType::LEFT_PAREN);
        case ')': return makeToken(TokenType::RIGHT_PAREN);
        case '[': return makeToken(TokenType::LEFT_BRACKET);
        case ']': return makeToken(TokenType::RIGHT_BRACKET);
        case ';': return makeToken(TokenType::SEMICOLON);
        case ',': return makeToken(TokenType::COMMA);
        case '+': return makeToken(TokenType::PLUS);
        case '-': return makeToken(TokenType::MINUS);
        case '*': return makeToken(TokenType::STAR);
        case '/': return makeToken(TokenType::SLASH);
        case ':': return makeToken(TokenType::COLON);
        case '<':
            if (match('=')) return makeToken(TokenType::LESS_EQUAL);
            if (match('>')) return makeToken(TokenType::BANG_EQUAL); // <> for not equal
            return makeToken(TokenType::LESS);
        case '>':
            return makeToken(match('=') ? TokenType::GREATER_EQUAL : TokenType::GREATER);
        case '=': // EasyLanguage uses a single '=' for comparison
            return makeToken(TokenType::EQUAL);
        
        // --- FIX IS HERE ---
        // Handle both single and double quotes for strings
        case '\'': return string('\'');
        case '"': return string('"');
    }

    return errorToken("Unexpected character.");
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
                line_++;
                advance();
                break;
            case '{': // EasyLanguage-style block comment
                while (peek() != '}' && !isAtEnd()) {
                    if (peek() == '\n') line_++;
                    advance();
                }
                if (!isAtEnd()) advance(); // Skip '}'
                break;
            case '/':
                if (peekNext() == '/') { // C++-style line comment
                    while (peek() != '\n' && !isAtEnd()) {
                        advance();
                    }
                } else {
                    return;
                }
                break;
            default:
                return;
        }
    }
}

Token EasyLanguageLexer::identifier() {
    while (isIdentifierChar(peek())) advance();
    
    std::string_view text = source_.substr(start_, current_ - start_);
    std::string upper_text;
    upper_text.resize(text.length());
    std::transform(text.begin(), text.end(), upper_text.begin(), ::toupper);

    static const std::unordered_map<std::string, TokenType> keywords = {
        {"IF",        TokenType::IF},
        {"THEN",      TokenType::THEN},
        {"ELSE",      TokenType::ELSE},
        {"BEGIN",     TokenType::BEGIN},
        {"END",       TokenType::END},
        {"VARIABLES", TokenType::VARIABLES},
        {"VARS",      TokenType::VARIABLES}, // Common alias
        {"INPUTS",    TokenType::INPUTS},
        {"AND",       TokenType::AND},
        {"OR",        TokenType::OR},
        {"NOT",       TokenType::NOT},
        {"TRUE",      TokenType::TRUE},
        {"FALSE",     TokenType::FALSE}
    };

    auto it = keywords.find(upper_text);
    if (it != keywords.end()) {
        return makeToken(it->second);
    }

    return makeToken(TokenType::IDENTIFIER);
}

Token EasyLanguageLexer::number() {
    while (std::isdigit(static_cast<unsigned char>(peek()))) advance();
    if (peek() == '.' && std::isdigit(static_cast<unsigned char>(peekNext()))) {
        advance();
        while (std::isdigit(static_cast<unsigned char>(peek()))) advance();
    }
    return makeToken(TokenType::NUMBER);
}

// Note: The generalized string() method is now a member of the class above.
// This space is intentionally left blank.

bool EasyLanguageLexer::isAtEnd() const { return current_ >= source_.length(); }
char EasyLanguageLexer::advance() { return source_[current_++]; }
char EasyLanguageLexer::peek() const { if (isAtEnd()) return '\0'; return source_[current_]; }
char EasyLanguageLexer::peekNext() const { if (current_ + 1 >= source_.length()) return '\0'; return source_[current_ + 1]; }
bool EasyLanguageLexer::match(char expected) { if (isAtEnd() || source_[current_] != expected) return false; current_++; return true; }
Token EasyLanguageLexer::makeToken(TokenType type) const { return {type, std::string(source_.substr(start_, current_ - start_)), line_}; }
Token EasyLanguageLexer::errorToken(const char* message) const { return {TokenType::ERROR, std::string(message), line_}; }