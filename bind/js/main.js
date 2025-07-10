// 使用 ES6 模块导入。build.sh/build.bat 中的 -s EXPORT_ES6=1 确保这能正常工作。
import createPineVmModule from './pine_vm.js';

// 获取页面上的输出元素，方便后续打印日志
const outputElement = document.getElementById('output');

/**
 * 一个帮助函数，用于将日志同时打印到浏览器控制台和页面上的 <pre> 标签中。
 * @param {string} message 要打印的消息。
 */
function logToPage(message) {
    console.log(message);
    outputElement.textContent += message + '\n';
}

/**
 * 模拟一个数据源类，提供市场数据。
 */
class DataSource {
    constructor() {
        this.numBars = 20;
        // 生成一些模拟的收盘价数据
        this.closeData = Array.from({ length: this.numBars }, (_, i) => 100 + i * 0.5 + Math.sin(i / 3) * 5);
    }
    
    getNumBars() {
        return this.numBars;
    }

   /**
     * 将数据加载到 PineVM 实例中。
     * @param {object} Module - 已加载的 Emscripten 模块。
     * @param {object} vm - PineVM 的实例。
     * @returns {Array} - 一个包含所有创建的 C++ 对象的数组，以便稍后清理内存。
     */
    loadData(Module, vm) {
        // AFTER: Call the factory function we defined, named "new".
        const closeSeries = Module.Series.new();

        closeSeries.name = "close";
        
        // 1. 创建一个 VectorDouble 实例
        const dataVec = new Module.VectorDouble();
        // 2. 遍历普通的 JS 数组，并将每个元素 push_back 到 VectorDouble 中
        for (const val of this.closeData) {
            dataVec.push_back(val);
        }
        
        // 3. 将创建的 VectorDouble 对象赋值给 series.data 属性
        closeSeries.data = dataVec;
        
        // 4. (重要) 清理临时的 VectorDouble 对象，因为数据已经被复制到 C++ Series 对象中
        dataVec.delete();

        vm.registerSeries("close", closeSeries);
        logToPage(`Registered 'close' series with ${this.numBars} bars.`);

        return [closeSeries];
    }
}

/**
 * 主执行函数
 */
async function main() {
    try {
        // 清空初始消息并显示加载状态
        outputElement.textContent = '';
        logToPage("Loading PineVM WebAssembly module...");
        
        // 等待 Wasm 模块加载和初始化
        const Module = await createPineVmModule();
        logToPage("PineVM Wasm module initialized.");
        logToPage("----------------------------------------");

        // --- 模拟 PineScript 编译结果 ---
        // 脚本:
        // sma20 = ta.sma(close, 20)
        // plot(sma20, "My SMA", "blue")
        
        // 常量池 (一个普通的 JS 数组，由我们的 C++ 工厂函数处理)
        const constant_pool = ["close", 20.0, "ta.sma", "My SMA", "sma20", "blue"];
        
        // 对于需要传递到 C++ std::vector 的参数，我们必须创建对应的 Embind 向量类型。
        
        // 创建 VectorString for global_name_pool
        const globalNamesVec = new Module.VectorString();
        globalNamesVec.push_back("sma20");

        // 创建 VectorInstruction for instructions
        const instructionsVec = new Module.VectorInstruction();
        instructionsVec.push_back({ op: Module.OpCode.LOAD_BUILTIN_VAR, operand: 0 }); // LOAD_BUILTIN_VAR "close"
        instructionsVec.push_back({ op: Module.OpCode.PUSH_CONST, operand: 1 });       // PUSH_CONST 20
        instructionsVec.push_back({ op: Module.OpCode.CALL_BUILTIN_FUNC, operand: 2 }); // CALL_BUILTIN_FUNC "ta.sma"
        instructionsVec.push_back({ op: Module.OpCode.RENAME_SERIES, operand: 3 });     // RENAME_SERIES "My SMA"
        instructionsVec.push_back({ op: Module.OpCode.STORE_GLOBAL, operand: 0 });      // STORE_GLOBAL slot 0 ("sma20")
        instructionsVec.push_back({ op: Module.OpCode.PUSH_CONST, operand: 5 });        // PUSH_CONST "blue"
        instructionsVec.push_back({ op: Module.OpCode.CALL_PLOT, operand: 0 });         // CALL_PLOT
        instructionsVec.push_back({ op: Module.OpCode.HALT, operand: 0 });              // HALT

        // 使用我们的工厂函数创建 Bytecode 对象
        const bytecode = Module.makeBytecode(instructionsVec, constant_pool, globalNamesVec);


        // --- 1. 初始化 VM 并注册数据 ---
        const dataSource = new DataSource();
        const vm = new Module.PineVM(dataSource.getNumBars());
        const registeredObjects = dataSource.loadData(Module, vm);

        // --- 2. 初始化并测量 VM 执行时间 ---
        logToPage("\n--- Executing VM ---");
        const startTime = performance.now();

        vm.loadBytecode(bytecode);
        const result = vm.execute();

        const endTime = performance.now();
        const duration = endTime - startTime;

        logToPage("\n--- Execution Time ---");
        logToPage(`VM execution took: ${duration.toFixed(2)} milliseconds`);

        if (result !== 0) {
            logToPage("VM execution failed.");
            return; // 提前退出
        }

        // --- 3. 获取并打印结果 ---
        logToPage("\n--- Accessing Plotted Results from JS ---");
        const plottedSeriesVec = vm.getPlottedSeries();
        for (let i = 0; i < plottedSeriesVec.size(); i++) {
            const plotted = plottedSeriesVec.get(i);
            const series = plotted.series;
            const data = series.data; // data 是一个 VectorDouble

            let dataStr = "";
            for (let j = 0; j < data.size(); j++) {
                const val = data.get(j);
                dataStr += (isNaN(val) ? 'na' : val.toFixed(2)) + ", ";
            }

            logToPage(`Series: ${series.name} (Color: ${plotted.color})`);
            logToPage(`Data: [${dataStr.slice(0, -2)}]`);
        }
        
        // --- 4. 清理内存 ---
        // 这是非常重要的一步，可以防止 WebAssembly 内存泄漏。
        // 我们需要手动 .delete() 所有在 JS 中创建的、对应 C++ 堆上内存的对​​象。
        logToPage("\n--- Cleaning up C++ memory ---");
        vm.delete();
        bytecode.delete(); // value_object 也需要 delete
        globalNamesVec.delete();
        instructionsVec.delete();
        plottedSeriesVec.delete();
        registeredObjects.forEach(obj => obj.delete());
        logToPage("Cleanup complete.");

    } catch (error) {
        logToPage('\n--- AN ERROR OCCURRED ---');
        logToPage(error.message);
        console.error(error);
    }
}

// 运行主函数
main();