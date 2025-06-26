#pragma once
#include "CompilerCommon.h"
#include "Lexer.h" // For Lexer class
#include <map>

// AST 表达式节点
struct BinaryExpr;
struct CallExpr;
struct LiteralExpr;
struct VariableExpr;
struct MemberAccessExpr;

struct ExprVisitor {
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

struct LiteralExpr : Expr {
    Value value;
    LiteralExpr(Value val) : value(std::move(val)) {}
    void accept(ExprVisitor& visitor) override { visitor.visit(*this); }
};

struct VariableExpr : Expr {
    Token name;
    VariableExpr(Token name) : name(name) {}
    void accept(ExprVisitor& visitor) override { visitor.visit(*this); }
};

struct MemberAccessExpr : Expr {
    std::unique_ptr<Expr> object;
    Token member;
    MemberAccessExpr(std::unique_ptr<Expr> obj, Token member) : object(std::move(obj)), member(member) {}
    void accept(ExprVisitor& visitor) override { visitor.visit(*this); }
};

struct CallExpr : Expr {
    std::unique_ptr<Expr> callee;
    std::vector<std::unique_ptr<Expr>> arguments;
    CallExpr(std::unique_ptr<Expr> callee) : callee(std::move(callee)) {}
    void accept(ExprVisitor& visitor) override { visitor.visit(*this); }
};

struct BinaryExpr : Expr {
    std::unique_ptr<Expr> left;
    Token op;
    std::unique_ptr<Expr> right;
    BinaryExpr(std::unique_ptr<Expr> left, Token op, std::unique_ptr<Expr> right)
        : left(std::move(left)), op(op), right(std::move(right)) {}
    void accept(ExprVisitor& visitor) override { visitor.visit(*this); }
};

// ... 其他表达式节点，如 BinaryExpr

// 语句基类
struct Stmt {
    virtual ~Stmt() = default;
    virtual void accept(AstVisitor& visitor) = 0;
};

// 表达式语句 (例如，单独一行的 plot(...)调用)
struct ExpressionStmt : Stmt {
    std::unique_ptr<Expr> expression;
    ExpressionStmt(std::unique_ptr<Expr> expr) : expression(std::move(expr)) {}
    void accept(AstVisitor& visitor) override { visitor.visit(*this); }
};

// 赋值语句
struct AssignmentStmt : Stmt {
    Token name;
    std::unique_ptr<Expr> initializer;
    AssignmentStmt(Token name, std::unique_ptr<Expr> init) : name(name), initializer(std::move(init)) {}
    void accept(AstVisitor& visitor) override { visitor.visit(*this); }
};

// 条件语句
struct IfStmt : Stmt {
    std::unique_ptr<Expr> condition;
    std::vector<std::unique_ptr<Stmt>> thenBranch;
    std::vector<std::unique_ptr<Stmt>> elseBranch; // Optional else branch

    IfStmt(std::unique_ptr<Expr> cond) : condition(std::move(cond)) {}
    void accept(AstVisitor& visitor) override { visitor.visit(*this); }
};

class Parser {
public:
    Parser(const std::string& source);
    std::vector<std::unique_ptr<Stmt>> parse();

private:
    Lexer lexer;
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
    std::unique_ptr<Expr> primary();
    std::unique_ptr<Expr> finishCall(std::unique_ptr<Expr> callee);
    std::unique_ptr<Expr> comparison();
    // ... 其他解析函数，如 term(), factor() 等
};