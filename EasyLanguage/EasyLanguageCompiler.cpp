#include "EasyLanguageCompiler.h"
#include <stdexcept>
#include <iostream> // For debugging

EasyLanguageCompiler::EasyLanguageCompiler() {}

Bytecode EasyLanguageCompiler::compile(const std::vector<std::unique_ptr<ELStatement>>& statements) {
    compileStatements(statements);
    emitByte(OpCode::HALT);
    return bytecode;
}

void EasyLanguageCompiler::compileStatements(const std::vector<std::unique_ptr<ELStatement>>& statements) {
    for (const auto& stmt : statements) {
        stmt->accept(*this);
    }
}

void EasyLanguageCompiler::visit(ELInputDeclaration& node) {
    // EasyLanguage Inputs are essentially global variables with default values.
    // Compile the default value expression if present.
    if (node.defaultValue) {
        node.defaultValue->accept(*this);
    } else {
        // If no default value, push a default (e.g., 0.0 or NAN)
        int constIndex = addConstant(0.0); // Default to 0.0 for numeric inputs
        emitByteWithOperand(OpCode::PUSH_CONST, constIndex);
    }
    resolveAndEmitStore(node.name);
}

void EasyLanguageCompiler::visit(ELVariableDeclaration& node) {
    // EasyLanguage Variables are also global variables.
    // Compile the initial value expression if present.
    if (node.initialValue) {
        node.initialValue->accept(*this);
    } else {
        // If no initial value, push a default (e.g., 0.0 or NAN)
        int constIndex = addConstant(0.0); // Default to 0.0 for numeric variables
        emitByteWithOperand(OpCode::PUSH_CONST, constIndex);
    }
    resolveAndEmitStore(node.name);
}

void EasyLanguageCompiler::visit(ELIfStatement& node) {
    node.condition->accept(*this); // Condition on stack

    int jumpIfFalseOffset = emitJump(OpCode::JUMP_IF_FALSE);

    compileStatements(node.thenBranch);

    int jumpOverElseOffset = -1;
    if (!node.elseBranch.empty()) {
        jumpOverElseOffset = emitJump(OpCode::JUMP);
    }

    patchJump(jumpIfFalseOffset);

    if (!node.elseBranch.empty()) {
        compileStatements(node.elseBranch);
        patchJump(jumpOverElseOffset);
    }
}

void EasyLanguageCompiler::visit(ELAssignmentStatement& node) {
    node.value->accept(*this);
    resolveAndEmitStore(node.name);
}

void EasyLanguageCompiler::visit(ELFunctionCallExpression& node) {
    // EasyLanguage function names need to be mapped to PineVM's built-in function names.
    // Example: Average -> ta.sma, RSI -> ta.rsi
    std::string pineVmFuncName;

    // Handle PlotX as a special function call
    if (node.name.lexeme.rfind("Plot", 0) == 0 && node.name.lexeme.length() > 4 && isdigit(node.name.lexeme[4])) {
        // This is a PlotX function call (e.g., Plot1, Plot2)
        // CALL_PLOT expects: plot_name_string, series_to_plot_val, color_val (pushed in reverse order)
        // So push color, then series, then plot_name
        if (node.arguments.size() == 2) { // value, color
            node.arguments[1]->accept(*this); // Push color_val
        } else { // Only value, no color
            int constIndex = addConstant(std::string("default_color"));
            emitByteWithOperand(OpCode::PUSH_CONST, constIndex); // Push default_color
        }
        node.arguments[0]->accept(*this); // Push series_to_plot_val
        int plotNameIndex = addConstant(node.name.lexeme); // Push plot_name_string
        emitByteWithOperand(OpCode::PUSH_CONST, plotNameIndex);
        emitByteWithOperand(OpCode::CALL_PLOT, 3); // 3 arguments: plot_name, series, color
        return; // Handled
    }

    if (node.name.type == TokenType::AVERAGE) {
        pineVmFuncName = "ta.sma"; // Map EasyLanguage Average to PineVM's ta.sma
    } else if (node.name.type == TokenType::RSI_EL) {
        pineVmFuncName = "ta.rsi"; // Map EasyLanguage RSI to PineVM's ta.rsi
    } else if (node.name.lexeme == "Close") { // Special handling for Close as a function call (e.g., Close[1])
        // The current PineVM only supports `close` as a variable (current bar).
        // If EasyLanguage calls Close() with arguments, it implies historical access.
        // For this simplified compiler, we'll only support `Close` as a variable.
        // If it's `Close(offset)`, it would require more complex bytecode generation (e.g., push series, push offset, then INDEX_SERIES opcode).
        // For now, if it's a function call, we'll treat it as an error.
        throw std::runtime_error("EasyLanguage Compiler Error: Line " + std::to_string(node.name.line) + ": 'Close' as a function call is not supported in this simplified compiler. Use 'Close' as a variable.");
    }
    else {
        // For other functions, assume direct mapping or error
        throw std::runtime_error("EasyLanguage Compiler Error: Line " + std::to_string(node.name.line) + ": Unsupported EasyLanguage function: " + node.name.lexeme);
    }

    // Push arguments onto the stack in order
    for (const auto& arg : node.arguments) {
        arg->accept(*this);
    }

    int funcNameIndex = addConstant(pineVmFuncName);
    emitByteWithOperand(OpCode::CALL_BUILTIN_FUNC, funcNameIndex);
}

void EasyLanguageCompiler::visit(ELPlotStatement& node) {
    // CALL_PLOT expects: plot_name_string, series_to_plot_val, color_val (pushed in reverse order)
    // So push color, then series, then plot_name

    if (node.color) {
        node.color->accept(*this); // Push color_val
    } else {
        int constIndex = addConstant(std::string("default_color")); // Or a specific default like "blue"
        emitByteWithOperand(OpCode::PUSH_CONST, constIndex); // Push default_color
    }
    node.value->accept(*this); // Push series_to_plot_val
    int plotNameIndex = addConstant(node.plotName.lexeme); // Push plot_name_string
    emitByteWithOperand(OpCode::PUSH_CONST, plotNameIndex);
    emitByteWithOperand(OpCode::CALL_PLOT, 3); // 3 arguments: plot_name, series, color
}

void EasyLanguageCompiler::visit(ELExpressionStatement& node) {
    node.expression->accept(*this); // Compile the expression
    emitByte(OpCode::POP); // Pop the result of the expression, as statements don't leave values on the stack
}

void EasyLanguageCompiler::visit(ELLiteralExpression& node) {
    int constIndex = addConstant(node.value);
    emitByteWithOperand(OpCode::PUSH_CONST, constIndex);
}

void EasyLanguageCompiler::visit(ELVariableExpression& node) {
    // EasyLanguage variables like 'Close' or user-defined 'MyVar'
    // Map 'Close' to PineVM's 'close' built-in variable.
    if (node.name.lexeme == "Close") {
        int constIndex = addConstant(std::string("close")); // PineVM expects "close"
        emitByteWithOperand(OpCode::LOAD_BUILTIN_VAR, constIndex);
    } else {
        resolveAndEmitLoad(node.name);
    }
}

void EasyLanguageCompiler::visit(ELBinaryExpression& node) {
    node.left->accept(*this);
    node.right->accept(*this);

    switch (node.op.type) {
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
        default:
            throw std::runtime_error("EasyLanguage Compiler Error: Unsupported binary operator: " + node.op.lexeme);
    }
}

void EasyLanguageCompiler::emitByte(OpCode op) {
    bytecode.instructions.push_back({op});
}

void EasyLanguageCompiler::emitByteWithOperand(OpCode op, int operand) {
    bytecode.instructions.push_back({op, operand});
}

int EasyLanguageCompiler::addConstant(const Value& value) {
    bytecode.constant_pool.push_back(value);
    return bytecode.constant_pool.size() - 1;
}

void EasyLanguageCompiler::resolveAndEmitLoad(const Token& name) {
    // For user-defined variables (Inputs, Variables)
    if (globalVarSlots.find(name.lexeme) == globalVarSlots.end()) {
        // This means a variable is used before it's declared.
        // In EasyLanguage, Inputs/Variables are typically declared at the top.
        // For simplicity, we'll assign a slot, but a real compiler would flag this as an error.
        globalVarSlots[name.lexeme] = nextSlot++;
    }
    emitByteWithOperand(OpCode::LOAD_GLOBAL, globalVarSlots[name.lexeme]);
}

void EasyLanguageCompiler::resolveAndEmitStore(const Token& name) {
    if (globalVarSlots.find(name.lexeme) == globalVarSlots.end()) {
        globalVarSlots[name.lexeme] = nextSlot++;
    }
    emitByteWithOperand(OpCode::STORE_GLOBAL, globalVarSlots[name.lexeme]);
}

int EasyLanguageCompiler::emitJump(OpCode jumpType) {
    emitByteWithOperand(jumpType, 0xFFFF); // Placeholder
    return bytecode.instructions.size() - 1;
}

void EasyLanguageCompiler::patchJump(int offset) {
    int jump = bytecode.instructions.size() - offset - 1;
    if (jump > 0xFFFF) {
        throw std::runtime_error("Jump offset too large!");
    }
    bytecode.instructions[offset].operand = jump;
}