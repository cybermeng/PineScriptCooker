#include "EasyLanguageParser.h"
#include <iostream>

EasyLanguageParser::EasyLanguageParser(std::string_view source) : lexer_(source), hadError_(false) {
    advance();
}

std::vector<std::unique_ptr<EasyLanguageStatement>> EasyLanguageParser::parse() {
    std::vector<std::unique_ptr<EasyLanguageStatement>> statements;
    while (!check(TokenType::END_OF_FILE)) {
        std::unique_ptr<EasyLanguageStatement> stmt = nullptr;
        if (check(TokenType::VARIABLES) || check(TokenType::INPUTS)) {
            stmt = declaration();
        } else {
            stmt = statement();
        }
        
        if (stmt) {
            statements.push_back(std::move(stmt));
        } else if (hadError_) {
            synchronize();
        }
    }
    return statements;
}

// --- Top-Level Parsing Rules ---

std::unique_ptr<EasyLanguageStatement> EasyLanguageParser::declaration() {
    Token keyword = current_;
    advance(); // Consume `VARIABLES` or `INPUTS`
    consume(TokenType::COLON, "Expect ':' after 'Variables' or 'Inputs'.");

    auto declStmt = std::make_unique<DeclarationsStatement>(keyword);

    do {
        if (!check(TokenType::IDENTIFIER)) {
            error(current_, "Expect variable name.");
            return nullptr;
        }
        Token name = current_;
        advance();
        
        std::unique_ptr<EasyLanguageExpression> initializer = nullptr;
        if (match(TokenType::LEFT_PAREN)) {
            initializer = expression();
            consume(TokenType::RIGHT_PAREN, "Expect ')' after variable initializer.");
        }
        
        declStmt->declarations.push_back({name, std::move(initializer)});

    } while (match(TokenType::COMMA));

    consume(TokenType::SEMICOLON, "Expect ';' after declarations list.");
    return declStmt;
}

std::unique_ptr<EasyLanguageStatement> EasyLanguageParser::statement() {
    if (match(TokenType::IF)) return ifStatement();
    if (match(TokenType::BEGIN)) return blockStatement();
    if (match(TokenType::SEMICOLON)) return std::make_unique<EmptyStatement>();
    return assignmentOrExpressionStatement();
}

std::unique_ptr<EasyLanguageStatement> EasyLanguageParser::ifStatement() {
    auto condition = expression();
    if (!condition) return nullptr;
    consume(TokenType::THEN, "Expect 'Then' after if condition.");

    auto thenBranch = statement();
    if (!thenBranch) return nullptr;

    std::unique_ptr<EasyLanguageStatement> elseBranch = nullptr;
    if (match(TokenType::ELSE)) {
        elseBranch = statement();
        if (!elseBranch) return nullptr;
    }

    return std::make_unique<IfStatement>(std::move(condition), std::move(thenBranch), std::move(elseBranch));
}

std::unique_ptr<EasyLanguageStatement> EasyLanguageParser::blockStatement() {
    auto block = std::make_unique<BlockStatement>();
    while (!check(TokenType::END) && !check(TokenType::END_OF_FILE)) {
        auto stmt = statement();
        if(stmt) block->statements.push_back(std::move(stmt));
    }
    consume(TokenType::END, "Expect 'End' after block.");
    match(TokenType::SEMICOLON); // Optional semicolon after End
    return block;
}

std::unique_ptr<EasyLanguageStatement> EasyLanguageParser::assignmentOrExpressionStatement() {
    auto expr = expression();
    if (!expr) return nullptr;

    // Check for assignment. EasyLanguage assignment is `Var = ...`, but `=` is also comparison.
    // The trick is that only a variable can be on the left of an assignment.
    if (match(TokenType::EQUAL)) {
        if (auto* varExpr = dynamic_cast<VariableExpression*>(expr.get())) {
            Token name = varExpr->name;
            auto value = expression();
            if (!value) return nullptr;
            consume(TokenType::SEMICOLON, "Expect ';' after assignment statement.");
            return std::make_unique<AssignmentStatement>(name, std::move(value));
        } else {
            error(previous_, "Invalid assignment target. Left side of '=' must be a variable for assignment.");
            return nullptr;
        }
    }
    
    // If not an assignment, it must be an expression statement.
    consume(TokenType::SEMICOLON, "Expect ';' after expression statement.");
    return std::make_unique<ExpressionStatement>(std::move(expr));
}

// --- Expression Parsing (Precedence order) ---

std::unique_ptr<EasyLanguageExpression> EasyLanguageParser::expression() {
    return logic_or();
}

std::unique_ptr<EasyLanguageExpression> EasyLanguageParser::logic_or() {
    auto expr = logic_and();
    if (!expr) return nullptr;
    while (match(TokenType::OR)) {
        Token op = previous_;
        auto right = logic_and();
        if (!right) return nullptr;
        expr = std::make_unique<BinaryExpression>(std::move(expr), op, std::move(right));
    }
    return expr;
}

std::unique_ptr<EasyLanguageExpression> EasyLanguageParser::logic_and() {
    auto expr = equality();
    if (!expr) return nullptr;
    while (match(TokenType::AND)) {
        Token op = previous_;
        auto right = equality();
        if (!right) return nullptr;
        expr = std::make_unique<BinaryExpression>(std::move(expr), op, std::move(right));
    }
    return expr;
}

// NOTE: In EasyLanguage, '=' is both assignment and equality. We parse it here as equality.
// The `assignmentOrExpressionStatement` function handles the ambiguity.
std::unique_ptr<EasyLanguageExpression> EasyLanguageParser::equality() {
    auto expr = comparison();
    if (!expr) return nullptr;
    while (match(TokenType::EQUAL) || match(TokenType::BANG_EQUAL)) {
        Token op = previous_;
        auto right = comparison();
        if (!right) return nullptr;
        expr = std::make_unique<BinaryExpression>(std::move(expr), op, std::move(right));
    }
    return expr;
}

std::unique_ptr<EasyLanguageExpression> EasyLanguageParser::comparison() {
    auto expr = term();
    if (!expr) return nullptr;
    while (match(TokenType::GREATER) || match(TokenType::GREATER_EQUAL) || match(TokenType::LESS) || match(TokenType::LESS_EQUAL)) {
        Token op = previous_;
        auto right = term();
        if (!right) return nullptr;
        expr = std::make_unique<BinaryExpression>(std::move(expr), op, std::move(right));
    }
    return expr;
}

std::unique_ptr<EasyLanguageExpression> EasyLanguageParser::term() {
    auto expr = factor();
    if (!expr) return nullptr;
    while (match(TokenType::MINUS) || match(TokenType::PLUS)) {
        Token op = previous_;
        auto right = factor();
        if (!right) return nullptr;
        expr = std::make_unique<BinaryExpression>(std::move(expr), op, std::move(right));
    }
    return expr;
}

std::unique_ptr<EasyLanguageExpression> EasyLanguageParser::factor() {
    auto expr = unary();
    if (!expr) return nullptr;
    while (match(TokenType::SLASH) || match(TokenType::STAR)) {
        Token op = previous_;
        auto right = unary();
        if (!right) return nullptr;
        expr = std::make_unique<BinaryExpression>(std::move(expr), op, std::move(right));
    }
    return expr;
}

std::unique_ptr<EasyLanguageExpression> EasyLanguageParser::unary() {
    if (match(TokenType::MINUS) || match(TokenType::NOT)) {
        Token op = previous_;
        auto right = unary();
        if (!right) return nullptr;
        return std::make_unique<UnaryExpression>(op, std::move(right));
    }
    return subscript();
}

std::unique_ptr<EasyLanguageExpression> EasyLanguageParser::subscript() {
    auto expr = primary();
    if (!expr) return nullptr;
    while (match(TokenType::LEFT_BRACKET)) {
        Token bracket = previous_;
        auto index = expression();
        if (!index) return nullptr;
        consume(TokenType::RIGHT_BRACKET, "Expect ']' after subscript index.");
        expr = std::make_unique<SubscriptExpression>(std::move(expr), std::move(index), bracket);
    }
    return expr;
}

std::unique_ptr<EasyLanguageExpression> EasyLanguageParser::primary() {
    if (match(TokenType::NUMBER)) return std::make_unique<LiteralExpression>(std::stod(previous_.lexeme));
    if (match(TokenType::STRING)) return std::make_unique<LiteralExpression>(previous_.lexeme.substr(1, previous_.lexeme.length() - 2));
    if (match(TokenType::TRUE)) return std::make_unique<LiteralExpression>(true);
    if (match(TokenType::FALSE)) return std::make_unique<LiteralExpression>(false);
    
    if (match(TokenType::IDENTIFIER)) {
        Token calleeName = previous_;
        if (match(TokenType::LEFT_PAREN)) {
            return finishCall(calleeName);
        }
        return std::make_unique<VariableExpression>(calleeName);
    }

    if (match(TokenType::LEFT_PAREN)) {
        auto expr = expression();
        if (!expr) return nullptr;
        consume(TokenType::RIGHT_PAREN, "Expect ')' after expression.");
        return expr;
    }

    error(current_, "Expect expression.");
    return nullptr;
}

std::unique_ptr<EasyLanguageExpression> EasyLanguageParser::finishCall(Token calleeName) {
    auto callExpr = std::make_unique<FunctionCallExpression>(calleeName);
    if (!check(TokenType::RIGHT_PAREN)) {
        do {
            if (callExpr->arguments.size() >= 255) {
                error(current_, "Cannot have more than 255 arguments.");
            }
            auto arg = expression();
            if (!arg) return nullptr;
            callExpr->arguments.push_back(std::move(arg));
        } while (match(TokenType::COMMA));
    }
    consume(TokenType::RIGHT_PAREN, "Expect ')' after arguments.");
    return callExpr;
}

// --- Parser Helper Methods ---

bool EasyLanguageParser::hadError() const { return hadError_; }
void EasyLanguageParser::advance() { previous_ = current_; for (;;) { current_ = lexer_.scanToken(); if (current_.type != TokenType::ERROR) break; error(current_, current_.lexeme.c_str()); } }
void EasyLanguageParser::consume(TokenType type, const char* message) { if (check(type)) { advance(); return; } error(current_, message); }
bool EasyLanguageParser::match(TokenType type) { if (!check(type)) return false; advance(); return true; }
bool EasyLanguageParser::check(TokenType type) const { return current_.type == type; }
void EasyLanguageParser::error(const Token& token, const char* message) { if (hadError_) return; hadError_ = true; std::cerr << "[line " << token.line << "] Error"; if (token.type == TokenType::END_OF_FILE) std::cerr << " at end"; else if (token.type != TokenType::ERROR) std::cerr << " at '" << token.lexeme << "'"; std::cerr << ": " << message << std::endl; }
void EasyLanguageParser::synchronize() { advance(); while (!check(TokenType::END_OF_FILE)) { if (previous_.type == TokenType::SEMICOLON) return; switch (current_.type) { case TokenType::IF: case TokenType::BEGIN: case TokenType::VARIABLES: case TokenType::INPUTS: return; default: break; } advance(); } }