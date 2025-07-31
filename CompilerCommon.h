#pragma once
#include <string>
#include <vector>
#include <variant>
#include <memory>
#include "VMCommon.h" // For Value, Series, OpCode, Bytecode

enum class TokenType {
    // Single-character tokens.
    LEFT_PAREN, RIGHT_PAREN, LEFT_BRACE, RIGHT_BRACE, COMMA, DOT, MINUS, PLUS, SLASH, STAR, GREATER, LESS,
    COLON, SEMICOLON, // For EasyLanguage

    LEFT_BRACKET, RIGHT_BRACKET,

    // One or two character tokens.
    EQUAL, EQUAL_EQUAL, BANG_EQUAL, GREATER_EQUAL, LESS_EQUAL, COLON_EQUAL, // Hithink `:=`

    // Literals.
    IDENTIFIER, NUMBER,

    // PineScript Keywords & Literals
    STRING, // PineScript string literal (also used by EasyLanguageLexer)
    IF, ELSE, AND, OR, NOT,
    INPUT, INT, FLOAT, BOOL, COLOR,
    TRUE, FALSE,

    // EasyLanguage / Hithink Keywords
    THEN, INPUTS, VARIABLES, BEGIN, END,

    SELECT, // Hithink select keyword

    // General
    ERROR, END_OF_FILE
};

struct Token {
    TokenType type;
    std::string lexeme; // 词元的原始文本
    int line;
};

// --- 抽象语法树 (AST) 节点 ---
// 我们需要为每种语法结构定义一个节点
// AST 表达式节点的前向声明，必须在 ExprVisitor 之前
struct BinaryExpr;
struct CallExpr;
struct LiteralExpr;
struct VariableExpr;
struct MemberAccessExpr;

// AST 语句节点的前向声明，必须在 AstVisitor 之前
struct AssignmentStmt;
struct IfStmt;
struct ExpressionStmt;

// 表达式基类 (前向声明)
struct Expr;
// 语句基类 (前向声明)
struct Stmt;

// 访问者模式，用于解耦 AST 遍历逻辑 (如代码生成)
struct AstVisitor {
    virtual ~AstVisitor() = default;
    virtual void visit(AssignmentStmt& stmt) = 0;
    virtual void visit(ExpressionStmt& stmt) = 0;
    virtual void visit(IfStmt& stmt) = 0;
};

// 表达式访问者
struct ExprVisitor {
    virtual ~ExprVisitor() = default;
    virtual void visit(BinaryExpr& expr) = 0;
    virtual void visit(CallExpr& expr) = 0;
    virtual void visit(LiteralExpr& expr) = 0;
    virtual void visit(VariableExpr& expr) = 0;
    virtual void visit(MemberAccessExpr& expr) = 0;
};


struct Expr {
    virtual ~Expr() = default;
    virtual void accept(ExprVisitor& visitor) = 0;
};


struct Stmt {
    virtual ~Stmt() = default;
    virtual void accept(AstVisitor& visitor) = 0;
};

// 表达式节点定义... (在解析器部分会更详细)