#pragma once

#include "HithinkLexer.h"
#include "HithinkAST.h"
#include <vector>
#include <memory>
#include <string_view>

class HithinkParser {
public:
    HithinkParser(std::string_view source);

    // 解析源代码并返回一个语句列表。
    // 如果解析过程中发生错误，返回的 vector 可能不完整，
    // 并且可以通过 hadError() 方法检查错误状态。
    std::vector<std::unique_ptr<HithinkStatement>> parse();

    bool hadError() const;

private:
    // 解析器状态
    HithinkLexer lexer_;
    Token current_;
    Token previous_;
    bool hadError_ = false;

    // 辅助方法
    void advance();
    void consume(TokenType type, const char* message);
    bool match(TokenType type);
    bool check(TokenType type) const;

    // 错误处理
    void synchronize();
    void error(const Token& token, const char* message);

    // 语法规则对应的解析方法
    std::unique_ptr<HithinkStatement> statement();
    void consumeStatementTerminator();
    std::unique_ptr<HithinkExpression> expression();
    // 新增：解析逻辑运算符 (OR, AND)
    std::unique_ptr<HithinkExpression> logic_or();
    std::unique_ptr<HithinkExpression> logic_and();
    // 解析比较运算符
    std::unique_ptr<HithinkExpression> comparison();
    std::unique_ptr<HithinkExpression> term();
    std::unique_ptr<HithinkExpression> factor();
    std::unique_ptr<HithinkExpression> unary();
    std::unique_ptr<HithinkExpression> primary();
    
    std::unique_ptr<HithinkExpression> finishCall(Token calleeName);
};