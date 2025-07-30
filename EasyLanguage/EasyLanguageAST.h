#pragma once
#include "../CompilerCommon.h" // For Token, Value, etc.
#include <vector>
#include <memory>
#include <string>
#include <variant>

// Forward declarations for EasyLanguage AST nodes
struct ELAstNode;
struct ELStatement;
struct ELExpression;

struct ELInputDeclaration;
struct ELVariableDeclaration;
struct ELExpressionStatement; // Forward declaration for new AST node

struct ELIfStatement;
struct ELAssignmentStatement;
struct ELFunctionCallExpression;
struct ELLiteralExpression;
struct ELVariableExpression;
struct ELBinaryExpression;

// EasyLanguage AST Visitors
struct ELAstVisitor {
    virtual ~ELAstVisitor() = default;
    virtual void visit(ELInputDeclaration& node) = 0;
    virtual void visit(ELVariableDeclaration& node) = 0;
    virtual void visit(ELIfStatement& node) = 0;
    virtual void visit(ELAssignmentStatement& node) = 0;
    virtual void visit(ELFunctionCallExpression& node) = 0;
    virtual void visit(ELLiteralExpression& node) = 0;
    virtual void visit(ELVariableExpression& node) = 0;
    virtual void visit(ELExpressionStatement& node) = 0; // Add visit for ExpressionStatement
    virtual void visit(ELBinaryExpression& node) = 0;
    // Add other EL-specific statement/expression types as needed
};

// Base class for all EasyLanguage AST nodes
struct ELAstNode {
    virtual ~ELAstNode() = default; // 添加虚析构函数，确保多态性下正确释放内存
    virtual void accept(ELAstVisitor& visitor) = 0;
};

// Base class for EasyLanguage statements
struct ELStatement : ELAstNode {};

// Base class for EasyLanguage expressions
struct ELExpression : ELAstNode {};

// Specific EasyLanguage AST Nodes

// Declarations
struct ELInputDeclaration : ELStatement {
    Token name;
    std::unique_ptr<ELExpression> defaultValue; // Optional
    ELInputDeclaration(Token name, std::unique_ptr<ELExpression> defaultValue = nullptr)
        : name(name), defaultValue(std::move(defaultValue)) {}
    void accept(ELAstVisitor& visitor) override { visitor.visit(*this); }
};

struct ELVariableDeclaration : ELStatement {
    Token name;
    std::unique_ptr<ELExpression> initialValue; // Optional
    ELVariableDeclaration(Token name, std::unique_ptr<ELExpression> initialValue = nullptr)
        : name(name), initialValue(std::move(initialValue)) {}
    void accept(ELAstVisitor& visitor) override { visitor.visit(*this); }
};

struct ELIfStatement : ELStatement {
    std::unique_ptr<ELExpression> condition;
    std::vector<std::unique_ptr<ELStatement>> thenBranch;
    std::vector<std::unique_ptr<ELStatement>> elseBranch; // Optional

    ELIfStatement(std::unique_ptr<ELExpression> cond) : condition(std::move(cond)) {}
    void accept(ELAstVisitor& visitor) override { visitor.visit(*this); }
};

struct ELAssignmentStatement : ELStatement {
    Token name;
    std::unique_ptr<ELExpression> value;
    ELAssignmentStatement(Token name, std::unique_ptr<ELExpression> value)
        : name(name), value(std::move(value)) {}
    void accept(ELAstVisitor& visitor) override { visitor.visit(*this); }
};

struct ELExpressionStatement : ELStatement {
    std::unique_ptr<ELExpression> expression;
    ELExpressionStatement(std::unique_ptr<ELExpression> expr) : expression(std::move(expr)) {}
    void accept(ELAstVisitor& visitor) override { visitor.visit(*this); }
};


// Expressions
struct ELFunctionCallExpression : ELExpression {
    Token name;
    std::vector<std::unique_ptr<ELExpression>> arguments;
    ELFunctionCallExpression(Token name) : name(name) {}
    void accept(ELAstVisitor& visitor) override { visitor.visit(*this); }
};

struct ELLiteralExpression : ELExpression {
    Value value; // Reusing PineVM's Value variant
    ELLiteralExpression(Value val) : value(std::move(val)) {}
    void accept(ELAstVisitor& visitor) override { visitor.visit(*this); }
};

struct ELVariableExpression : ELExpression {
    Token name;
    ELVariableExpression(Token name) : name(name) {}
    void accept(ELAstVisitor& visitor) override { visitor.visit(*this); }
};

struct ELBinaryExpression : ELExpression {
    std::unique_ptr<ELExpression> left;
    Token op; // Operator token (e.g., PLUS, MINUS, EQUAL_EQUAL)
    std::unique_ptr<ELExpression> right;
    ELBinaryExpression(std::unique_ptr<ELExpression> left, Token op, std::unique_ptr<ELExpression> right)
        : left(std::move(left)), op(op), right(std::move(right)) {}
    void accept(ELAstVisitor& visitor) override { visitor.visit(*this); }
};