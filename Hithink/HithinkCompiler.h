#pragma once
#include "../CompilerCommon.h" 
#include "HithinkAST.h"
#include <vector>
#include <memory>
#include <string_view>
#include <unordered_map>

class HithinkCompiler : public HithinkAstVisitor {
public:
    HithinkCompiler();

    Bytecode compile(std::string_view source);
    std::string compile_to_str(std::string_view source);

    bool hadError() const;

private:
    // HithinkAstVisitor 方法
    void visit(HithinkEmptyStatement& stmt) override;
    void visit(HithinkAssignmentStatement& node) override;
    void visit(HithinkExpressionStatement& node) override;
    void visit(HithinkBinaryExpression& node) override;
    void visit(HithinkFunctionCallExpression& node) override;
    void visit(HithinkLiteralExpression& node) override;
    void visit(HithinkUnaryExpression& node) override;
    void visit(HithinkVariableExpression& node) override;
    void visit(HithinkSubscriptExpression& node) override; // 新增

    // 字节码生成辅助函数
    void emitByte(OpCode op);
    void emitByteForMath(OpCode op);
    void emitByteWithOperand(OpCode op, int operand);
    int addConstant(const Value& value);
    void resolveAndEmitLoad(const Token& name);
    void resolveAndEmitStore(const Token& name);
    void resolveAndEmitStoreGlobal(const Token& name);
    int emitJump(OpCode jumpType);
    void patchJump(int offset);

    // 编译器状态
    Bytecode bytecode;
    std::unordered_map<std::string, int> globalVarSlots;
    int nextSlot = 0;
    bool hadError_ = false;

    static const std::unordered_map<std::string, std::string> builtin_mappings;
};