#include "Parser.h"
#include <iostream>

// (Parser 的实现同样很长，它会递归地调用解析函数来构建树)
// 例如，`assignmentStatement` 的简化逻辑：
/*
std::unique_ptr<Stmt> Parser::statement() {
    if (// next token is an identifier followed by '=') {
        return assignmentStatement();
    }
    return expressionStatement();
}

std::unique_ptr<Stmt> Parser::assignmentStatement() {
    Token name = current; // consume IDENTIFIER
    consume(TokenType::EQUAL, "Expect '=' after variable name.");
    std::unique_ptr<Expr> initializer = expression();
    return std::make_unique<AssignmentStmt>(name, std::move(initializer));
}
*/
Parser::Parser(const std::string& source)
{
    lexer = Lexer(source);
    advance();
}

std::vector<std::unique_ptr<Stmt>> Parser::parse()
{
    std::vector<std::unique_ptr<Stmt>> statements;
    while (current.type != TokenType::END_OF_FILE) {
        statements.push_back(statement());
    }
    return statements;
}

void Parser::advance() {
    previous = current;
    current = lexer.scanToken();
    while (current.type == TokenType::ERROR) {
        std::cerr << "Error on line " << current.line << ": " << current.lexeme << std::endl;
        hadError = true;
        current = lexer.scanToken();
    }
}

void Parser::consume(TokenType type, const std::string& message) {
    if (current.type == type) {
        advance();
        return;
    }
    throw std::runtime_error("Line " + std::to_string(current.line) + ": " + message);
}

bool Parser::match(TokenType type) {
    if (!check(type)) return false;
    advance();
    return true;
}

bool Parser::check(TokenType type) {
    return current.type == type;
}

std::unique_ptr<Stmt> Parser::statement() {
    if (match(TokenType::IF)) {
        return ifStatement();
    }

    if (check(TokenType::IDENTIFIER)) {
        // Check if the identifier is followed by an '='
        if (lexer.peekNextToken().type == TokenType::EQUAL) {
            return assignmentStatement();
        }
    }
    return expressionStatement();
}

std::unique_ptr<Stmt> Parser::ifStatement() {
    consume(TokenType::LEFT_PAREN, "Expect '(' after 'if'.");
    std::unique_ptr<Expr> condition = expression();
    consume(TokenType::RIGHT_PAREN, "Expect ')' after if condition.");

    std::unique_ptr<IfStmt> ifStmt = std::make_unique<IfStmt>(std::move(condition));

    // Then branch
    consume(TokenType::LEFT_BRACE, "Expect '{' before then branch.");
    while (!check(TokenType::RIGHT_BRACE) && current.type != TokenType::END_OF_FILE) {
    
        ifStmt->thenBranch.push_back(statement());
    }
    consume(TokenType::RIGHT_BRACE, "Expect '}' after then branch.");

    // Else branch (optional)
    if (match(TokenType::ELSE)) {
        consume(TokenType::LEFT_BRACE, "Expect '{' before else branch.");
        while (!check(TokenType::RIGHT_BRACE) && current.type != TokenType::END_OF_FILE) {
            ifStmt->elseBranch.push_back(statement());
        }

        consume(TokenType::RIGHT_BRACE, "Expect '}' after else branch.");
    }
    return ifStmt;    
}

std::unique_ptr<Stmt> Parser::assignmentStatement() {
    Token name = current;
    consume(TokenType::IDENTIFIER, "Expect variable name.");
    consume(TokenType::EQUAL, "Expect '=' after variable name in assignment.");
    std::unique_ptr<Expr> initializer = expression();
    return std::make_unique<AssignmentStmt>(name, std::move(initializer));
}

std::unique_ptr<Stmt> Parser::expressionStatement() {
    std::unique_ptr<Expr> expr = expression();
    // PineScript 语句通常不需要分号，但为了简单起见，我们假设它们是单行语句
    // consume(TokenType::SEMICOLON, "Expect ';' after expression."); // 如果需要分号
    return std::make_unique<ExpressionStmt>(std::move(expr));
}

std::unique_ptr<Expr> Parser::expression() {
    return comparison();
}

std::unique_ptr<Expr> Parser::comparison() {
    std::unique_ptr<Expr> expr = primary(); // 简化了优先级，仅用于演示

    while (match(TokenType::GREATER) || match(TokenType::LESS) ||
           match(TokenType::GREATER_EQUAL) || match(TokenType::LESS_EQUAL) ||
           match(TokenType::EQUAL_EQUAL) || match(TokenType::BANG_EQUAL))
    {

        Token op = previous;
        std::unique_ptr<Expr> right = primary();
        expr = std::make_unique<BinaryExpr>(std::move(expr), op, std::move(right));
    }
    return expr;
}

std::unique_ptr<Expr> Parser::primary() {
    if (match(TokenType::NUMBER)) {

        return std::make_unique<LiteralExpr>(std::stod(previous.lexeme));
    }
    if (match(TokenType::STRING)) {
        std::string s = previous.lexeme;
        return std::make_unique<LiteralExpr>(s.substr(1, s.length() - 2));
    }
    if (match(TokenType::IDENTIFIER)) {
        // 检查是否是函数调用或成员访问
        std::unique_ptr<Expr> expr = std::make_unique<VariableExpr>(previous);
        while (match(TokenType::LEFT_PAREN) || match(TokenType::DOT)) {
            if (previous.type == TokenType::LEFT_PAREN) {
                expr = finishCall(std::move(expr));
            } else if (previous.type == TokenType::DOT) {
                consume(TokenType::IDENTIFIER, "Expect property name after '.'.");
                expr = std::make_unique<MemberAccessExpr>(std::move(expr), previous);
            }
        }
        return expr;
    }
    throw std::runtime_error("Line " + std::to_string(current.line) + ": Expect expression.");
}

std::unique_ptr<Expr> Parser::finishCall(std::unique_ptr<Expr> callee) {
    auto call = std::make_unique<CallExpr>(std::move(callee));
    if (!check(TokenType::RIGHT_PAREN)) {
        do {
            call->arguments.push_back(expression());
        } while (match(TokenType::COMMA));
    }
    consume(TokenType::RIGHT_PAREN, "Expect ')' after arguments.");
    return call;
}