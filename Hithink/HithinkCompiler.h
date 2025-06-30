#pragma once
#include "../CompilerCommon.h" // For Bytecode, Value, OpCode
#include "HithinkAST.h"
#include <vector>
#include <memory>
#include <string_view>
#include <unordered_map>

// 前向声明以减少头文件依赖
// struct HithinkStatement; // 不再需要，因为已包含 HithinkAST.h

class HithinkCompiler : public HithinkAstVisitor {
public:
    HithinkCompiler();

    Bytecode compile(std::string_view source);

    bool hadError() const;

private:
    // HithinkAstVisitor 方法，用于遍历 AST
    void visit(HithinkAssignmentStatement& node) override;
    void visit(HithinkExpressionStatement& node) override;
    void visit(HithinkBinaryExpression& node) override;
    void visit(HithinkFunctionCallExpression& node) override;
    void visit(HithinkLiteralExpression& node) override;
    void visit(HithinkUnaryExpression& node) override;
    void visit(HithinkVariableExpression& node) override;

    // 字节码生成辅助函数
    void emitByte(OpCode op);
    void emitByteWithOperand(OpCode op, int operand);
    int addConstant(const Value& value);
    void resolveAndEmitLoad(const Token& name);
    void resolveAndEmitStore(const Token& name);
    int emitJump(OpCode jumpType);
    void patchJump(int offset);

    // 编译器状态
    Bytecode bytecode;
    std::unordered_map<std::string, int> globalVarSlots;
    int nextSlot = 0;
    bool hadError_ = false;

    // 从 Hithink 名称到 PineVM 内置名称的映射
    static const std::unordered_map<std::string, std::string> builtin_mappings;
};
