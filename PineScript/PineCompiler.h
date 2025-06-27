#pragma once
#include "../CompilerCommon.h"
#include "PineAST.h" // For PineScript specific AST nodes
#include "PineParser.h" // For PineParser class (used in compile method)
#include <map>

class PineCompiler : public AstVisitor, public ExprVisitor {
public:
    PineCompiler();
    Bytecode compile(const std::string& source);

private:
    // AstVisitor methods
    void visit(AssignmentStmt& stmt) override;
    void visit(ExpressionStmt& stmt) override;
    void visit(IfStmt& stmt) override;

    // ExprVisitor methods
    void visit(LiteralExpr& expr) override;
    void visit(VariableExpr& expr) override;
    void visit(MemberAccessExpr& expr) override;
    void visit(CallExpr& expr) override;
    void visit(BinaryExpr& expr) override;

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
};