#include "PineCompiler.h"
#include <stdexcept> // For std::runtime_error
#include "PineParser.h" // 需要包含解析器来生成 AST

PineCompiler::PineCompiler() {}

Bytecode PineCompiler::compile(const std::string& source) {
    PineParser parser(source);
    auto statements = parser.parse();

    // 遍历所有顶层语句并为其生成代码
    for (const auto& stmt : statements) {
        stmt->accept(*this);
    }

    emitByte(OpCode::HALT);
    return bytecode;
}

std::string PineCompiler::compile_to_str(const std::string& source)
{
    Bytecode compiled_bytecode = compile(source);
    return bytecodeToTxt(compiled_bytecode);
}

// --- 访问者实现 ---

// 访问赋值语句: ma = ...
void PineCompiler::visit(AssignmentStmt& stmt) {
    // 1. 编译右侧的表达式，其结果将被压入栈顶
    stmt.initializer->accept(*this);

    // 2. 发出 STORE 指令
    resolveAndEmitStore(stmt.name);
}

// 访问表达式语句: plot(...)
void PineCompiler::visit(ExpressionStmt& stmt) {
    // 编译表达式
    stmt.expression->accept(*this);
    // 表达式的值留在栈上，对于plot等函数，它可能需要被弹出
    emitByte(OpCode::POP); 
}

// 访问 If 语句
void PineCompiler::visit(IfStmt& stmt) {
    // 1. 编译条件表达式，将结果留在栈顶
    stmt.condition->accept(*this);

    // 2. 发出 JUMP_IF_FALSE 指令，如果栈顶值为假，则跳过 then 分支
    int jumpIfFalseOffset = emitJump(OpCode::JUMP_IF_FALSE);

    // 3. 编译 then 分支
    for (const auto& thenStmt : stmt.thenBranch) {
        thenStmt->accept(*this);
    }

    // 4. 处理可选的 else 分支
    int jumpOverElseOffset = -1;
    if (stmt.elseBranch.size() > 0) {
        // 如果有 else 分支，则在 then 分支结束后发出一个 JUMP 指令，跳过 else 分支
        jumpOverElseOffset = emitJump(OpCode::JUMP);
    }

    // 5. 回填 JUMP_IF_FALSE 指令的操作数
    // 此时的当前位置是 else 分支的开始（如果有）或整个 if 语句的结束
    patchJump(jumpIfFalseOffset);

    // 6. 编译 else 分支 (如果存在)
    if (stmt.elseBranch.size() > 0) {
        for (const auto& elseStmt : stmt.elseBranch) {
            elseStmt->accept(*this);
        }
        // 7. 回填跳过 else 分支的 JUMP 指令的操作数
        patchJump(jumpOverElseOffset);
    }
}

// 访问字面量: 14, "MA Length"
void PineCompiler::visit(LiteralExpr& expr) {
    int constIndex = addConstant(expr.value);
    emitByteWithOperand(OpCode::PUSH_CONST, constIndex);
}

// 访问变量: close, ma_length
void PineCompiler::visit(VariableExpr& expr) {
    resolveAndEmitLoad(expr.name);
}

// 访问成员访问: color.blue
void PineCompiler::visit(MemberAccessExpr& expr) {
    // This visitor is for when a member access is an expression
    // that should produce a value on the stack, e.g., an argument like `color.blue`.
    if (auto* obj = dynamic_cast<VariableExpr*>(expr.object.get())) {
        if (obj->name.lexeme == "color") {
            // It's a color constant like color.blue
            // We can represent colors as special strings.
            // The VM will need to know how to interpret "color.blue".
            std::string colorValue = "color." + expr.member.lexeme;
            int constIndex = addConstant(colorValue);
            emitByteWithOperand(OpCode::PUSH_CONST, constIndex);
            return;
        }
    }
    throw std::runtime_error("Unsupported member access expression for value context.");
}

// 访问函数调用: ta.sma(close, 14)
void PineCompiler::visit(CallExpr& expr) {
    // 1. 编译所有参数，将它们的值压入栈
    for (const auto& arg : expr.arguments) {
        arg->accept(*this);
    }

    // 2. 处理调用者 (callee)
    if (auto* var = dynamic_cast<VariableExpr*>(expr.callee.get())) {
        // Simple function call like plot(...)
        std::string funcName = var->name.lexeme;
        if (funcName == "plot") { // PineScript's plot
            // CALL_PLOT expects: plot_name_string, series_to_plot_val, color_val (pushed in reverse order)
            // So push color, then series, then plot_name
            if (expr.arguments.size() == 2) { expr.arguments[1]->accept(*this); } else { int constIndex = addConstant(std::string("default_color")); emitByteWithOperand(OpCode::PUSH_CONST, constIndex); }
            expr.arguments[0]->accept(*this); // Push series
            int plotNameIndex = addConstant(std::string("plot")); // Push "plot" as default name
            emitByteWithOperand(OpCode::PUSH_CONST, plotNameIndex);
            emitByteWithOperand(OpCode::CALL_PLOT, 3); // Always 3 arguments now
        } else {
            int funcNameIndex = addConstant(funcName);
            emitByteWithOperand(OpCode::CALL_BUILTIN_FUNC, funcNameIndex);
        }
    } else if (auto* member = dynamic_cast<MemberAccessExpr*>(expr.callee.get())) {
        // Member call like ta.sma(...) or input.int(...)
        if (auto* obj = dynamic_cast<VariableExpr*>(member->object.get())) {
            std::string funcName = obj->name.lexeme + "." + member->member.lexeme;
            int funcNameIndex = addConstant(funcName);
            emitByteWithOperand(OpCode::CALL_BUILTIN_FUNC, funcNameIndex);
        } else {
            throw std::runtime_error("Unsupported callee: member access on non-variable.");
        }
    } else {
        throw std::runtime_error("Unsupported callee expression type.");
    }
}

// Corrected PineCompiler::visit(BinaryExpr& expr)
void PineCompiler::visit(BinaryExpr& expr) {
    // This is a regular binary operation (arithmetic or comparison)
    expr.left->accept(*this);  // Compile left operand, push its result
    expr.right->accept(*this); // Compile right operand, push its result

    switch (expr.op.type) {
        case TokenType::GREATER: emitByte(OpCode::GREATER); break;
        case TokenType::LESS: emitByte(OpCode::LESS); break;
        case TokenType::LESS_EQUAL: emitByte(OpCode::LESS_EQUAL); break;
        case TokenType::EQUAL_EQUAL: emitByte(OpCode::EQUAL_EQUAL); break;
        case TokenType::BANG_EQUAL: emitByte(OpCode::BANG_EQUAL); break;
        case TokenType::GREATER_EQUAL: emitByte(OpCode::GREATER_EQUAL); break;
        case TokenType::PLUS: emitByte(OpCode::ADD); break;
        case TokenType::MINUS: emitByte(OpCode::SUB); break;
        case TokenType::STAR: emitByte(OpCode::MUL); break;
        case TokenType::SLASH: emitByte(OpCode::DIV); break;
        default: // TokenType::EQUAL should not reach here if Parser is correct
            throw std::runtime_error("Unsupported binary operator: " + expr.op.lexeme);
    }
}

void PineCompiler::emitByte(OpCode op) {
    bytecode.instructions.push_back({op});
}

void PineCompiler::emitByteWithOperand(OpCode op, int operand) {
    bytecode.instructions.push_back({op, operand});
}

int PineCompiler::emitJump(OpCode jumpType) {
    emitByteWithOperand(jumpType, 0xFFFF); // 0xFFFF 是一个占位符
    return bytecode.instructions.size() - 1;
}

void PineCompiler::patchJump(int offset) {
    // 计算从跳转指令到当前指令的偏移量
    int jump = bytecode.instructions.size() - offset - 1;

    if (jump > 0xFFFF) {
        throw std::runtime_error("Jump offset too large!");
    }
    bytecode.instructions[offset].operand = jump;
}

// 解析变量并发出 LOAD 指令
void PineCompiler::resolveAndEmitLoad(const Token& name) {
    // 检查是内置变量还是全局变量
    if (name.lexeme == "close" || name.lexeme == "high") { // 等内置变量
        int constIndex = addConstant(name.lexeme);
        emitByteWithOperand(OpCode::LOAD_BUILTIN_VAR, constIndex);
    } else {
        if (globalVarSlots.find(name.lexeme) == globalVarSlots.end()) {
            // 变量首次使用，可能是一个错误，但这里我们先假定它已被定义
            // 生产级编译器会在此处做更严格的检查
            globalVarSlots[name.lexeme] = nextSlot++;
        }
        emitByteWithOperand(OpCode::LOAD_GLOBAL, globalVarSlots[name.lexeme]);
    }
}

// 解析变量并发出 STORE 指令
void PineCompiler::resolveAndEmitStore(const Token& name) {
    if (globalVarSlots.find(name.lexeme) == globalVarSlots.end()) {
        bytecode.global_name_pool.push_back(name.lexeme);
        globalVarSlots[name.lexeme] = nextSlot++;
    }
    emitByteWithOperand(OpCode::STORE_GLOBAL, globalVarSlots[name.lexeme]);
}

int PineCompiler::addConstant(const Value& value) {
    bytecode.constant_pool.push_back(value);
    return bytecode.constant_pool.size() - 1;
}
