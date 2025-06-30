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
    std::unique_ptr<HithinkStatement> assignmentOrExpressionStatement();

    std::unique_ptr<HithinkExpression> expression();
    // Hithink/通达信的逻辑运算通常通过函数(如AND,OR)或比较运算符组合实现
    // 因此我们的表达式解析直接从比较运算符的优先级开始
    std::unique_ptr<HithinkExpression> comparison();
    std::unique_ptr<HithinkExpression> term();
    std::unique_ptr<HithinkExpression> factor();
    std::unique_ptr<HithinkExpression> unary();
    std::unique_ptr<HithinkExpression> primary();
    
    std::unique_ptr<HithinkExpression> finishCall(Token calleeName);
};