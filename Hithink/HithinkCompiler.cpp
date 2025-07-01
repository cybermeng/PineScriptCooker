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

void HithinkCompiler::visit(HithinkAssignmentStatement& node) {
    node.value->accept(*this);
    if (node.isOutput) {
        // 1. 处理输出变量（使用 ':'）
        //  - 将变量值存储到全局槽位
        //  - 将变量值再次压入栈顶 (这样绘图时栈顶始终是绘图数据)
        resolveAndEmitStore(node.name);
        resolveAndEmitLoad(node.name);
    } else {
        // 2. 处理普通赋值 (使用 ':='), 仅将值存储到全局槽位
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
        case TokenType::PLUS: emitByte(OpCode::ADD); break;
        case TokenType::MINUS: emitByte(OpCode::SUB); break;
        case TokenType::STAR: emitByte(OpCode::MUL); break;
        case TokenType::SLASH: emitByte(OpCode::DIV); break;
        case TokenType::GREATER: emitByte(OpCode::GREATER); break;
        case TokenType::GREATER_EQUAL: emitByte(OpCode::GREATER_EQUAL); break;
        case TokenType::LESS: emitByte(OpCode::LESS); break;
        case TokenType::LESS_EQUAL: emitByte(OpCode::LESS_EQUAL); break;
        case TokenType::EQUAL: emitByte(OpCode::EQUAL_EQUAL); break;
        case TokenType::BANG_EQUAL: emitByte(OpCode::BANG_EQUAL); break;
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
        int plotNameIndex = addConstant(builtin_mappings.at("DRAWTEXT")); // "plot"
        emitByteWithOperand(OpCode::PUSH_CONST, plotNameIndex);
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
    if (varName == "close" || varName == "high" || varName == "low" || varName == "open" || varName == "volume") {
        int constIndex = addConstant(varName);
        emitByteWithOperand(OpCode::LOAD_BUILTIN_VAR, constIndex);
        return;
    }

    // 2. 否则，作为全局变量处理
    if (globalVarSlots.find(varName) == globalVarSlots.end()) {
        // 首次使用，先定义
        globalVarSlots[varName] = nextSlot++;
    }
    emitByteWithOperand(OpCode::LOAD_GLOBAL, globalVarSlots[varName]);
}

void HithinkCompiler::resolveAndEmitStore(const Token& name) {
    std::string varName = name.lexeme;
    // 即使是输出变量，也使用相同的全局槽位
    if (globalVarSlots.find(varName) == globalVarSlots.end()) {
        globalVarSlots[varName] = nextSlot++;
    }
    emitByteWithOperand(OpCode::STORE_GLOBAL, globalVarSlots[varName]);
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