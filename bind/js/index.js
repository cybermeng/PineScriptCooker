// 使用 require 加载 Emscripten 生成的模块
const createPineVmModule = require('./pine_vm.js');

// 模拟一个数据源
class DataSource {
    constructor() {
        this.numBars = 20;
        this.closeData = Array.from({ length: this.numBars }, (_, i) => 100 + i * 0.5 + Math.sin(i / 3) * 5);
        // ... 其他数据如 open, high, low
    }
    
    getNumBars() {
        return this.numBars;
    }

    // loadData 现在接收 JS 版本的 vm 和 Series 对象
    loadData(Module, vm) {
        const closeSeries = new Module.Series();
        closeSeries.name = "close";
        // Embind 自动处理 JS array -> std::vector<double> 的转换
        closeSeries.data = this.closeData; 
        
        vm.registerSeries("close", closeSeries);
        console.log("Registered 'close' series with", this.numBars, "bars.");

        // 返回创建的对象以便稍后清理内存
        return [closeSeries];
    }
}

// 脚本的主要逻辑
async function main() {
    // 等待 Wasm 模块加载和初始化
    const Module = await createPineVmModule();
    console.log("PineVM Wasm module initialized.");

    // --- 模拟 PineScript 编译结果 ---
    // 脚本:
    // sma20 = ta.sma(close, 20)
    // plot(sma20, "My SMA", "blue")
    
    // 常量池
    const constant_pool = [
        "close",       // 0: series name
        20.0,          // 1: sma length
        "ta.sma",      // 2: function name
        "My SMA",      // 3: new series name for plot
        "sma20",       // 4: global variable name
        "blue",        // 5: plot color
    ];
    
    // 全局变量名池
    const global_name_pool = ["sma20"];

    // 指令序列
    const instructions = [
        // sma20 = ta.sma(close, 20)
        new Module.Instruction({ op: Module.OpCode.LOAD_BUILTIN_VAR, operand: 0 }), // LOAD_BUILTIN_VAR "close"
        new Module.Instruction({ op: Module.OpCode.PUSH_CONST, operand: 1 }),       // PUSH_CONST 20
        new Module.Instruction({ op: Module.OpCode.CALL_BUILTIN_FUNC, operand: 2 }), // CALL_BUILTIN_FUNC "ta.sma"
        new Module.Instruction({ op: Module.OpCode.RENAME_SERIES, operand: 3 }),     // RENAME_SERIES "My SMA"
        new Module.Instruction({ op: Module.OpCode.STORE_GLOBAL, operand: 0 }),      // STORE_GLOBAL slot 0 ("sma20")
        
        // plot(sma20, "My SMA", "blue")
        // 为了简化，我们假定 plot 只接受 series 和 color
        // 并且我们已经重命名了序列，所以再次加载它
        // LOAD_GLOBAL "sma20" - 实际字节码会更复杂，这里简化
        // PUSH_CONST "blue"
        // CALL_PLOT
        new Module.Instruction({ op: Module.OpCode.PUSH_CONST, operand: 5 }), // PUSH_CONST "blue"
        new Module.Instruction({ op: Module.OpCode.CALL_PLOT, operand: 0 }), // operand 不使用，但需要一个

        new Module.Instruction({ op: Module.OpCode.HALT, operand: 0 })
    ];

    // 使用工厂函数创建 Bytecode 对象
    const bytecode = Module.makeBytecode(instructions, constant_pool, global_name_pool);


    // --- 1. 初始化 VM 并注册数据 ---
    const dataSource = new DataSource();
    const vm = new Module.PineVM(dataSource.getNumBars());
    const registeredObjects = dataSource.loadData(Module, vm);

    // --- 2. 初始化并测量 VM 执行时间 ---
    console.log("\n--- Executing VM ---");
    const startTime = process.hrtime.bigint();

    vm.loadBytecode(bytecode);
    const result = vm.execute();

    const endTime = process.hrtime.bigint();
    const duration = (endTime - startTime) / 1000000n; // 转换为毫秒

    console.log("\n--- Execution Time ---");
    console.log(`VM execution took: ${duration} milliseconds`);

    if (result !== 0) {
        console.error("VM execution failed.");
        return;
    }

    // --- 3. 获取并打印结果 ---
    // printPlottedResults 会在 wasm 的 stdout (即node控制台) 中打印
    vm.printPlottedResults();

    // 也可以在JS中直接访问数据
    console.log("\n--- Accessing Plotted Results from JS ---");
    const plottedSeriesVec = vm.getPlottedSeries();
    for (let i = 0; i < plottedSeriesVec.size(); i++) {
        const plotted = plottedSeriesVec.get(i);
        const series = plotted.series;
        const data = series.data; // series.data 是一个 VectorDouble

        let dataStr = "";
        for (let j = 0; j < data.size(); j++) {
            const val = data.get(j);
            dataStr += (isNaN(val) ? 'na' : val.toFixed(2)) + ", ";
        }

        console.log(`Series: ${series.name} (Color: ${plotted.color})`);
        console.log(`Data: [${dataStr.slice(0, -2)}]`);
        
        // 清理 C++ 侧返回的 series 对象的内存 (如果它是通过值返回的副本)
        // 在我们的例子中，plotted.series 是一个共享指针，其生命周期由C++管理，
        // 但最好养成清理的习惯。然而，这里的 plotted 对象本身是临时的，不需要 delete。
    }
    
    vm.writePlottedResults("plotted_results.csv");


    // --- 4. 清理内存 ---
    console.log("\nCleaning up C++ memory...");
    vm.delete();
    // bytecode 是一个值对象，由 JS GC 管理，不需要 delete
    // registeredObjects 包含共享指针，当JS侧没有引用时，其引用计数会减少
    registeredObjects.forEach(obj => obj.delete());
    instructions.forEach(inst => inst.delete()); // Instruction 是值对象，也由JS GC管理，但显式调用无害
    plottedSeriesVec.delete();
}

main().catch(console.error);