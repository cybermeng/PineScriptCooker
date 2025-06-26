#pragma once
#include <string>
#include <vector>
#include <variant>
#include <memory>
#include "PineVM.h" // 复用上一节定义的 OpCode, Value, Bytecode 等


// --- 词法分析部分 ---
enum class TokenType {
    // Single-character tokens.
    LEFT_PAREN, RIGHT_PAREN, LEFT_BRACE, RIGHT_BRACE, COMMA, DOT, MINUS, PLUS, SLASH, STAR, GREATER, LESS,

    // One or two character tokens.
    EQUAL, EQUAL_EQUAL, BANG_EQUAL, GREATER_EQUAL, LESS_EQUAL,

    // Literals.
    IDENTIFIER, STRING, NUMBER,

    // Keywords (simplified, none for now)

    IF, ELSE,

    ERROR, END_OF_FILE
};

struct Token {
    TokenType type;
    std::string lexeme; // 词元的原始文本
    int line;
};

// --- 抽象语法树 (AST) 节点 ---
// 我们需要为每种语法结构定义一个节点
struct Expr; // 表达式基类 (前向声明)

struct AssignmentStmt;
struct IfStmt;
struct ExpressionStmt;

// 访问者模式，用于解耦 AST 遍历逻辑 (如代码生成)
struct AstVisitor {
    virtual void visit(AssignmentStmt& stmt) = 0;
    virtual void visit(ExpressionStmt& stmt) = 0;
    virtual void visit(IfStmt& stmt) = 0;
};




// 表达式节点定义... (在解析器部分会更详细)