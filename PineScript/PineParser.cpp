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

    // 赋值语句以一个可赋值的词元（l-value）开始，后跟一个'='。
    // 这包括普通标识符和一些可以作为变量名的关键字（如 'rsi', 'plot' 等）。
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

std::unique_ptr<Stmt> PineParser::assignmentStatement() {
    if (!isAssignableToken(current.type)) {
        // 如果从 statement() 调用，这里不应该被触发，但作为安全保障。
        throw std::runtime_error("Line " + std::to_string(current.line) + ": Invalid assignment target.");
    }
    Token name = current;
    advance(); // 消费变量名（例如 'rsi', 'ma_length' 等词元）。
    consume(TokenType::EQUAL, "Expect '=' after variable name in assignment.");
    std::unique_ptr<Expr> initializer = expression();
    return std::make_unique<AssignmentStmt>(name, std::move(initializer));
}

std::unique_ptr<Stmt> PineParser::expressionStatement() {
    std::unique_ptr<Expr> expr = expression();
    // PineScript 语句通常不需要分号，但为了简单起见，我们假设它们是单行语句
    // consume(TokenType::SEMICOLON, "Expect ';' after expression."); // 如果需要分号
    return std::make_unique<ExpressionStmt>(std::move(expr));
}

std::unique_ptr<Expr> PineParser::expression() {
    return comparison();
}

std::unique_ptr<Expr> PineParser::comparison() {
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

// 辅助函数：检查一个词元类型是否可以作为表达式的起始
bool PineParser::isExpressionStartToken(TokenType type) {
    return type == TokenType::IDENTIFIER ||
           type == TokenType::INPUT ||      // 'input' 是关键字，但可以开始表达式
           type == TokenType::TRUE ||       // 'true' 是关键字，但可以开始表达式
           type == TokenType::FALSE ||      // 'false' 是关键字，但可以开始表达式
           type == TokenType::CLOSE ||      // 'close' 是内置变量，可以开始表达式
           type == TokenType::SMA ||        // 'sma' 是内置函数名，可以开始表达式
           type == TokenType::RSI ||        // 'rsi' 是内置函数名，可以开始表达式
           type == TokenType::PLOT ||       // 'plot' 是内置函数名，可以开始表达式
           type == TokenType::PLOTSHAPE ||  // 'plotshape' 是内置函数名，可以开始表达式
           type == TokenType::LEFT_PAREN || // '(' 用于分组表达式，可以开始表达式
           type == TokenType::COLOR;       // 'color' 是关键字，但可以开始表达式 (如 color.red)
    // 根据 PineScript 语法，如果还有其他关键字可以作为表达式的起始，请在此处添加。
}

// 辅助函数：检查一个词元类型是否可以作为成员访问的名称（在 '.' 之后）
bool PineParser::isMemberNameToken(TokenType type) {
    return type == TokenType::IDENTIFIER ||
           type == TokenType::INT ||        // 例如：input.int
           type == TokenType::FLOAT ||      // 例如：input.float
           type == TokenType::BOOL ||       // 例如：input.bool
           type == TokenType::COLOR ||      // 例如：color.red
           type == TokenType::SMA ||        // 例如：ta.sma
           type == TokenType::RSI ||        // 例如：ta.rsi
           type == TokenType::CLOSE;        // 例如：some_object.close (虽然不常见，但语法上可能)
    // 根据 PineScript 语法，如果还有其他关键字可以作为成员名称，请在此处添加。
}

// 辅助函数：检查一个词元是否可以作为赋值的目标 (l-value)
bool PineParser::isAssignableToken(TokenType type) {
    // 在 PineScript 中，你可以对与内置函数同名的变量进行赋值。
    // 我们允许任何标识符，以及代表函数或类型的关键字。
    switch (type) {
        case TokenType::IDENTIFIER:
        case TokenType::INPUT:
        case TokenType::INT:
        case TokenType::FLOAT:
        case TokenType::BOOL:
        case TokenType::COLOR:
        case TokenType::PLOT:
        case TokenType::PLOTSHAPE:
        case TokenType::SMA:
        case TokenType::RSI:
            return true;
        default:
            return false;
    }
}

std::unique_ptr<Expr> PineParser::primary() {
    if (match(TokenType::NUMBER)) {
        return std::make_unique<LiteralExpr>(std::stod(previous.lexeme));
    }
    if (match(TokenType::STRING)) {
        std::string s = previous.lexeme;
        return std::make_unique<LiteralExpr>(s.substr(1, s.length() - 2));
    }

    // 显式处理 plot 作为函数调用
    if (match(TokenType::PLOT)) {
        std::unique_ptr<Expr> callee = std::make_unique<VariableExpr>(previous); // 'previous' 现在是 'plot' 词元
        consume(TokenType::LEFT_PAREN, "Expect '(' after 'plot'.");
        return finishCall(std::move(callee));
    }
    // 显式处理 plotshape 作为函数调用
    if (match(TokenType::PLOTSHAPE)) {
        std::unique_ptr<Expr> callee = std::make_unique<VariableExpr>(previous);
        consume(TokenType::LEFT_PAREN, "Expect '(' after 'plotshape'.");
        return finishCall(std::move(callee));
    }
    
    // 检查当前词元是否可以作为表达式的起始（包括标识符和某些关键字）
    if (isExpressionStartToken(current.type)) {
        if (current.type == TokenType::LEFT_PAREN) { // 处理分组表达式，如 (a + b)
            advance(); // 消费 '('
            std::unique_ptr<Expr> expr = expression(); // 递归解析括号内的表达式
            consume(TokenType::RIGHT_PAREN, "Expect ')' after expression.");
            return expr;
        }

        advance(); // 消费当前词元（例如：IDENTIFIER, INPUT, TRUE, CLOSE, PLOT 等）

        // 检查是否是函数调用或成员访问
        std::unique_ptr<Expr> expr = std::make_unique<VariableExpr>(previous);

        while (match(TokenType::LEFT_PAREN) || match(TokenType::DOT)) {

            if (previous.type == TokenType::LEFT_PAREN) {
                expr = finishCall(std::move(expr));
            } else if (previous.type == TokenType::DOT) {
                // 点号后的成员名称可以是标识符，也可以是某些关键字（如 'int'）
                if (isMemberNameToken(current.type)) {
                    advance(); // 消费成员名称词元（例如：IDENTIFIER, INT, COLOR, SMA 等）
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