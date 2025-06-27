#pragma once
#include "../CompilerCommon.h" // For Expr, Stmt, Token, Value, AstVisitor, ExprVisitor
#include <vector>
#include <memory>
#include <string>

// Concrete PineScript Expression Nodes
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

// Concrete PineScript Statement Nodes
struct ExpressionStmt : Stmt {
    std::unique_ptr<Expr> expression;
    ExpressionStmt(std::unique_ptr<Expr> expr) : expression(std::move(expr)) {}
    void accept(AstVisitor& visitor) override { visitor.visit(*this); }
};

struct AssignmentStmt : Stmt {
    Token name;
    std::unique_ptr<Expr> initializer;
    AssignmentStmt(Token name, std::unique_ptr<Expr> init) : name(name), initializer(std::move(init)) {}
    void accept(AstVisitor& visitor) override { visitor.visit(*this); }
};

struct IfStmt : Stmt {
    std::unique_ptr<Expr> condition;
    std::vector<std::unique_ptr<Stmt>> thenBranch;
    std::vector<std::unique_ptr<Stmt>> elseBranch; // Optional else branch

    IfStmt(std::unique_ptr<Expr> cond) : condition(std::move(cond)) {}
    void accept(AstVisitor& visitor) override { visitor.visit(*this); }
};