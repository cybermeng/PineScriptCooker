#pragma once
#include "../CompilerCommon.h" // 包含 Token, Value 等公共定义
#include <vector>
#include <unordered_map>
#include <memory>
#include <string>
#include <string_view>
#include <variant>

// 前向声明 Hithink (通达信) AST 节点
struct HithinkAstNode;
struct HithinkStatement;
struct HithinkExpression;
struct HithinkAstVisitor; // 前向声明访问者

struct HithinkAssignmentStatement;
struct HithinkExpressionStatement;

struct HithinkBinaryExpression;
struct HithinkFunctionCallExpression;
struct HithinkLiteralExpression;
struct HithinkVariableExpression;

// Hithink AST 访问者 (Visitor) 模式
// 为每种 AST 节点类型提供一个 visit 方法
struct HithinkAstVisitor {
    virtual ~HithinkAstVisitor() = default;
    virtual void visit(HithinkAssignmentStatement& node) = 0;
    virtual void visit(HithinkExpressionStatement& node) = 0;
    virtual void visit(HithinkBinaryExpression& node) = 0;
    virtual void visit(HithinkFunctionCallExpression& node) = 0;
    virtual void visit(HithinkLiteralExpression& node) = 0;
    virtual void visit(HithinkVariableExpression& node) = 0;
};

// 所有 Hithink AST 节点的基类
struct HithinkAstNode {
    virtual ~HithinkAstNode() = default;
    virtual void accept(HithinkAstVisitor& visitor) = 0;
};

// Hithink 语句的基类
struct HithinkStatement : HithinkAstNode {};

// Hithink 表达式的基类
struct HithinkExpression : HithinkAstNode {};

// --- 具体的语句节点 ---

// 赋值语句节点, 用于 `MA5: MA(C, 5);` 或 `V1 := C > O;`
struct HithinkAssignmentStatement : HithinkStatement {
    Token name; // 变量名 (例如 MA5, V1)
    std::unique_ptr<HithinkExpression> value; // 右侧的表达式
    bool isOutput; // 是否为输出语句 (true for ':', false for ':=')

    HithinkAssignmentStatement(Token name, std::unique_ptr<HithinkExpression> value, bool isOutput)
        : name(std::move(name)), value(std::move(value)), isOutput(isOutput) {}

    void accept(HithinkAstVisitor& visitor) override { visitor.visit(*this); }
};


// 表达式语句节点, 用于独立的函数调用, 如 `DRAWTEXT(...)`
struct HithinkExpressionStatement : HithinkStatement {
    std::unique_ptr<HithinkExpression> expression;

    HithinkExpressionStatement(std::unique_ptr<HithinkExpression> expr) : expression(std::move(expr)) {}

    void accept(HithinkAstVisitor& visitor) override { visitor.visit(*this); }
};

// --- 具体的表达式节点 ---

// 二元运算表达式节点, 如 `A + B`, `C > D`
struct HithinkBinaryExpression : HithinkExpression {
    std::unique_ptr<HithinkExpression> left;
    Token op; // 操作符
    std::unique_ptr<HithinkExpression> right;

    HithinkBinaryExpression(std::unique_ptr<HithinkExpression> left, Token op, std::unique_ptr<HithinkExpression> right)
        : left(std::move(left)), op(std::move(op)), right(std::move(right)) {}

    void accept(HithinkAstVisitor& visitor) override { visitor.visit(*this); }
};

// 函数调用表达式节点, 如 `MA(C, 5)`
struct HithinkFunctionCallExpression : HithinkExpression {
    Token name; // 函数名
    std::vector<std::unique_ptr<HithinkExpression>> arguments; // 参数列表

    HithinkFunctionCallExpression(Token name) : name(std::move(name)) {}

    void accept(HithinkAstVisitor& visitor) override { visitor.visit(*this); }
};

// 字面量表达式节点, 如数字 `10` 或字符串 `'Buy'`
struct HithinkLiteralExpression : HithinkExpression {
    Value value; // 使用 Value 变体来存储不同类型的值

    HithinkLiteralExpression(Value val) : value(std::move(val)) {}

    void accept(HithinkAstVisitor& visitor) override { visitor.visit(*this); }
};

// 变量表达式节点, 如 `CLOSE`, `MA5`
struct HithinkVariableExpression : HithinkExpression {
    Token name; // 变量名

    HithinkVariableExpression(Token name) : name(std::move(name)) {}

    void accept(HithinkAstVisitor& visitor) override { visitor.visit(*this); }
};