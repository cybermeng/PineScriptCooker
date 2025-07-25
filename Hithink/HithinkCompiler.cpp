#include "HithinkCompiler.h"
#include "HithinkParser.h" // 使用解析器来构建AST
#include <stdexcept>
#include <algorithm> // for std::transform
#include <string>

// 将 Hithink 内置名称映射到 PineVM 内置名称
const std::unordered_map<std::string, std::string> HithinkCompiler::builtin_mappings = {
    {"CLOSE", "close"},
    {"C", "close"},
    {"OPEN", "open"},
    {"O", "open"},
    {"HIGH", "high"},
    {"H", "high"},
    {"LOW", "low"},
    {"L", "low"},
    {"VOL", "volume"},
    {"V", "volume"},
    {"DATE", "date"},
    {"TIME", "time"},
    // 将 DRAWTEXT 映射到一个绘图函数。为简单起见，我们使用 plot。
    // VM 的 plot 函数是 `plot(series, title, color)`
    // DRAWTEXT 是 `DRAWTEXT(COND, PRICE, TEXT)`。我们可以将其映射到 plot(PRICE, TEXT)。
    {"DRAWTEXT", "plot"}
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

std::string HithinkCompiler::compile_to_str(std::string_view source)
{
    Bytecode compiled_bytecode = compile(source);
    if (hadError()) {
        return "Compilation failed.";
    }
    return bytecodeToTxt(compiled_bytecode);
}

void HithinkCompiler::visit(HithinkEmptyStatement& stmt) {
    // 空语句不需要生成任何字节码
    // 编译器会简单地跳过它
}

void HithinkCompiler::visit(HithinkAssignmentStatement& node) {
    node.value->accept(*this);
    if (node.isOutput) {
        // 1. 处理输出变量 (使用 ':')
        //  - 将其存储到全局槽位并标记为绘图
        resolveAndEmitStoreAndPlot(node.name);
    } else {
        // 2. 处理普通赋值 (使用 ':=')
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
        case TokenType::PLUS: emitByteForMath(OpCode::ADD); break;
        case TokenType::MINUS: emitByteForMath(OpCode::SUB); break;
        case TokenType::STAR: emitByteForMath(OpCode::MUL); break;
        case TokenType::SLASH: emitByteForMath(OpCode::DIV); break;
        case TokenType::GREATER: emitByteForMath(OpCode::GREATER); break;
        case TokenType::GREATER_EQUAL: emitByteForMath(OpCode::GREATER_EQUAL); break;
        case TokenType::LESS: emitByteForMath(OpCode::LESS); break;
        case TokenType::LESS_EQUAL: emitByteForMath(OpCode::LESS_EQUAL); break;
        case TokenType::EQUAL: emitByteForMath(OpCode::EQUAL_EQUAL); break;
        case TokenType::BANG_EQUAL: emitByteForMath(OpCode::BANG_EQUAL); break;
        case TokenType::AND: emitByteForMath(OpCode::LOGICAL_AND); break;
        case TokenType::OR: emitByteForMath(OpCode::LOGICAL_OR); break;
        default: throw std::runtime_error("Unknown binary operator.");
    }
}

void HithinkCompiler::visit(HithinkFunctionCallExpression& node) {
    // 特殊处理 DRAWTEXT: 转换为 plot, 并添加条件跳转
    if (node.name.lexeme == "DRAWTEXT") {
        if (node.arguments.size() != 3) {
            throw std::runtime_error("DRAWTEXT expects 3 arguments (condition, price, text).");
        }

        // 1. 编译条件表达式
        node.arguments[0]->accept(*this);

        // 2. 如果条件为假，则跳转到绘图语句之后
        int jump_if_false = emitJump(OpCode::JUMP_IF_FALSE);

        // 3. 编译绘图语句 (plot(price, text))
        node.arguments[2]->accept(*this); // text (plot name)
        node.arguments[1]->accept(*this); // price (series)
        int plotNameIndex = addConstant(builtin_mappings.at("drawtext")); // "plot"
        emitByteWithOperand(OpCode::CALL_PLOT, 3); // CALL_PLOT (plot_name, series, color), always 3 args

        // 4. 回填跳转指令
        patchJump(jump_if_false);
        return;
    }

    // 常规函数调用处理
    for (const auto& arg : node.arguments) {
        arg->accept(*this);
    }

    std::string funcName = node.name.lexeme;
    // 将函数名转换为小写，因为PineVM的内置函数名是小写的
    std::transform(funcName.begin(), funcName.end(), funcName.begin(),
                   [](unsigned char c){ return std::tolower(c); });
    if (builtin_mappings.count(funcName)) {
        funcName = builtin_mappings.at(funcName); // 使用映射
    }

    int constIndex = addConstant(funcName);
    emitByteWithOperand(OpCode::CALL_BUILTIN_FUNC, constIndex);
}

void HithinkCompiler::visit(HithinkLiteralExpression& node) {
    int constIndex = addConstant(node.value);
    emitByteWithOperand(OpCode::PUSH_CONST, constIndex);
}

void HithinkCompiler::visit(HithinkUnaryExpression& node) {
    node.right->accept(*this);
    if (node.op.type == TokenType::MINUS) {
        //  0 - x  来实现  -x
        int constIndex = addConstant(0.0);
        emitByteWithOperand(OpCode::PUSH_CONST, constIndex);
        emitByte(OpCode::SUB);
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
    if (builtin_mappings.count(varName)) {
        varName = builtin_mappings.at(varName);
    }

    // 1. 尝试作为内置变量加载 (例如 'close')
    if (varName == "close" || varName == "high" || varName == "low" || varName == "open" || varName == "volume"
         || varName == "time" || varName == "date") {
        int constIndex = addConstant(varName);
        emitByteWithOperand(OpCode::LOAD_BUILTIN_VAR, constIndex);
        return;
    }

    // 2. 否则，作为全局变量处理
    if (globalVarSlots.find(varName) == globalVarSlots.end()) {
        // 首次使用，先定义
        bytecode.global_name_pool.push_back(varName);
        globalVarSlots[varName] = nextSlot++;
    }
    emitByteWithOperand(OpCode::LOAD_GLOBAL, globalVarSlots[varName]);
}

void HithinkCompiler::resolveAndEmitStore(const Token& name) {
    std::string varName = name.lexeme;
    // 即使是输出变量，也使用相同的全局槽位 (Even if it's an output variable, use the same global slot)
    if (globalVarSlots.find(varName) == globalVarSlots.end()) {
        bytecode.global_name_pool.push_back(varName);
        globalVarSlots[varName] = nextSlot++;
    }
    emitByteWithOperand(OpCode::STORE_GLOBAL, globalVarSlots[varName]);
}

void HithinkCompiler::resolveAndEmitStoreAndPlot(const Token& name) {
    std::string varName = name.lexeme;
    // 输出变量也使用全局槽位 (Output variables also use global slots)
    if (globalVarSlots.find(varName) == globalVarSlots.end()) {
        bytecode.global_name_pool.push_back(varName);
        globalVarSlots[varName] = nextSlot++;
    }
    emitByteWithOperand(OpCode::STORE_AND_PLOT_GLOBAL, globalVarSlots[varName]);
}

int HithinkCompiler::emitJump(OpCode jumpType) {
    emitByteWithOperand(jumpType, 0xFFFF);  // 0xFFFF 是一个占位符
    return bytecode.instructions.size() - 1;
}

void HithinkCompiler::patchJump(int offset) {
    int jump = bytecode.instructions.size() - offset - 1;

    if (jump > 0xFFFF) {
        throw std::runtime_error("Jump offset too large!");
    }

    bytecode.instructions[offset].operand = jump;
}