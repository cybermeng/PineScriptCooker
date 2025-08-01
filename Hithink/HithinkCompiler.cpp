#include "HithinkCompiler.h"
#include "HithinkParser.h"
#include <stdexcept>
#include <algorithm>
#include <string>

const std::unordered_map<std::string, std::string> HithinkCompiler::builtin_mappings = {
    {"CLOSE", "close"}, {"C", "close"},
    {"OPEN", "open"},   {"O", "open"},
    {"HIGH", "high"},   {"H", "high"},
    {"LOW", "low"},     {"L", "low"},
    {"VOL", "volume"},  {"V", "volume"},
    {"AMOUNT", "amount"},
    {"DATE", "date"},
    {"TIME", "time"}
};

HithinkCompiler::HithinkCompiler() : hadError_(false) {}

bool HithinkCompiler::hadError() const {
    return hadError_;
}

Bytecode HithinkCompiler::compile(std::string_view source) {
    HithinkParser parser(source);
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

std::string HithinkCompiler::compile_to_str(std::string_view source) {
    Bytecode compiled_bytecode = compile(source);
    if (hadError()) {
        return "Compilation failed.";
    }
    return bytecodeToTxt(compiled_bytecode);
}

void HithinkCompiler::visit(HithinkEmptyStatement& stmt) {
    // 空语句不生成字节码
}

void HithinkCompiler::visit(HithinkAssignmentStatement& node) {
    node.value->accept(*this);
    if (node.isOutput) {
        resolveAndEmitStoreGlobal(node.name);
    } else {
        resolveAndEmitStore(node.name);
    }
}

void HithinkCompiler::visit(HithinkExpressionStatement& node) {
    node.expression->accept(*this);
    emitByte(OpCode::POP);
}

void HithinkCompiler::visit(HithinkBinaryExpression& node) {
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
        default: throw std::runtime_error("Unknown binary operator.");
    }
}

// 新增：为下标表达式生成字节码
void HithinkCompiler::visit(HithinkSubscriptExpression& node) {
    node.callee->accept(*this); // 编译被访问的对象 (e.g., CLOSE)
    node.index->accept(*this);  // 编译下标 (e.g., 2)
    emitByteForMath(OpCode::SUBSCRIPT); // 发出下标操作码
}

void HithinkCompiler::visit(HithinkFunctionCallExpression& node) {
    // Step 1: Compile all arguments and push them onto the stack.
    for (const auto& arg : node.arguments) {
        arg->accept(*this);
    }

    // Step 2: Push the number of arguments onto the stack. The VM will pop this
    // value to determine how many arguments to retrieve for the call.
    int argCount = node.arguments.size();
    int argCountConstIndex = addConstant(static_cast<double>(argCount));
    emitByteWithOperand(OpCode::PUSH_CONST, argCountConstIndex);

    // Step 3: Prepare the function name and emit the call instruction.
    std::string funcName = node.name.lexeme;
    // Normalize the function name (e.g., to lowercase for case-insensitivity)
    std::transform(funcName.begin(), funcName.end(), funcName.begin(),
                   [](unsigned char c){ return std::tolower(c); });
    
    // Preserve original mapping logic for compatibility.
    if (builtin_mappings.count(funcName)) {
        funcName = builtin_mappings.at(funcName);
    }

    // Add the function name string to the constant pool.
    int funcNameConstIndex = addConstant(funcName);
    // Emit the call instruction. Its operand is the index of the function's name.
    emitByteWithOperand(OpCode::CALL_BUILTIN_FUNC, funcNameConstIndex);
}

void HithinkCompiler::visit(HithinkLiteralExpression& node) {
    int constIndex = addConstant(node.value);
    emitByteWithOperand(OpCode::PUSH_CONST, constIndex);
}

void HithinkCompiler::visit(HithinkUnaryExpression& node) {
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

void HithinkCompiler::visit(HithinkVariableExpression& node) {
    resolveAndEmitLoad(node.name);
}

void HithinkCompiler::emitByte(OpCode op) {
    bytecode.instructions.push_back({op});
}

void HithinkCompiler::emitByteForMath(OpCode op) {
    bytecode.instructions.push_back({op, bytecode.varNum++});
}

void HithinkCompiler::emitByteWithOperand(OpCode op, int operand) {
    bytecode.instructions.push_back({op, operand});
}

int HithinkCompiler::addConstant(const Value& value) {
    bytecode.constant_pool.push_back(value);
    return bytecode.constant_pool.size() - 1;
}

void HithinkCompiler::resolveAndEmitLoad(const Token& name) {
    std::string varName = name.lexeme;
    // 转换为大写进行不区分大小写的查找
    std::string upperVarName = varName;
    std::transform(upperVarName.begin(), upperVarName.end(), upperVarName.begin(), ::toupper);

    if (builtin_mappings.count(upperVarName)) {
        varName = builtin_mappings.at(upperVarName);
    }

    if (varName == "close" || varName == "high" || varName == "low" || varName == "open" || varName == "volume"
         || varName == "time" || varName == "date") {
        int constIndex = addConstant(varName);
        emitByteWithOperand(OpCode::LOAD_BUILTIN_VAR, constIndex);
        return;
    }

    if (globalVarSlots.find(varName) == globalVarSlots.end()) {
        bytecode.global_name_pool.push_back(varName);
        globalVarSlots[varName] = nextSlot++;
    }
    emitByteWithOperand(OpCode::LOAD_GLOBAL, globalVarSlots[varName]);
}

void HithinkCompiler::resolveAndEmitStore(const Token& name) {
    std::string varName = name.lexeme;
    if (globalVarSlots.find(varName) == globalVarSlots.end()) {
        bytecode.global_name_pool.push_back(varName);
        globalVarSlots[varName] = nextSlot++;
    }
    emitByteWithOperand(OpCode::STORE_GLOBAL, globalVarSlots[varName]);
}

void HithinkCompiler::resolveAndEmitStoreGlobal(const Token& name) {
    std::string varName = name.lexeme;
    if (globalVarSlots.find(varName) == globalVarSlots.end()) {
        bytecode.global_name_pool.push_back(varName);
        globalVarSlots[varName] = nextSlot++;
    }
    emitByteWithOperand(OpCode::STORE_EXPORT, globalVarSlots[varName]);
}

int HithinkCompiler::emitJump(OpCode jumpType) {
    emitByteWithOperand(jumpType, 0xFFFF);
    return bytecode.instructions.size() - 1;
}

void HithinkCompiler::patchJump(int offset) {
    int jump = bytecode.instructions.size() - offset - 1;
    if (jump > 0xFFFF) {
        throw std::runtime_error("Jump offset too large!");
    }
    bytecode.instructions[offset].operand = jump;
}