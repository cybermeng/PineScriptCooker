#include "HithinkParser.h"
#include "HithinkAST.h" 
#include <iostream>

HithinkParser::HithinkParser(std::string_view source) : lexer_(source), hadError_(false) {
    advance(); 
}

std::vector<std::unique_ptr<HithinkStatement>> HithinkParser::parse() {
    std::vector<std::unique_ptr<HithinkStatement>> statements;
    
    while (!check(TokenType::END_OF_FILE)) {
        auto stmt = statement();
        if (stmt) {
            statements.push_back(std::move(stmt));
        } else if (hadError_) {
            synchronize();
        }
    }
    return statements;
}

bool HithinkParser::hadError() const {
    return hadError_;
}

void HithinkParser::advance() {
    previous_ = current_;
    for (;;) {
        current_ = lexer_.scanToken();
        if (current_.type != TokenType::ERROR)
            break;
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

void HithinkParser::synchronize() {
    advance();
    while (!check(TokenType::END_OF_FILE)) {
        if (previous_.type == TokenType::SEMICOLON) return;
        switch (current_.type) {
            case TokenType::SELECT:
                return;
            default:
                break;
        }
        advance();
    }
}
void HithinkParser::consumeStatementTerminator() {
    if (match(TokenType::SEMICOLON)) {
        return;
    }
    if (check(TokenType::END_OF_FILE)) {
        return;
    }
    if (current_.line > previous_.line) {
        return;
    }
    error(current_, "Expect ';' or a newline after the statement.");
}

std::unique_ptr<HithinkStatement> HithinkParser::statement() {
    if (match(TokenType::SEMICOLON)) {
        return std::make_unique<HithinkEmptyStatement>();
    }
    
    if (match(TokenType::SELECT)) {
        Token selectKeyword = previous_;
        auto condition = expression();
        if (!condition) return nullptr;
        consumeStatementTerminator(); 

        Token nameToken = {TokenType::IDENTIFIER, "select", selectKeyword.line};
        return std::make_unique<HithinkAssignmentStatement>(nameToken, std::move(condition), true);
    }

    auto expr = expression();
    if (!expr) return nullptr;

    if (match(TokenType::COLON) || match(TokenType::COLON_EQUAL)) {
        Token assign_op = previous_;
        bool isOutput = assign_op.type == TokenType::COLON;

        if (auto* varExpr = dynamic_cast<HithinkVariableExpression*>(expr.get())) {
            Token name = varExpr->name;
            auto value = expression();
            if (!value) return nullptr;
            consumeStatementTerminator();

            return std::make_unique<HithinkAssignmentStatement>(name, std::move(value), isOutput);
        } else {
            error(assign_op, "Invalid assignment target.");
            return nullptr;
        }
    } else {
        consumeStatementTerminator();
        return std::make_unique<HithinkExpressionStatement>(std::move(expr));
    }
}

std::unique_ptr<HithinkExpression> HithinkParser::expression() {
    return logic_or();
}

std::unique_ptr<HithinkExpression> HithinkParser::logic_or() {
    auto expr = logic_and();
    if (!expr) return nullptr;
    while (match(TokenType::OR)) {
        Token op = previous_;
        auto right = logic_and();
        if (!right) return nullptr;
        expr = std::make_unique<HithinkBinaryExpression>(std::move(expr), op, std::move(right));
    }
    return expr;
}

std::unique_ptr<HithinkExpression> HithinkParser::logic_and() {
    auto expr = comparison();
    if (!expr) return nullptr;
    while (match(TokenType::AND)) {
        Token op = previous_;
        auto right = comparison();
        if (!right) return nullptr;
        expr = std::make_unique<HithinkBinaryExpression>(std::move(expr), op, std::move(right));
    }
    return expr;
}

std::unique_ptr<HithinkExpression> HithinkParser::comparison() {
    auto expr = term();
    if (!expr) return nullptr;
    while (match(TokenType::GREATER) || match(TokenType::GREATER_EQUAL) || match(TokenType::LESS) ||
           match(TokenType::LESS_EQUAL) || match(TokenType::EQUAL) || match(TokenType::BANG_EQUAL)) {
        Token op = previous_;
        auto right = term();
        if (!right) return nullptr;
        expr = std::make_unique<HithinkBinaryExpression>(std::move(expr), op, std::move(right));
    }
    return expr;
}

std::unique_ptr<HithinkExpression> HithinkParser::term() {
    auto expr = factor();
    if (!expr) return nullptr;
    while (match(TokenType::MINUS) || match(TokenType::PLUS)) {
        Token op = previous_;
        auto right = factor();
        if (!right) return nullptr;
        expr = std::make_unique<HithinkBinaryExpression>(std::move(expr), op, std::move(right));
    }
    return expr;
}

std::unique_ptr<HithinkExpression> HithinkParser::factor() {
    auto expr = unary();
    if (!expr) return nullptr;
    while (match(TokenType::SLASH) || match(TokenType::STAR)) {
        Token op = previous_;
        auto right = unary();
        if (!right) return nullptr;
        expr = std::make_unique<HithinkBinaryExpression>(std::move(expr), op, std::move(right));
    }
    return expr;
}

// 解析一元运算符 (-)
std::unique_ptr<HithinkExpression> HithinkParser::unary() {
    if (match(TokenType::MINUS)) {
        Token op = previous_;
        auto right = unary();
        if (!right) return nullptr;
        return std::make_unique<HithinkUnaryExpression>(op, std::move(right));
    }
    // 修改：调用 subscript() 而不是 primary()
    return subscript();
}

// 新增：解析下标运算符 ([])
// 它位于 unary 和 primary 之间，因为它比一元运算符优先级高。
std::unique_ptr<HithinkExpression> HithinkParser::subscript() {
    auto expr = primary(); // 首先解析一个 primary 表达式，例如一个变量名或函数调用
    if (!expr) return nullptr;

    // 循环处理可能的下标访问，支持链式访问如 `x[1][2]`
    while (match(TokenType::LEFT_BRACKET)) {
        Token bracket = previous_; // 保存 '[' 用于错误报告
        auto index = expression();
        if (!index) return nullptr;
        consume(TokenType::RIGHT_BRACKET, "Expect ']' after subscript index.");
        expr = std::make_unique<HithinkSubscriptExpression>(std::move(expr), std::move(index), bracket);
    }

    return expr;
}


// 解析最高优先级的表达式：字面量、变量、括号表达式、函数调用
std::unique_ptr<HithinkExpression> HithinkParser::primary() {
    if (match(TokenType::NUMBER)) {
        return std::make_unique<HithinkLiteralExpression>(std::stod(previous_.lexeme));
    }
    if (match(TokenType::STRING)) {
        std::string value = previous_.lexeme.substr(1, previous_.lexeme.length() - 2);
        return std::make_unique<HithinkLiteralExpression>(value);
    }
    if (match(TokenType::IDENTIFIER)) {
        Token calleeName = previous_;
        if (match(TokenType::LEFT_PAREN)) {
            return finishCall(calleeName);
        }
        return std::make_unique<HithinkVariableExpression>(calleeName);
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

std::unique_ptr<HithinkExpression> HithinkParser::finishCall(Token calleeName) {
    auto callExpr = std::make_unique<HithinkFunctionCallExpression>(calleeName);
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