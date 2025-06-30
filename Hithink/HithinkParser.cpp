#include "HithinkParser.h"
#include <iostream> // 用于错误报告

HithinkParser::HithinkParser(std::string_view source)
    : lexer_(source), hadError_(false) {
    // 在 parse() 方法开始时调用 advance() 来加载第一个词元
}

std::vector<std::unique_ptr<HithinkStatement>> HithinkParser::parse() {
    // 这是一个占位符实现。
    // 一个真正的实现会循环调用 statement() 直到文件末尾。
    std::cout << "[HithinkParser] Placeholder parse() called. No statements will be generated." << std::endl;

    advance(); // 加载第一个词元
    while (!check(TokenType::END_OF_FILE)) {
        // 在一个真正的解析器中，我们会在这里调用 statement() 来解析一条语句。
        // 现在，为了避免无限循环，我们只消耗词元。
        // auto stmt = statement();
        // if (stmt) statements.push_back(std::move(stmt));
        advance();
    }

    return {}; //暂时返回一个空的语句列表
}

bool HithinkParser::hadError() const {
    return hadError_;
}

void HithinkParser::advance() {
    previous_ = current_;
    // 循环以跳过任何错误词元，并在过程中报告它们。
    for (;;) {
        current_ = lexer_.scanToken();
        if (current_.type != TokenType::ERROR) break;
        error(current_, current_.lexeme.c_str());
    }
}

void HithinkParser::consume(TokenType type, const char* message) {
    if (check(type)) {
        advance();
        return;
    }
    error(current_, message);
}

bool HithinkParser::match(TokenType type) {
    if (!check(type)) return false;
    advance();
    return true;
}

bool HithinkParser::check(TokenType type) const {
    return current_.type == type;
}

void HithinkParser::error(const Token& token, const char* message) {
    // 如果我们已经处于紧急模式，不要报告更多的错误。
    // 当我们添加 synchronize() 时，这将更有用。
    if (hadError_) return;
    hadError_ = true;
    std::cerr << "[line " << token.line << "] Error";
    if (token.type == TokenType::END_OF_FILE) {
        std::cerr << " at end";
    } else if (token.type != TokenType::ERROR) {
        std::cerr << " at '" << token.lexeme << "'";
    }
    std::cerr << ": " << message << std::endl;
}

// 其他私有方法的存根实现
void HithinkParser::synchronize() { /* TODO */ }
std::unique_ptr<HithinkStatement> HithinkParser::statement() { return nullptr; }
std::unique_ptr<HithinkStatement> HithinkParser::assignmentOrExpressionStatement() { return nullptr; }
std::unique_ptr<HithinkExpression> HithinkParser::expression() { return nullptr; }