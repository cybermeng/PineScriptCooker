#pragma once
#include "../CompilerCommon.h"
#include <vector>
#include <memory>
#include <string>
#include <variant>

// Forward declarations
struct EasyLanguageAstVisitor;
struct EasyLanguageStatement;
struct EasyLanguageExpression;

struct VariableDecl;
struct DeclarationsStatement;
struct AssignmentStatement;
struct IfStatement;
struct BlockStatement;
struct ExpressionStatement;
struct EmptyStatement;
struct BinaryExpression;
struct UnaryExpression;
struct LiteralExpression;
struct VariableExpression;
struct FunctionCallExpression;
struct SubscriptExpression;

// The Visitor for the EasyLanguage AST
struct EasyLanguageAstVisitor {
    virtual ~EasyLanguageAstVisitor() = default;
    virtual void visit(DeclarationsStatement& stmt) = 0;
    virtual void visit(AssignmentStatement& stmt) = 0;
    virtual void visit(IfStatement& stmt) = 0;
    virtual void visit(BlockStatement& stmt) = 0;
    virtual void visit(ExpressionStatement& stmt) = 0;
    virtual void visit(EmptyStatement& stmt) = 0;
    virtual void visit(BinaryExpression& expr) = 0;
    virtual void visit(UnaryExpression& expr) = 0;
    virtual void visit(LiteralExpression& expr) = 0;
    virtual void visit(VariableExpression& expr) = 0;
    virtual void visit(FunctionCallExpression& expr) = 0;
    virtual void visit(SubscriptExpression& expr) = 0;
};

// Base nodes
struct EasyLanguageAstNode {
    virtual ~EasyLanguageAstNode() = default;
    virtual void accept(EasyLanguageAstVisitor& visitor) = 0;
};

struct EasyLanguageStatement : EasyLanguageAstNode {};
struct EasyLanguageExpression : EasyLanguageAstNode {};

// --- Concrete Statement Nodes ---

// Represents a single variable declaration like `MyVar(10)`
struct VariableDecl {
    Token name;
    std::unique_ptr<EasyLanguageExpression> initializer;
};

// Represents a block of declarations like `Variables: Var1(0), Var2;`
struct DeclarationsStatement : EasyLanguageStatement {
    Token keyword; // `VARIABLES` or `INPUTS`
    std::vector<VariableDecl> declarations;

    DeclarationsStatement(Token keyword) : keyword(std::move(keyword)) {}

    void accept(EasyLanguageAstVisitor& visitor) override { visitor.visit(*this); }
};

// `MyVar = C + O;`
struct AssignmentStatement : EasyLanguageStatement {
    Token name;
    std::unique_ptr<EasyLanguageExpression> value;

    AssignmentStatement(Token name, std::unique_ptr<EasyLanguageExpression> value)
        : name(std::move(name)), value(std::move(value)) {}
    
    void accept(EasyLanguageAstVisitor& visitor) override { visitor.visit(*this); }
};

// `If C > O Then ... Else ...`
struct IfStatement : EasyLanguageStatement {
    std::unique_ptr<EasyLanguageExpression> condition;
    std::unique_ptr<EasyLanguageStatement> thenBranch;
    std::unique_ptr<EasyLanguageStatement> elseBranch; // Can be nullptr

    IfStatement(std::unique_ptr<EasyLanguageExpression> condition,
                std::unique_ptr<EasyLanguageStatement> thenBranch,
                std::unique_ptr<EasyLanguageStatement> elseBranch)
        : condition(std::move(condition)), thenBranch(std::move(thenBranch)), elseBranch(std::move(elseBranch)) {}
    
    void accept(EasyLanguageAstVisitor& visitor) override { visitor.visit(*this); }
};

// `Begin ... End`
struct BlockStatement : EasyLanguageStatement {
    std::vector<std::unique_ptr<EasyLanguageStatement>> statements;
    void accept(EasyLanguageAstVisitor& visitor) override { visitor.visit(*this); }
};

// A standalone expression, like `Plot(...);`
struct ExpressionStatement : EasyLanguageStatement {
    std::unique_ptr<EasyLanguageExpression> expression;

    ExpressionStatement(std::unique_ptr<EasyLanguageExpression> expr) : expression(std::move(expr)) {}

    void accept(EasyLanguageAstVisitor& visitor) override { visitor.visit(*this); }
};

// An empty statement, e.g., a lone semicolon
struct EmptyStatement : EasyLanguageStatement {
    void accept(EasyLanguageAstVisitor& visitor) override { visitor.visit(*this); }
};


// --- Concrete Expression Nodes ---

// `A + B`
struct BinaryExpression : EasyLanguageExpression {
    std::unique_ptr<EasyLanguageExpression> left;
    Token op;
    std::unique_ptr<EasyLanguageExpression> right;

    BinaryExpression(std::unique_ptr<EasyLanguageExpression> left, Token op, std::unique_ptr<EasyLanguageExpression> right)
        : left(std::move(left)), op(std::move(op)), right(std::move(right)) {}
    
    void accept(EasyLanguageAstVisitor& visitor) override { visitor.visit(*this); }
};

// `-A`
struct UnaryExpression : EasyLanguageExpression {
    Token op;
    std::unique_ptr<EasyLanguageExpression> right;

    UnaryExpression(Token op, std::unique_ptr<EasyLanguageExpression> right)
        : op(std::move(op)), right(std::move(right)) {}
    
    void accept(EasyLanguageAstVisitor& visitor) override { visitor.visit(*this); }
};

// `10.5` or `"hello"`
struct LiteralExpression : EasyLanguageExpression {
    Value value;

    LiteralExpression(Value val) : value(std::move(val)) {}
    
    void accept(EasyLanguageAstVisitor& visitor) override { visitor.visit(*this); }
};

// `Close` or `MyVar`
struct VariableExpression : EasyLanguageExpression {
    Token name;

    VariableExpression(Token name) : name(std::move(name)) {}
    
    void accept(EasyLanguageAstVisitor& visitor) override { visitor.visit(*this); }
};

// `MA(C, 5)`
struct FunctionCallExpression : EasyLanguageExpression {
    Token name;
    std::vector<std::unique_ptr<EasyLanguageExpression>> arguments;

    FunctionCallExpression(Token name) : name(std::move(name)) {}
    
    void accept(EasyLanguageAstVisitor& visitor) override { visitor.visit(*this); }
};

// `Close[1]`
struct SubscriptExpression : EasyLanguageExpression {
    std::unique_ptr<EasyLanguageExpression> callee;
    std::unique_ptr<EasyLanguageExpression> index;
    Token bracket;

    SubscriptExpression(std::unique_ptr<EasyLanguageExpression> callee, std::unique_ptr<EasyLanguageExpression> index, Token bracket)
        : callee(std::move(callee)), index(std::move(index)), bracket(std::move(bracket)) {}
    
    void accept(EasyLanguageAstVisitor& visitor) override { visitor.visit(*this); }
};