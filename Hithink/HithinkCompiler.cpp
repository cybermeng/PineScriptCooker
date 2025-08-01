#include "HithinkCompiler.h"
#include "HithinkParser.h"
#include <stdexcept>
#include <algorithm>
#include <string>
#include <sstream> // For bytecodeToScript
#include <map>     // For bytecodeToScript

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

    if (varName == "close" || varName == "high" || varName == "low" || varName == "open"
         || varName == "volume" || varName == "amount"
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

// Helper to convert a Value variant to its string representation for the script
static std::string valueToString(const Value& value) {
    return std::visit([](auto&& arg) -> std::string {
        using T = std::decay_t<decltype(arg)>;
        if constexpr (std::is_same_v<T, std::monostate>) {
            return "null"; // Or some other representation for nil/monostate
        } else if constexpr (std::is_same_v<T, double>) {
            std::stringstream ss;
            ss << arg;
            return ss.str();
        } else if constexpr (std::is_same_v<T, bool>) {
            return arg ? "true" : "false"; // Hithink doesn't have bool literals, but this is for completeness
        } else if constexpr (std::is_same_v<T, std::string>) {
            // Add quotes around the string literal
            return "'" + arg + "'";
        } else if constexpr (std::is_same_v<T, std::shared_ptr<Series>>) {
            // This shouldn't typically appear as a literal in code, but handle it just in case
            return arg->name;
        }
        return "<?>"; // Unknown type
    }, value);
}

std::string HithinkCompiler::bytecodeToScript(const Bytecode& bytecode) {
    if (bytecode.instructions.empty()) {
        return "";
    }

    std::vector<std::string> expression_stack;
    std::vector<std::string> statements;

    // Map binary opcodes to their string symbols
    const std::map<OpCode, std::string> binary_op_map = {
        {OpCode::ADD,           " + "},
        {OpCode::SUB,           " - "},
        {OpCode::MUL,           " * "},
        {OpCode::DIV,           " / "},
        {OpCode::GREATER,       " > "},
        {OpCode::GREATER_EQUAL, " >= "},
        {OpCode::LESS,          " < "},
        {OpCode::LESS_EQUAL,    " <= "},
        {OpCode::EQUAL_EQUAL,   " = "}, // Use '=' for equality as per Hithink grammar
        {OpCode::BANG_EQUAL,    " <> "},// Use '<>' for inequality
        {OpCode::LOGICAL_AND,   " AND "},
        {OpCode::LOGICAL_OR,    " OR "}
    };

    for (const auto& instr : bytecode.instructions) {
        switch (instr.op) {
            case OpCode::PUSH_CONST: {
                expression_stack.push_back(valueToString(bytecode.constant_pool[instr.operand]));
                break;
            }

            case OpCode::LOAD_BUILTIN_VAR:
            case OpCode::LOAD_GLOBAL: {
                std::string varName;
                if (instr.op == OpCode::LOAD_BUILTIN_VAR) {
                     // The name is stored as a string constant
                    varName = std::get<std::string>(bytecode.constant_pool[instr.operand]);
                } else { // LOAD_GLOBAL
                    varName = bytecode.global_name_pool[instr.operand];
                }
                 // Built-in variables are often uppercase in scripts
                std::transform(varName.begin(), varName.end(), varName.begin(), ::toupper);
                expression_stack.push_back(varName);
                break;
            }

            case OpCode::ADD:
            case OpCode::SUB:
            case OpCode::MUL:
            case OpCode::DIV:
            case OpCode::GREATER:
            case OpCode::GREATER_EQUAL:
            case OpCode::LESS:
            case OpCode::LESS_EQUAL:
            case OpCode::EQUAL_EQUAL:
            case OpCode::BANG_EQUAL:
            case OpCode::LOGICAL_AND:
            case OpCode::LOGICAL_OR: {
                if (expression_stack.size() < 2) throw std::runtime_error("Decompile error: stack underflow for binary op.");
                std::string right = expression_stack.back(); expression_stack.pop_back();
                std::string left = expression_stack.back(); expression_stack.pop_back();
                std::string op_str = binary_op_map.at(instr.op);
                expression_stack.push_back("(" + left + op_str + right + ")");
                break;
            }

            case OpCode::SUBSCRIPT: {
                if (expression_stack.size() < 2) throw std::runtime_error("Decompile error: stack underflow for subscript.");
                std::string index = expression_stack.back(); expression_stack.pop_back();
                std::string callee = expression_stack.back(); expression_stack.pop_back();
                expression_stack.push_back(callee + "[" + index + "]");
                break;
            }
            
            case OpCode::CALL_BUILTIN_FUNC: {
                if (expression_stack.empty()) throw std::runtime_error("Decompile error: stack underflow for function call arg count.");
                
                // The argument count was pushed as a constant right before the call
                int arg_count = static_cast<int>(std::stod(expression_stack.back()));
                expression_stack.pop_back();

                if (expression_stack.size() < arg_count) throw std::runtime_error("Decompile error: stack underflow for function arguments.");

                std::vector<std::string> args;
                for (int i = 0; i < arg_count; ++i) {
                    args.push_back(expression_stack.back());
                    expression_stack.pop_back();
                }
                std::reverse(args.begin(), args.end());

                std::string func_name_str = valueToString(bytecode.constant_pool[instr.operand]);
                // The function name is stored as a string, remove the quotes added by valueToString
                func_name_str = func_name_str.substr(1, func_name_str.length() - 2);
                std::transform(func_name_str.begin(), func_name_str.end(), func_name_str.begin(), ::toupper);


                std::stringstream call_ss;
                call_ss << func_name_str << "(";
                for (size_t i = 0; i < args.size(); ++i) {
                    call_ss << args[i] << (i == args.size() - 1 ? "" : ", ");
                }
                call_ss << ")";
                expression_stack.push_back(call_ss.str());
                break;
            }
            
            case OpCode::STORE_GLOBAL:
            case OpCode::STORE_EXPORT: {
                if (expression_stack.empty()) throw std::runtime_error("Decompile error: stack underflow for assignment.");
                std::string value_str = expression_stack.back(); expression_stack.pop_back();
                std::string var_name = bytecode.global_name_pool[instr.operand];

                // Special case for `SELECT` keyword
                if (var_name == "select" && instr.op == OpCode::STORE_EXPORT) {
                    statements.push_back("SELECT " + value_str + ";");
                } else {
                    std::string op = (instr.op == OpCode::STORE_EXPORT) ? ":" : ":=";
                    statements.push_back(var_name + " " + op + " " + value_str + ";");
                }
                break;
            }
            
            case OpCode::POP: {
                if (expression_stack.empty()) throw std::runtime_error("Decompile error: stack underflow for POP.");
                // This is an expression statement, like a DRAWTEXT() call that isn't assigned.
                std::string expr_str = expression_stack.back();
                expression_stack.pop_back();
                statements.push_back(expr_str + ";");
                break;
            }

            case OpCode::HALT:
                goto end_loop; // Exit the loop

            default:
                // Ignore other opcodes like JUMP for now as they are not generated by this compiler.
                // Or throw an error if they are unexpected.
                // throw std::runtime_error("Unsupported opcode in decompiler: " + std::to_string(static_cast<int>(instr.op)));
                break;
        }
    }

end_loop:
    // Join all finalized statements with a newline
    std::stringstream result_ss;
    for (const auto& stmt : statements) {
        result_ss << stmt << "\n";
    }

    return result_ss.str();
}
