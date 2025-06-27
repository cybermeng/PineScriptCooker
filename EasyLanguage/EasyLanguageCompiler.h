#pragma once
#include "EasyLanguageAST.h"
#include "../PineVM.h" // For Bytecode, OpCode, Value
#include <map>
#include <string>
#include <vector>

class EasyLanguageCompiler : public ELAstVisitor {
public:
    EasyLanguageCompiler();
    Bytecode compile(const std::vector<std::unique_ptr<ELStatement>>& statements);

private:
    // ELAstVisitor methods
    void visit(ELInputDeclaration& node) override;
    void visit(ELVariableDeclaration& node) override; // Keep this
    void visit(ELIfStatement& node) override;
    void visit(ELAssignmentStatement& node) override;
    void visit(ELFunctionCallExpression& node) override;
    void visit(ELLiteralExpression& node) override;
    void visit(ELExpressionStatement& node) override; // New: for expression statements
    void visit(ELPlotStatement& node) override; // Implement this pure virtual function
    void visit(ELVariableExpression& node) override;
    void visit(ELBinaryExpression& node) override;

    void emitByte(OpCode op);
    void emitByteWithOperand(OpCode op, int operand);
    int addConstant(const Value& value);
    void resolveAndEmitLoad(const Token& name);
    void resolveAndEmitStore(const Token& name);
    int emitJump(OpCode jumpType);
    void patchJump(int offset);

    Bytecode bytecode;
    std::map<std::string, int> globalVarSlots;
    int nextSlot = 0;

    // Helper to compile a list of statements
    void compileStatements(const std::vector<std::unique_ptr<ELStatement>>& statements);
};