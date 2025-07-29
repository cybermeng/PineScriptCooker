// --- START OF FILE PineParser.h ---

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
    std::unique_ptr<Expr> comparison();
    // --- 新增函数声明 ---
    std::unique_ptr<Expr> term();
    std::unique_ptr<Expr> factor();
    // --- 结束新增 ---
    std::unique_ptr<Expr> primary();
    std::unique_ptr<Expr> finishCall(std::unique_ptr<Expr> callee);
    bool isExpressionStartToken(TokenType type);
    bool isMemberNameToken(TokenType type);
    bool isAssignableToken(TokenType type);
};