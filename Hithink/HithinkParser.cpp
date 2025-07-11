#include "HithinkParser.h"
#include <iostream> // 用于错误报告

HithinkParser::HithinkParser(std::string_view source) : lexer_(source), hadError_(false) {
    advance(); // 加载第一个词元
}

std::vector<std::unique_ptr<HithinkStatement>> HithinkParser::parse() {
    std::vector<std::unique_ptr<HithinkStatement>> statements;
    while (!check(TokenType::END_OF_FILE)) {
        auto stmt = statement();
        if (stmt) {
            statements.push_back(std::move(stmt));
        } else if (hadError_) {
            // 如果解析语句时出错，尝试同步到下一条语句的开头
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
    // 循环以跳过任何错误词元，并在过程中报告它们。
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
    // 避免在同步时报告级联错误
    if (hadError_)
        return;
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
        if (previous_.type == TokenType::SEMICOLON)
            return;
        // 可以根据语言特性在这里添加更多的同步点，例如关键字
        advance();
    }
}

std::unique_ptr<HithinkStatement> HithinkParser::statement() {
    // 新增：处理 'select' 语句
    if (match(TokenType::SELECT)) {
        Token selectKeyword = previous_; // 保存 'select' 关键字词元以获取行号
        auto condition = expression();
        if (!condition) {
            return nullptr; // expression() 中已报告错误
        }
        consume(TokenType::SEMICOLON, "Expect ';' after select condition.");

        // 将 'select' 语句转换为一个隐式的输出赋值语句
        // 变量名为 "select"，并且 isOutput 为 true
        Token nameToken = {TokenType::IDENTIFIER, "select", selectKeyword.line};
        return std::make_unique<HithinkAssignmentStatement>(nameToken, std::move(condition), true);
    }

    // Hithink 语法非常简单:
    // IDENTIFIER (':' | ':=') expression ';'
    // expression ';'
    // 为了解决歧义，我们向前查看一个词元。
    if (check(TokenType::IDENTIFIER) &&
        (lexer_.peekNextToken().type == TokenType::COLON || lexer_.peekNextToken().type == TokenType::COLON_EQUAL)) {
        Token name = current_;
        advance(); // 消耗标识符
        bool isOutput = match(TokenType::COLON);
        if (!isOutput) {
            consume(TokenType::COLON_EQUAL, "Expect ':=' for variable assignment.");
        }

        auto value = expression();
        if (!value) {
            return nullptr; // 错误已在 expression() 中报告
        }
        consume(TokenType::SEMICOLON, "Expect ';' after statement.");
        return std::make_unique<HithinkAssignmentStatement>(name, std::move(value), isOutput);
    }
    return assignmentOrExpressionStatement();
}

std::unique_ptr<HithinkStatement> HithinkParser::assignmentOrExpressionStatement() {
    auto expr = expression();
    if (!expr)
        return nullptr;
    consume(TokenType::SEMICOLON, "Expect ';' after expression.");
    return std::make_unique<HithinkExpressionStatement>(std::move(expr));
}

std::unique_ptr<HithinkExpression> HithinkParser::expression() {
    return comparison();
}

std::unique_ptr<HithinkExpression> HithinkParser::comparison() {
    auto expr = term();
    while (match(TokenType::GREATER) || match(TokenType::GREATER_EQUAL) || match(TokenType::LESS) ||
           match(TokenType::LESS_EQUAL) || match(TokenType::EQUAL) || match(TokenType::BANG_EQUAL)) {
        Token op = previous_;
        auto right = term();
        expr = std::make_unique<HithinkBinaryExpression>(std::move(expr), op, std::move(right));
    }
    return expr;
}

std::unique_ptr<HithinkExpression> HithinkParser::term() {
    auto expr = factor();
    while (match(TokenType::MINUS) || match(TokenType::PLUS)) {
        Token op = previous_;
        auto right = factor();
        expr = std::make_unique<HithinkBinaryExpression>(std::move(expr), op, std::move(right));
    }
    return expr;
}

std::unique_ptr<HithinkExpression> HithinkParser::factor() {
    auto expr = unary();
    while (match(TokenType::SLASH) || match(TokenType::STAR)) {
        Token op = previous_;
        auto right = unary();
        expr = std::make_unique<HithinkBinaryExpression>(std::move(expr), op, std::move(right));
    }
    return expr;
}

std::unique_ptr<HithinkExpression> HithinkParser::unary() {
    if (match(TokenType::MINUS)) {
        Token op = previous_;
        auto right = unary();
        return std::make_unique<HithinkUnaryExpression>(op, std::move(right));
    }
    return primary();
}

std::unique_ptr<HithinkExpression> HithinkParser::primary() {
    if (match(TokenType::NUMBER)) {
        // 使用 std::stod 将 string 转换为 double
        return std::make_unique<HithinkLiteralExpression>(std::stod(previous_.lexeme));
    }
    if (match(TokenType::STRING)) {
        // 移除字符串两边的引号
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
        consume(TokenType::RIGHT_PAREN, "Expect ')' after expression.");
        return expr; // Grouping expression, not creating a node for it.
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
            callExpr->arguments.push_back(expression());
        } while (match(TokenType::COMMA));
    }
    consume(TokenType::RIGHT_PAREN, "Expect ')' after arguments.");
    return callExpr;
}