#pragma once

#include "HithinkLexer.h"
#include "HithinkAST.h"
#include <vector>
#include <memory>
#include <string_view>

class HithinkParser {
public:
    HithinkParser(std::string_view source);

    std::vector<std::unique_ptr<HithinkStatement>> parse();
    bool hadError() const;

private:
    HithinkLexer lexer_;
    Token current_;
    Token previous_;
    bool hadError_ = false;

    void advance();
    void consume(TokenType type, const char* message);
    bool match(TokenType type);
    bool check(TokenType type) const;

    void synchronize();
    void error(const Token& token, const char* message);

    // 语法规则对应的解析方法
    std::unique_ptr<HithinkStatement> statement();
    void consumeStatementTerminator();
    std::unique_ptr<HithinkExpression> expression();
    std::unique_ptr<HithinkExpression> logic_or();
    std::unique_ptr<HithinkExpression> logic_and();
    std::unique_ptr<HithinkExpression> comparison();
    std::unique_ptr<HithinkExpression> term();
    std::unique_ptr<HithinkExpression> factor();
    std::unique_ptr<HithinkExpression> unary();
    std::unique_ptr<HithinkExpression> subscript(); // 新增：处理下标操作
    std::unique_ptr<HithinkExpression> primary();
    
    std::unique_ptr<HithinkExpression> finishCall(Token calleeName);
};