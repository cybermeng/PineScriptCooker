// 从 Emscripten 生成的文件中导入模块加载函数
import createPineVmModule from './public/pine_vm.js';

// 获取所有需要的 DOM 元素
const statusElement = document.getElementById('status');
const executeButton = document.getElementById('executeButton');
const bytecodeInput = document.getElementById('bytecodeInput');
const dataInput = document.getElementById('dataInput');
const outputElement = document.getElementById('output');

// 使用我们导入的函数来初始化 WebAssembly 模块
createPineVmModule()
    .then(Module => {
        statusElement.textContent = 'WebAssembly Module Loaded Successfully!';
        statusElement.style.color = 'green';
        executeButton.disabled = false;

        // 使用 cwrap 来创建一个包装函数
        // 语法: cwrap('c_function_name', 'return_type', ['arg1_type', 'arg2_type'])
        // 'string' 类型会自动处理 JS string -> C char* 的转换
        const runPineCalculationJS = Module.cwrap(
            'run_pine_calculation', 
            'string', 
            ['string', 'string']
        );

        executeButton.addEventListener('click', () => {
            executeButton.disabled = true;
            outputElement.textContent = 'Executing... Please wait.';

            setTimeout(() => {
                try {
                    const bytecode = bytecodeInput.value;
                    const financialData = dataInput.value;
                    
                    const startTime = performance.now();
                    // 直接调用我们包装好的JS函数
                    const result = runPineCalculationJS(bytecode, financialData);
                    const endTime = performance.now();
                    
                    outputElement.textContent = result;
                    console.log(`JS-side timing: ${(endTime - startTime).toFixed(2)} ms`);

                } catch (error) {
                    outputElement.textContent = `An error occurred in JavaScript: ${error}\n\n${error.stack}`;
                    console.error(error);
                } finally {
                    executeButton.disabled = false;
                }
            }, 10);
        });
    })
    .catch(error => {
        statusElement.textContent = 'Error loading WebAssembly module. Check console.';
        statusElement.style.color = 'red';
        console.error("WASM Loading Error:", error);
    });