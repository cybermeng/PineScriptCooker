#include <emscripten/bind.h>
#include "../../PineVM.cpp"

using namespace emscripten;

// --- 辅助函数和工厂函数 (无需修改) ---

Value from_js_val(const val& js_val) {
    std::string type = js_val.typeOf().as<std::string>();
    if (type == "number") return js_val.as<double>();
    if (type == "boolean") return js_val.as<bool>();
    if (type == "string") return js_val.as<std::string>();
    return std::monostate{};
}

Bytecode make_bytecode(const std::vector<Instruction>& instructions,
                       const val& js_constant_pool,
                       const std::vector<std::string>& global_name_pool) {
    Bytecode bc;
    bc.instructions = instructions;
    bc.global_name_pool = global_name_pool;
    const auto len = js_constant_pool["length"].as<unsigned>();
    bc.constant_pool.reserve(len);
    for (unsigned i = 0; i < len; ++i) {
        bc.constant_pool.push_back(from_js_val(js_constant_pool[i]));
    }
    return bc;
}

// --- Emscripten 绑定定义 ---
EMSCRIPTEN_BINDINGS(pine_vm_module) {
    
    register_vector<double>("VectorDouble");
    register_vector<Instruction>("VectorInstruction");
    register_vector<PlottedSeries>("VectorPlottedSeries");
    register_vector<std::string>("VectorString");

    enum_<OpCode>("OpCode")
        .value("PUSH_CONST", OpCode::PUSH_CONST).value("POP", OpCode::POP)
        .value("ADD", OpCode::ADD).value("SUB", OpCode::SUB).value("MUL", OpCode::MUL).value("DIV", OpCode::DIV)
        .value("LESS", OpCode::LESS).value("LESS_EQUAL", OpCode::LESS_EQUAL).value("EQUAL_EQUAL", OpCode::EQUAL_EQUAL)
        .value("BANG_EQUAL", OpCode::BANG_EQUAL).value("GREATER", OpCode::GREATER).value("GREATER_EQUAL", OpCode::GREATER_EQUAL)
        .value("LOAD_BUILTIN_VAR", OpCode::LOAD_BUILTIN_VAR).value("LOAD_GLOBAL", OpCode::LOAD_GLOBAL)
        .value("STORE_GLOBAL", OpCode::STORE_GLOBAL).value("RENAME_SERIES", OpCode::RENAME_SERIES)
        .value("STORE_AND_PLOT_GLOBAL", OpCode::STORE_AND_PLOT_GLOBAL).value("JUMP_IF_FALSE", OpCode::JUMP_IF_FALSE)
        .value("JUMP", OpCode::JUMP).value("CALL_BUILTIN_FUNC", OpCode::CALL_BUILTIN_FUNC)
        .value("CALL_PLOT", OpCode::CALL_PLOT).value("HALT", OpCode::HALT);

    class_<Series>("Series")
        // This sets up the shared_ptr type for the class.
        .smart_ptr<std::shared_ptr<Series>>("shared_ptr<Series>")
        // This creates a static factory function on the JS class: Module.Series.new()
        .class_function("new", &std::make_shared<Series>)
        .property("name", &Series::name)
        .property("data", &Series::data)
        .function("getCurrent", &Series::getCurrent)
        .function("setCurrent", &Series::setCurrent)
        .function("setName", &Series::setName);

    value_object<Instruction>("Instruction")
        .field("op", &Instruction::op)
        .field("operand", &Instruction::operand);
    
    value_object<PlottedSeries>("PlottedSeries")
        .field("series", &PlottedSeries::series)
        .field("color", &PlottedSeries::color);

    value_object<Bytecode>("Bytecode")
        .field("instructions", &Bytecode::instructions)
        .field("global_name_pool", &Bytecode::global_name_pool);

    function("makeBytecode", &make_bytecode);

    // 绑定核心类 PineVM (包含修正)
    class_<PineVM>("PineVM")
        .constructor<int>()
        .function("loadBytecode", &PineVM::loadBytecode)
        .function("execute", &PineVM::execute)
        // AFTER: Revert to the simple, direct binding. It will now work
        //        because JS will be passing a valid shared_ptr.
        .function("registerSeries", &PineVM::registerSeries)
        .function("getPlottedSeries", &PineVM::getPlottedSeries, allow_raw_pointers())
        .function("printPlottedResults", &PineVM::printPlottedResults)
        .function("writePlottedResults", &PineVM::writePlottedResults)
        .class_function("bytecodeToTxt", &PineVM::bytecodeToTxt, allow_raw_pointers())
        .class_function("txtToBytecode", &PineVM::txtToBytecode, allow_raw_pointers());
}