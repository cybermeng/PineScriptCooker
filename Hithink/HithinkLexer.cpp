#include "HithinkLexer.h"
#include <cctype>   // for std::isalpha, std::isdigit, std::isalnum, std::toupper
#include <string>   // for std::string in makeToken
#include <string_view> // for std::string_view
#include <algorithm> // for std::transform

// 检查一个字符是否可以作为标识符的开头
// 允许：ASCII字母, 下划线, 或任何多字节字符 (如中文)
static inline bool isIdentifierStart(char c) {
    unsigned char uc = static_cast<unsigned char>(c);
    return std::isalpha(uc) || uc == '_' || (uc & 0x80); // 检查最高位是否为1，表示非ASCII字符
}

// 检查一个字符是否可以作为标识符的组成部分
// 允许：ASCII字母/数字, 下划线, 或任何多字节字符
static inline bool isIdentifierChar(char c) {
    unsigned char uc = static_cast<unsigned char>(c);
    return std::isalnum(uc) || uc == '_' || (uc & 0x80); // 检查最高位是否为1，表示非ASCII字符
}

HithinkLexer::HithinkLexer(std::string_view source)
    : source_(source), start_(0), current_(0), line_(1) {}

Token HithinkLexer::scanToken() {
    skipWhitespace();
    start_ = current_;

    if (isAtEnd()) return makeToken(TokenType::END_OF_FILE);

    char c = advance();

    if (isIdentifierStart(c)) return identifier();
    if (std::isdigit(static_cast<unsigned char>(c))) return number();

    switch (c) {
        case '(': return makeToken(TokenType::LEFT_PAREN);
        case ')': return makeToken(TokenType::RIGHT_PAREN);
        case ';': return makeToken(TokenType::SEMICOLON);
        case ',': return makeToken(TokenType::COMMA);
        case '+': return makeToken(TokenType::PLUS);
        case '-': return makeToken(TokenType::MINUS);
        case '*': return makeToken(TokenType::STAR);
        case '/': return makeToken(TokenType::SLASH);
        case ':':
            return makeToken(match('=') ? TokenType::COLON_EQUAL : TokenType::COLON);
        case '<':
            if (match('=')) return makeToken(TokenType::LESS_EQUAL);
            if (match('>')) return makeToken(TokenType::BANG_EQUAL); // <>
            return makeToken(TokenType::LESS);
        case '>':
            return makeToken(match('=') ? TokenType::GREATER_EQUAL : TokenType::GREATER);
        case '=':
            // 支持 == 和 = 都作为相等判断符号。
            // match('=') 会在存在第二个'='时消耗它。
            // 无论如何，都返回EQUAL类型。makeToken会根据current_指针
            // 正确地生成 "=" 或 "==" 的文本。
            match('=');
            return makeToken(TokenType::EQUAL);
        case '\'': return string();
    }

    return errorToken("Unexpected character.");
}

bool HithinkLexer::isAtEnd() const {
    return current_ >= source_.length();
}

char HithinkLexer::advance() {
    return source_[current_++];
}

char HithinkLexer::peek() const {
    if (isAtEnd()) return '\0';
    return source_[current_];
}

char HithinkLexer::peekNext() const {
    if (current_ + 1 >= source_.length()) return '\0';
    return source_[current_ + 1];
}

bool HithinkLexer::match(char expected) {
    if (isAtEnd() || source_[current_] != expected) {
        return false;
    }
    current_++;
    return true;
}

void HithinkLexer::skipWhitespace() {
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
            case '{': // Hithink/TDX 注释
                while (peek() != '}' && !isAtEnd()) {
                    if (peek() == '\n') line_++;
                    advance();
                }
                if (!isAtEnd()) advance(); // Skip '}'
                break;
            case '/':
                // 检查是否为 "//" 注释
                if (peekNext() == '/') {
                    // 是注释，消耗掉这一行的所有字符
                    while (peek() != '\n' && !isAtEnd()) {
                        advance();
                    }
                    // break 以便外层循环可以处理换行符或文件结尾
                    break;
                } else {
                    // 是除法运算符，不是注释。停止跳过空白，让 scanToken() 来处理它。
                    return;
                }
           default:
                return;
        }
    }
}

Token HithinkLexer::identifier() {
    while (isIdentifierChar(peek())) advance();
    
    std::string_view text = source_.substr(start_, current_ - start_);
    
    // 为了不区分大小写地处理关键字
    std::string upper_text;
    upper_text.resize(text.length());
    std::transform(text.begin(), text.end(), upper_text.begin(),
                   [](unsigned char c){ return std::toupper(c); });

    if (upper_text == "SELECT") return makeToken(TokenType::SELECT);
    if (upper_text == "AND") return makeToken(TokenType::AND);
    if (upper_text == "OR") return makeToken(TokenType::OR);

    // Hithink的函数名和内置变量(如C, O, MA)都作为标识符处理，由Parser或Semantic Analyzer识别
    return makeToken(TokenType::IDENTIFIER);
}

Token HithinkLexer::number() {
    while (std::isdigit(static_cast<unsigned char>(peek()))) advance();

    if (peek() == '.' && std::isdigit(static_cast<unsigned char>(peekNext()))) {
        advance(); // Consume the "."
        while (std::isdigit(static_cast<unsigned char>(peek()))) advance();
    }

    return makeToken(TokenType::NUMBER);
}

Token HithinkLexer::string() {
    while (peek() != '\'' && !isAtEnd()) {
        if (peek() == '\n') line_++;
        advance();
    }
    if (isAtEnd()) return errorToken("Unterminated string.");
    advance(); // The closing quote.
    return makeToken(TokenType::STRING);
}

Token HithinkLexer::makeToken(TokenType type) const {
    return Token{type, std::string(source_.substr(start_, current_ - start_)), line_};
}

Token HithinkLexer::errorToken(const char* message) const {
    return Token{TokenType::ERROR, std::string(message), line_};
}

Token HithinkLexer::peekNextToken() {
    // 保存当前状态
    size_t original_start = start_;
    size_t original_current = current_;
    int original_line = line_;

    Token next_token = scanToken();

    // 恢复状态
    start_ = original_start;
    current_ = original_current;
    line_ = original_line;
    return next_token;
}