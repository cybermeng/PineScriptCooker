#include "EasyLanguageCompiler.h"
#include "EasyLanguageParser.h"
#include <stdexcept>
#include <algorithm>

const std::unordered_map<std::string, std::string> EasyLanguageCompiler::builtin_mappings = {
    {"CLOSE", "close"}, {"C", "close"},
    {"OPEN", "open"},   {"O", "open"},
    {"HIGH", "high"},   {"H", "high"},
    {"LOW", "low"},     {"L", "low"},
    {"VOL", "volume"},  {"V", "volume"},
    {"VOLUME", "volume"},
    {"AMOUNT", "amount"},
    {"DATE", "date"},
    {"TIME", "time"}
};

EasyLanguageCompiler::EasyLanguageCompiler() : hadError_(false) {}

bool EasyLanguageCompiler::hadError() const { return hadError_; }

Bytecode EasyLanguageCompiler::compile(std::string_view source) {
    EasyLanguageParser parser(source);
    auto statements = parser.parse();

    hadError_ = parser.hadError();
    if (hadError_) {
        return bytecode;
    }

    for (const auto& statement : statements) {
        statement->accept(*this);
    }

    emitByte(OpCode::HALT);
    return bytecode;
}

std::string EasyLanguageCompiler::compile_to_str(std::string_view source) {
    Bytecode compiled_bytecode = compile(source);
    if (hadError()) {
        return "Compilation failed.";
    }
    return bytecodeToTxt(compiled_bytecode);
}

// --- Visitor Implementations ---

void EasyLanguageCompiler::visit(DeclarationsStatement& stmt) {
    for (auto& decl : stmt.declarations) {
        if (decl.initializer) {
            decl.initializer->accept(*this);
        } else {
            // Default initializer is 0
            int constIndex = addConstant(0.0);
            emitByteWithOperand(OpCode::PUSH_CONST, constIndex);
        }
        emitStore(decl.name);
    }
}

void EasyLanguageCompiler::visit(AssignmentStatement& stmt) {
    stmt.value->accept(*this);
    emitStore(stmt.name);
}

void EasyLanguageCompiler::visit(IfStatement& stmt) {
    stmt.condition->accept(*this);
    int thenJump = emitJump(OpCode::JUMP_IF_FALSE);

    stmt.thenBranch->accept(*this);

    int elseJump = -1;
    if (stmt.elseBranch) {
        elseJump = emitJump(OpCode::JUMP);
    }

    patchJump(thenJump);

    if (stmt.elseBranch) {
        stmt.elseBranch->accept(*this);
        patchJump(elseJump);
    }
}

void EasyLanguageCompiler::visit(BlockStatement& stmt) {
    for (const auto& statement : stmt.statements) {
        statement->accept(*this);
    }
}

void EasyLanguageCompiler::visit(ExpressionStatement& stmt) {
    stmt.expression->accept(*this);
    emitByte(OpCode::POP); // Discard the result of the expression
}

void EasyLanguageCompiler::visit(EmptyStatement& stmt) {
    // Do nothing for empty statements
}

void EasyLanguageCompiler::visit(BinaryExpression& node) {
    node.left->accept(*this);
    node.right->accept(*this);

    switch (node.op.type) {
        case TokenType::PLUS:          emitByteForMath(OpCode::ADD); break;
        case TokenType::MINUS:         emitByteForMath(OpCode::SUB); break;
        case TokenType::STAR:          emitByteForMath(OpCode::MUL); break;
        case TokenType::SLASH:         emitByteForMath(OpCode::DIV); break;
        case TokenType::GREATER:       emitByteForMath(OpCode::GREATER); break;
        case TokenType::GREATER_EQUAL: emitByteForMath(OpCode::GREATER_EQUAL); break;
        case TokenType::LESS:          emitByteForMath(OpCode::LESS); break;
        case TokenType::LESS_EQUAL:    emitByteForMath(OpCode::LESS_EQUAL); break;
        case TokenType::EQUAL:         emitByteForMath(OpCode::EQUAL_EQUAL); break;
        case TokenType::BANG_EQUAL:    emitByteForMath(OpCode::BANG_EQUAL); break;
        case TokenType::AND:           emitByteForMath(OpCode::LOGICAL_AND); break;
        case TokenType::OR:            emitByteForMath(OpCode::LOGICAL_OR); break;
        default: throw std::runtime_error("Unknown binary operator in compiler.");
    }
}

void EasyLanguageCompiler::visit(UnaryExpression& node) {
    node.right->accept(*this);
    if (node.op.type == TokenType::MINUS) {
        auto right = bytecode.instructions.back();
        bytecode.instructions.pop_back();
        int constIndex = addConstant(0.0);
        emitByteWithOperand(OpCode::PUSH_CONST, constIndex);
        bytecode.instructions.push_back(right);
        emitByteForMath(OpCode::SUB);
    } else {
        throw std::runtime_error("Unsupported unary operator.");
    }
}

void EasyLanguageCompiler::visit(LiteralExpression& node) {
    int constIndex = addConstant(node.value);
    emitByteWithOperand(OpCode::PUSH_CONST, constIndex);
}

void EasyLanguageCompiler::visit(VariableExpression& node) {
    resolveAndEmitLoad(node.name);
}

void EasyLanguageCompiler::visit(FunctionCallExpression& node) {
    for (const auto& arg : node.arguments) {
        arg->accept(*this);
    }
    int argCount = node.arguments.size();
    int argCountConstIndex = addConstant(static_cast<double>(argCount));
    emitByteWithOperand(OpCode::PUSH_CONST, argCountConstIndex);

    std::string funcName = node.name.lexeme;
    std::transform(funcName.begin(), funcName.end(), funcName.begin(), ::tolower);
    
    int funcNameConstIndex = addConstant(funcName);
    emitByteWithOperand(OpCode::CALL_BUILTIN_FUNC, funcNameConstIndex);
}

void EasyLanguageCompiler::visit(SubscriptExpression& node) {
    node.callee->accept(*this);
    node.index->accept(*this);
    emitByteForMath(OpCode::SUBSCRIPT);
}

// --- Bytecode Emitters and Helpers ---

void EasyLanguageCompiler::emitByte(OpCode op) {
    bytecode.instructions.push_back({op});
}

void EasyLanguageCompiler::emitByteForMath(OpCode op) {
    bytecode.instructions.push_back({op, bytecode.varNum++});
}

void EasyLanguageCompiler::emitByteWithOperand(OpCode op, int operand) {
    bytecode.instructions.push_back({op, operand});
}

int EasyLanguageCompiler::addConstant(const Value& value) {
    bytecode.constant_pool.push_back(value);
    return bytecode.constant_pool.size() - 1;
}

int EasyLanguageCompiler::resolveAndDefineVar(const std::string& varName) {
    if (globalVarSlots.find(varName) == globalVarSlots.end()) {
        bytecode.global_name_pool.push_back(varName);
        globalVarSlots[varName] = nextSlot++;
    }
    return globalVarSlots[varName];
}

void EasyLanguageCompiler::resolveAndEmitLoad(const Token& name) {
    std::string varName = name.lexeme;
    std::string upperVarName = varName;
    std::transform(upperVarName.begin(), upperVarName.end(), upperVarName.begin(), ::toupper);

    if (builtin_mappings.count(upperVarName)) {
        std::string builtinName = builtin_mappings.at(upperVarName);
        int constIndex = addConstant(builtinName);
        emitByteWithOperand(OpCode::LOAD_BUILTIN_VAR, constIndex);
        return;
    }

    if (globalVarSlots.find(varName) == globalVarSlots.end()) {
        // Undeclared variable, treat as an error or implicitly define
        // For simplicity, we implicitly define it, which is common in some scripting languages.
        // A stricter compiler would throw an error here.
        resolveAndDefineVar(varName);
    }
    emitByteWithOperand(OpCode::LOAD_GLOBAL, globalVarSlots[varName]);
}

void EasyLanguageCompiler::emitStore(const Token& name) {
    int slot = resolveAndDefineVar(name.lexeme);
    emitByteWithOperand(OpCode::STORE_GLOBAL, slot);
}

int EasyLanguageCompiler::emitJump(OpCode jumpType) {
    emitByteWithOperand(jumpType, 0xFFFF); // Placeholder offset
    return bytecode.instructions.size() - 1;
}

void EasyLanguageCompiler::patchJump(int offset) {
    int jump = bytecode.instructions.size() - offset - 1;
    if (jump > 0xFFFF) {
        throw std::runtime_error("Jump offset too large!");
    }
    bytecode.instructions[offset].operand = jump;
}