#pragma once
#include "EasyLanguageLexer.h"
#include "EasyLanguageAST.h"
#include <vector>
#include <memory>
#include <string_view>

class EasyLanguageParser {
public:
    EasyLanguageParser(std::string_view source);
    std::vector<std::unique_ptr<EasyLanguageStatement>> parse();
    bool hadError() const;

private:
    // Parser state and helpers
    EasyLanguageLexer lexer_;
    Token current_;
    Token previous_;
    bool hadError_ = false;

    void advance();
    void consume(TokenType type, const char* message);
    bool match(TokenType type);
    bool check(TokenType type) const;
    void synchronize();
    void error(const Token& token, const char* message);

    // Grammar parsing methods
    std::unique_ptr<EasyLanguageStatement> declaration();
    std::unique_ptr<EasyLanguageStatement> statement();
    std::unique_ptr<EasyLanguageStatement> ifStatement();
    std::unique_ptr<EasyLanguageStatement> blockStatement();
    std::unique_ptr<EasyLanguageStatement> assignmentOrExpressionStatement();
    
    std::unique_ptr<EasyLanguageExpression> expression();
    std::unique_ptr<EasyLanguageExpression> logic_or();
    std::unique_ptr<EasyLanguageExpression> logic_and();
    std::unique_ptr<EasyLanguageExpression> equality();
    std::unique_ptr<EasyLanguageExpression> comparison();
    std::unique_ptr<EasyLanguageExpression> term();
    std::unique_ptr<EasyLanguageExpression> factor();
    std::unique_ptr<EasyLanguageExpression> unary();
    std::unique_ptr<EasyLanguageExpression> subscript();
    std::unique_ptr<EasyLanguageExpression> primary();
    std::unique_ptr<EasyLanguageExpression> finishCall(Token calleeName);
};