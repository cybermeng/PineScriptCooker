#pragma once
#include "../CompilerCommon.h"
#include "EasyLanguageAST.h"
#include <vector>
#include <memory>
#include <string_view>
#include <unordered_map>

class EasyLanguageCompiler : public EasyLanguageAstVisitor {
public:
    EasyLanguageCompiler();
    Bytecode compile(std::string_view source);
    std::string compile_to_str(std::string_view source);
    bool hadError() const;

private:
    // Visitor methods
    void visit(DeclarationsStatement& stmt) override;
    void visit(AssignmentStatement& stmt) override;
    void visit(IfStatement& stmt) override;
    void visit(BlockStatement& stmt) override;
    void visit(ExpressionStatement& stmt) override;
    void visit(EmptyStatement& stmt) override;
    void visit(BinaryExpression& expr) override;
    void visit(UnaryExpression& expr) override;
    void visit(LiteralExpression& expr) override;
    void visit(VariableExpression& expr) override;
    void visit(FunctionCallExpression& expr) override;
    void visit(SubscriptExpression& expr) override;

    // Bytecode generation helpers
    void emitByte(OpCode op);
    void emitByteForMath(OpCode op);
    void emitByteWithOperand(OpCode op, int operand);
    int addConstant(const Value& value);
    void resolveAndEmitLoad(const Token& name);
    int resolveAndDefineVar(const std::string& varName);
    void emitStore(const Token& name);
    int emitJump(OpCode jumpType);
    void patchJump(int offset);

    // Compiler state
    Bytecode bytecode;
    std::unordered_map<std::string, int> globalVarSlots;
    int nextSlot = 0;
    bool hadError_ = false;
    
    static const std::unordered_map<std::string, std::string> builtin_mappings;
};