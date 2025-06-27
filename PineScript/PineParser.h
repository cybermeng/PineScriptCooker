#pragma once
#include "../CompilerCommon.h"
#include "PineLexer.h" // For PineLexer class
#include "PineAST.h"   // For PineScript specific AST nodes
#include <map>

class PineParser {
public:
    PineParser(const std::string& source);
    std::vector<std::unique_ptr<Stmt>> parse();

private:
    PineLexer lexer;
    Token current;
    Token previous;
    bool hadError = false;

    void advance();
    void consume(TokenType type, const std::string& message);
    bool match(TokenType type);
    bool check(TokenType type);

    std::unique_ptr<Stmt> ifStatement();
    std::unique_ptr<Stmt> statement();
    std::unique_ptr<Stmt> assignmentStatement();
    std::unique_ptr<Stmt> expressionStatement();
    std::unique_ptr<Expr> expression();
    std::unique_ptr<Expr> primary();
    std::unique_ptr<Expr> finishCall(std::unique_ptr<Expr> callee);
    std::unique_ptr<Expr> comparison();
    bool isExpressionStartToken(TokenType type); // 新增：检查是否能作为表达式的起始词元
    bool isMemberNameToken(TokenType type);     // 新增：检查是否能作为成员访问的名称词元
    bool isAssignableToken(TokenType type);     // 新增：检查一个词元是否可以作为赋值的目标
    // ... 其他解析函数，如 term(), factor() 等
};