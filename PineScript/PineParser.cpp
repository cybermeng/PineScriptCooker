// --- START OF FILE PineParser.cpp ---

#include "PineParser.h"
#include <iostream>

PineParser::PineParser(const std::string& source) : lexer(source)
{
    advance();
}

std::vector<std::unique_ptr<Stmt>> PineParser::parse()
{
    std::vector<std::unique_ptr<Stmt>> statements;
    while (current.type != TokenType::END_OF_FILE) {
        statements.push_back(statement());
    }
    return statements;
}

void PineParser::advance() {
    previous = current;
    current = lexer.scanToken();
    while (current.type == TokenType::ERROR) {
        std::cerr << "Error on line " << current.line << ": " << current.lexeme << std::endl;
        hadError = true;
        current = lexer.scanToken();
    }
}

void PineParser::consume(TokenType type, const std::string& message) {
    if (current.type == type) {
        advance();
        return;
    }
    throw std::runtime_error("Line " + std::to_string(current.line) + ": " + message);
}

bool PineParser::match(TokenType type) {
    if (!check(type)) return false;
    advance();
    return true;
}

bool PineParser::check(TokenType type) {
    return current.type == type;
}

std::unique_ptr<Stmt> PineParser::statement() {
    if (match(TokenType::IF)) {
        return ifStatement();
    }

    if (isAssignableToken(current.type) && lexer.peekNextToken().type == TokenType::EQUAL) {
        return assignmentStatement();
    }

    return expressionStatement();
}

std::unique_ptr<Stmt> PineParser::ifStatement() {
    consume(TokenType::LEFT_PAREN, "Expect '(' after 'if'.");
    std::unique_ptr<Expr> condition = expression();
    consume(TokenType::RIGHT_PAREN, "Expect ')' after if condition.");

    std::unique_ptr<IfStmt> ifStmt = std::make_unique<IfStmt>(std::move(condition));

    consume(TokenType::LEFT_BRACE, "Expect '{' before then branch.");
    while (!check(TokenType::RIGHT_BRACE) && current.type != TokenType::END_OF_FILE) {
        ifStmt->thenBranch.push_back(statement());
    }
    consume(TokenType::RIGHT_BRACE, "Expect '}' after then branch.");

    if (match(TokenType::ELSE)) {
        consume(TokenType::LEFT_BRACE, "Expect '{' before else branch.");
        while (!check(TokenType::RIGHT_BRACE) && current.type != TokenType::END_OF_FILE) {
            ifStmt->elseBranch.push_back(statement());
        }
        consume(TokenType::RIGHT_BRACE, "Expect '}' after else branch.");
    }
    return ifStmt;
}

std::unique_ptr<Stmt> PineParser::assignmentStatement() {
    if (!isAssignableToken(current.type)) {
        throw std::runtime_error("Line " + std::to_string(current.line) + ": Invalid assignment target.");
    }
    Token name = current;
    advance();
    consume(TokenType::EQUAL, "Expect '=' after variable name in assignment.");
    std::unique_ptr<Expr> initializer = expression();
    return std::make_unique<AssignmentStmt>(name, std::move(initializer));
}

std::unique_ptr<Stmt> PineParser::expressionStatement() {
    std::unique_ptr<Expr> expr = expression();
    return std::make_unique<ExpressionStmt>(std::move(expr));
}

std::unique_ptr<Expr> PineParser::expression() {
    return comparison();
}

// --- 修改后的 comparison 函数 ---
// 它现在调用 term() 而不是 primary()
std::unique_ptr<Expr> PineParser::comparison() {
    std::unique_ptr<Expr> expr = term(); // <<< 修改点

    while (match(TokenType::GREATER) || match(TokenType::LESS) ||
           match(TokenType::GREATER_EQUAL) || match(TokenType::LESS_EQUAL) ||
           match(TokenType::EQUAL_EQUAL) || match(TokenType::BANG_EQUAL))
    {
        Token op = previous;
        std::unique_ptr<Expr> right = term(); // <<< 修改点
        expr = std::make_unique<BinaryExpr>(std::move(expr), op, std::move(right));
    }
    return expr;
}

// --- 新增的 term 函数 ---
// 处理加法和减法，调用 factor()
std::unique_ptr<Expr> PineParser::term() {
    std::unique_ptr<Expr> expr = factor();
    while (match(TokenType::PLUS) || match(TokenType::MINUS)) {
        Token op = previous;
        std::unique_ptr<Expr> right = factor();
        expr = std::make_unique<BinaryExpr>(std::move(expr), op, std::move(right));
    }
    return expr;
}

// --- 新增的 factor 函数 ---
// 处理乘法和除法，调用 primary()
std::unique_ptr<Expr> PineParser::factor() {
    std::unique_ptr<Expr> expr = primary();
    while (match(TokenType::STAR) || match(TokenType::SLASH)) {
        Token op = previous;
        std::unique_ptr<Expr> right = primary();
        expr = std::make_unique<BinaryExpr>(std::move(expr), op, std::move(right));
    }
    return expr;
}


// --- 辅助函数保持不变 ---

bool PineParser::isExpressionStartToken(TokenType type) {
    return type == TokenType::IDENTIFIER ||
           type == TokenType::INPUT ||
           type == TokenType::TRUE ||
           type == TokenType::FALSE ||
           type == TokenType::LEFT_PAREN ||
           type == TokenType::COLOR;
}

bool PineParser::isMemberNameToken(TokenType type) {
    return type == TokenType::IDENTIFIER ||
           type == TokenType::INT ||
           type == TokenType::FLOAT ||
           type == TokenType::BOOL ||
           type == TokenType::COLOR;
}

bool PineParser::isAssignableToken(TokenType type) {
    switch (type) {
        case TokenType::IDENTIFIER:
        case TokenType::INPUT:
        case TokenType::INT:
        case TokenType::FLOAT:
        case TokenType::BOOL:
        case TokenType::COLOR:
            return true;
        default:
            return false;
    }
}

// --- primary 和 finishCall 函数保持不变 ---

std::unique_ptr<Expr> PineParser::primary() {
    if (match(TokenType::NUMBER)) {
        return std::make_unique<LiteralExpr>(std::stod(previous.lexeme));
    }
    if (match(TokenType::STRING)) {
        std::string s = previous.lexeme;
        return std::make_unique<LiteralExpr>(s.substr(1, s.length() - 2));
    }

    if (isExpressionStartToken(current.type)) {
        if (current.type == TokenType::LEFT_PAREN) {
            advance();
            std::unique_ptr<Expr> expr = expression();
            consume(TokenType::RIGHT_PAREN, "Expect ')' after expression.");
            return expr;
        }

        advance();

        std::unique_ptr<Expr> expr = std::make_unique<VariableExpr>(previous);

        while (match(TokenType::LEFT_PAREN) || match(TokenType::DOT)) {
            if (previous.type == TokenType::LEFT_PAREN) {
                expr = finishCall(std::move(expr));
            } else if (previous.type == TokenType::DOT) {
                if (isMemberNameToken(current.type)) {
                    advance();
                } else {
                    throw std::runtime_error("Line " + std::to_string(current.line) + ": Expect property name after '.'.");
                }
                expr = std::make_unique<MemberAccessExpr>(std::move(expr), previous);
            }
        }
        return expr;
    }
    throw std::runtime_error("Line " + std::to_string(current.line) + ": Expect expression.");
}

std::unique_ptr<Expr> PineParser::finishCall(std::unique_ptr<Expr> callee) {
    auto call = std::make_unique<CallExpr>(std::move(callee));
    if (!check(TokenType::RIGHT_PAREN)) {
        do {
            call->arguments.push_back(expression());
        } while (match(TokenType::COMMA));
    }
    consume(TokenType::RIGHT_PAREN, "Expect ')' after arguments.");
    return call;
}