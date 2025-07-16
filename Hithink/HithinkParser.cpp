#include "HithinkParser.h"
#include "HithinkAST.h" // 确保包含了AST节点的头文件，以便使用 dynamic_cast 和 HithinkVariableExpression
#include <iostream>

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

// 增强的同步函数
void HithinkParser::synchronize() {
    advance();
    while (!check(TokenType::END_OF_FILE)) {
        if (previous_.type == TokenType::SEMICOLON) return;

        // 同步到下一条可能语句的开头关键字
        switch (current_.type) {
            case TokenType::SELECT:
            // 在这里可以添加未来可能有的其他语句开头的关键字
                return;
            default:
                // Do nothing, just advance
                break;
        }

        advance();
    }
}

// =======================================================================
// vvvvvvvvvvvvvv         核心修改：重构的 statement() 函数         vvvvvvvvvvvvvv
// =======================================================================

// 移除了 assignmentOrExpressionStatement 函数，其逻辑被完全整合到这里。
// 移除了对 lexer_.peekNextToken() 的依赖。
std::unique_ptr<HithinkStatement> HithinkParser::statement() {
    // 首先处理 SELECT 语句，因为它由一个明确的关键字开头
    if (match(TokenType::SELECT)) {
        Token selectKeyword = previous_;
        auto condition = expression();
        if (!condition) {
            return nullptr; // expression() 已经报告了错误
        }
        consume(TokenType::SEMICOLON, "Expect ';' after select condition.");

        Token nameToken = {TokenType::IDENTIFIER, "select", selectKeyword.line};
        return std::make_unique<HithinkAssignmentStatement>(nameToken, std::move(condition), true);
    }

    // “先解析，后决策” 策略：
    // 1. 将语句的开头部分当作一个完整的表达式来解析。
    //    对于 "pbx1: ...", expression() 会解析 "pbx1" 并返回一个 VariableExpression。
    //    对于 "(a+b)/c;", expression() 会解析整个 "(a+b)/c" 表达式。
    auto expr = expression();
    if (!expr) {
        // 如果连一个表达式都解析不出来，说明有严重的语法错误，直接返回。
        return nullptr;
    }

    // 2. 检查紧跟在刚才解析的表达式后面的词元是什么。
    if (match(TokenType::COLON) || match(TokenType::COLON_EQUAL)) {
        // 如果是 ':' 或 ':=', 那么这一定是一个赋值语句。
        Token assign_op = previous_;
        bool isOutput = assign_op.type == TokenType::COLON;

        // 验证赋值目标（我们刚刚解析的 expr）是否合法。它必须是一个变量。
        if (auto* varExpr = dynamic_cast<HithinkVariableExpression*>(expr.get())) {
            // 类型转换成功，说明赋值目标合法。
            Token name = varExpr->name;

            // 现在解析赋值运算符右边的表达式。
            auto value = expression();
            if (!value) {
                return nullptr; // RHS 解析失败
            }
            
            consume(TokenType::SEMICOLON, "Expect ';' after assignment value.");
            return std::make_unique<HithinkAssignmentStatement>(name, std::move(value), isOutput);
        } else {
            // 类型转换失败，说明赋值目标不合法 (例如 " (a+b): 1; ")。
            error(assign_op, "Invalid assignment target.");
            return nullptr;
        }
    } else {
        // 如果表达式后面不是赋值运算符，那它就是一个表达式语句。
        consume(TokenType::SEMICOLON, "Expect ';' after expression statement.");
        return std::make_unique<HithinkExpressionStatement>(std::move(expr));
    }
}

std::unique_ptr<HithinkExpression> HithinkParser::expression() {
    // 表达式解析的唯一入口，从最低优先级的比较开始。
    return comparison();
}

// 解析比较运算符 (>, >=, <, <=, ==, !=)
std::unique_ptr<HithinkExpression> HithinkParser::comparison() {
    auto expr = term(); // 操作数是更高优先级的 term
    if (!expr) return nullptr;

    while (match(TokenType::GREATER) || match(TokenType::GREATER_EQUAL) || match(TokenType::LESS) ||
           match(TokenType::LESS_EQUAL) || match(TokenType::EQUAL) || match(TokenType::BANG_EQUAL)) {
        Token op = previous_;
        auto right = term(); // 右操作数也是 term
        if (!right) return nullptr;
        expr = std::make_unique<HithinkBinaryExpression>(std::move(expr), op, std::move(right));
    }
    return expr;
}

// 解析加法和减法 (+, -)
std::unique_ptr<HithinkExpression> HithinkParser::term() {
    auto expr = factor(); // 操作数是更高优先级的 factor
    if (!expr) return nullptr;

    while (match(TokenType::MINUS) || match(TokenType::PLUS)) {
        Token op = previous_;
        auto right = factor(); // 右操作数也是 factor
        if (!right) return nullptr;
        expr = std::make_unique<HithinkBinaryExpression>(std::move(expr), op, std::move(right));
    }
    return expr;
}

// 解析乘法和除法 (*, /)
std::unique_ptr<HithinkExpression> HithinkParser::factor() {
    auto expr = unary(); // 操作数是更高优先级的 unary
    if (!expr) return nullptr;

    while (match(TokenType::SLASH) || match(TokenType::STAR)) {
        Token op = previous_;
        auto right = unary(); // 右操作数也是 unary
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
    return primary();
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
        // 如果标识符后是 '(', 则是函数调用
        if (match(TokenType::LEFT_PAREN)) {
            return finishCall(calleeName);
        }
        // 否则是变量
        return std::make_unique<HithinkVariableExpression>(calleeName);
    }
    if (match(TokenType::LEFT_PAREN)) {
        // 对于括号内的表达式，我们从头开始递归解析，即再次调用 expression()
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
                // 不要继续解析，因为参数太多了
            }
            auto arg = expression();
            if (!arg) return nullptr; // 参数解析失败
            callExpr->arguments.push_back(std::move(arg));
        } while (match(TokenType::COMMA));
    }
    consume(TokenType::RIGHT_PAREN, "Expect ')' after arguments.");
    return callExpr;
}
