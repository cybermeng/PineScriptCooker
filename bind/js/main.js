// 从 Emscripten 生成的文件中导入模块加载函数
import createPineVmModule from './public/pine_vm.js';

// --- DOM Element Selection ---
const statusElement = document.getElementById('status');
const executeButton = document.getElementById('executeButton');
const bytecodeInput = document.getElementById('bytecodeInput');
const dataInput = document.getElementById('dataInput');
const outputElement = document.getElementById('output');
const controlPanel = document.getElementById('control-panel');
const panelToggle = document.getElementById('panel-toggle');
const chartDom = document.getElementById('chart-main');

// --- ECharts Initialization ---
let myChart = echarts.init(chartDom);
myChart.showLoading();

// --- UI Event Handlers ---
panelToggle.addEventListener('click', () => {
    controlPanel.classList.toggle('collapsed');
    panelToggle.textContent = controlPanel.classList.contains('collapsed') ? '>' : '<';
    setTimeout(() => myChart.resize(), 350);
});

// --- Data Input Drag and Drop ---
dataInput.addEventListener('dragover', (event) => {
    event.preventDefault(); // Necessary to allow drop
    dataInput.classList.add('drag-over');
});

dataInput.addEventListener('dragleave', () => {
    dataInput.classList.remove('drag-over');
});

dataInput.addEventListener('drop', (event) => {
    event.preventDefault();
    dataInput.classList.remove('drag-over');

    if (event.dataTransfer.files && event.dataTransfer.files.length > 0) {
        const file = event.dataTransfer.files[0];
        const reader = new FileReader();

        reader.onload = (e) => {
            dataInput.value = e.target.result;
        };

        reader.onerror = (e) => {
            console.error("Error reading file:", e);
            alert("Error reading the dropped file. See console for details.");
        };

        reader.readAsText(file);
    }
});


// --- Data Processing and Charting Logic ---

/**
 * Parses raw financial data (JSON lines) into a format for ECharts.
 * Sorts data by date ascending and extracts K-line, volume, and dates.
 * @param {string} financialData - String of newline-separated JSON objects.
 * @returns {{dates: string[], kline: number[][], volumes: number[], volumeColors: string[], code: string}}
 */
function parseFinancialData(financialData) {
    const upColor = '#00da3c';
    const downColor = '#ec0000';

    const lines = financialData.trim().split('\n');
    if (lines.length === 0 || lines[0].trim() === '') {
        throw new Error("Input data is empty.");
    }
    const data = lines.map(line => JSON.parse(line));
    
    // Sort by date ascending to ensure correct calculations
    data.sort((a, b) => a.trade_date - b.trade_date);
    
    const dates = [];
    const klineData = [];
    const volumes = [];
    const volumeColors = [];

    for (let i = 0; i < data.length; i++) {
        const item = data[i];
        const dateStr = item.trade_date.toString();
        dates.push(`${dateStr.slice(0, 4)}-${dateStr.slice(4, 6)}-${dateStr.slice(6, 8)}`);
        
        const open = item['7'];
        const close = item['11'];
        const low = item['9'];
        const high = item['8'];
        klineData.push([open, close, low, high]);

        volumes.push(item['13']); // Key "13" is our volume
        
        // Determine volume bar color: green if close >= open, red otherwise
        volumeColors.push(close >= open ? upColor : downColor);
    }
    
    const code = data.length > 0 ? data[0].code : 'STOCK';

    return { dates, kline: klineData, volumes, volumeColors, code };
}


/**
 * Renders a 3-part chart: K-Line (top), Volume (middle), Indicators (bottom).
 * @param {echarts.ECharts} chartInstance
 * @param {{dates: string[], kline: number[][], volumes: number[], volumeColors: string[], code: string}} marketData
 * @param {object | null} indicatorData - Parsed column-oriented JSON from WASM output.
 */
function renderChart(chartInstance, marketData, indicatorData = null) {
    const upColor = '#00da3c';
    const downColor = '#ec0000';

    const indicatorNames = indicatorData ? Object.keys(indicatorData) : [];
    const hasIndicators = indicatorNames.length > 0;

    // --- Base Series ---
    const legendData = ['K-Line', 'Volume'];
    const series = [
        // 0: K-Line Series
        {
            name: 'K-Line',
            type: 'candlestick',
            data: marketData.kline,
            itemStyle: { color: upColor, color0: downColor, borderColor: upColor, borderColor0: downColor },
            xAxisIndex: 0,
            yAxisIndex: 0
        },
        // 1: Volume Series
        {
            name: 'Volume',
            type: 'bar',
            data: marketData.volumes,
            itemStyle: {
                color: (params) => marketData.volumeColors[params.dataIndex]
            },
            xAxisIndex: 1,
            yAxisIndex: 1
        }
    ];

    // --- Dynamic Layout Configuration based on whether indicators exist ---
    const grid = [
        { left: '10%', right: '8%', top: '8%', height: '45%' },  // Grid 0: K-Line
        { left: '10%', right: '8%', top: '58%', height: '15%' } // Grid 1: Volume
    ];
    const xAxis = [
        { // Axis 0 for K-Line
            type: 'category', gridIndex: 0, data: marketData.dates, scale: true, boundaryGap: false,
            axisLine: { onZero: false }, splitLine: { show: false }, axisLabel: { show: false },
            min: 'dataMin', max: 'dataMax'
        },
        { // Axis 1 for Volume
            type: 'category', gridIndex: 1, data: marketData.dates, scale: true, boundaryGap: false,
            axisLine: { onZero: false }, splitLine: { show: false }, axisLabel: { show: false },
            min: 'dataMin', max: 'dataMax'
        }
    ];
    const yAxis = [
        { scale: true, gridIndex: 0, splitArea: { show: true } }, // Axis 0 for K-Line
        { scale: true, gridIndex: 1, splitNumber: 3, axisLine: { show: false }, axisTick: { show: false }, axisLabel: { show: false }, splitLine: { show: false } } // Axis 1 for Volume
    ];
    const dataZoomXAxisIndex = [0, 1];


    if (hasIndicators) {
        // Add a third grid for indicators
        grid.push({ left: '10%', right: '8%', top: '78%', height: '15%' }); // Grid 2: Indicators
        
        // Add X and Y axes for the new grid
        xAxis.push({ // Axis 2 for Indicators
            type: 'category', gridIndex: 2, data: marketData.dates, scale: true, boundaryGap: false,
            axisLine: { onZero: false }, splitLine: { show: false },
            min: 'dataMin', max: 'dataMax'
        });
        yAxis.push({ scale: true, gridIndex: 2, splitNumber: 4 }); // Axis 2 for Indicators
        dataZoomXAxisIndex.push(2);

        // Create and assign all indicator series to the bottom grid
        indicatorNames.forEach(name => {
            const data = indicatorData[name];
            if (!Array.isArray(data)) return;
            legendData.push(name);
            
            const seriesType = name.toLowerCase().includes('macd') ? 'bar' : 'line';
            const indicatorSeries = {
                name,
                data,
                type: seriesType,
                xAxisIndex: 2, // Assign to bottom grid
                yAxisIndex: 2
            };

            if (seriesType === 'line') {
                indicatorSeries.smooth = true;
            } else { // bar
                indicatorSeries.itemStyle = { color: (params) => (params.value >= 0 ? upColor : downColor) };
            }
            series.push(indicatorSeries);
        });
    }
    
    // --- Assemble and Set Final Chart Option ---
    const option = {
        title: { text: `${marketData.code} Chart`, left: 'center' },
        tooltip: { trigger: 'axis', axisPointer: { type: 'cross', link: [{ xAxisIndex: 'all' }] } },
        legend: { data: legendData, top: 30 },
        axisPointer: { link: [{ xAxisIndex: 'all' }] },
        grid,
        xAxis,
        yAxis,
        dataZoom: [
            { type: 'inside', xAxisIndex: dataZoomXAxisIndex, start: 50, end: 100 },
            { show: true, type: 'slider', xAxisIndex: dataZoomXAxisIndex, top: '94%', start: 50, end: 100 }
        ],
        series
    };
    
    chartInstance.hideLoading();
    chartInstance.setOption(option, true);
}


/**
 * Parses the string result from WASM, assuming the first column is a time/date
 * and should be ignored, while subsequent columns are indicator data.
 * @param {string} wasmResultString - The raw string from the WASM module.
 * @returns {object} - A column-oriented JSON object of indicators. Returns an empty object on failure.
 */
function parseWasmResult(wasmResultString) {
  if (!wasmResultString || typeof wasmResultString !== 'string' || wasmResultString.trim() === '') {
    console.warn("Wasm result string is empty or invalid.");
    return {};
  }

  // First, try to parse as JSON
  try {
    const jsonData = JSON.parse(wasmResultString);
    if (typeof jsonData === 'object' && jsonData !== null && !Array.isArray(jsonData)) {
        const keys = Object.keys(jsonData);
        if (keys.length > 0) {
            // Assume the first key is the time column and remove it.
            console.log(`JSON format detected. Ignoring first column: "${keys[0]}"`);
            delete jsonData[keys[0]];
        }
        return jsonData;
    }
  } catch (jsonError) {
    // Fall through to CSV parsing if JSON parsing fails
  }

  // Second, try to parse as CSV
  try {
    const lines = wasmResultString.trim().split(/\r?\n/);
    // Need at least a header row with a time column and one indicator column.
    if (lines.length < 2) return {}; 
    
    const headers = lines[0].split(',').map(h => h.trim());
    if (headers.length < 2) return {}; // Not enough columns to be useful

    const columnData = {};
    // Initialize data arrays, skipping the first header (time column)
    for (let j = 1; j < headers.length; j++) {
        columnData[headers[j]] = [];
    }

    // Populate data, starting from the second row (data)
    for (let i = 1; i < lines.length; i++) {
        const values = lines[i].split(',');
        // For each indicator column (skip the first column)
        for (let j = 1; j < headers.length; j++) {
            const headerName = headers[j];
            const rawValue = values[j] ? values[j].trim() : null;
            const finalValue = !isNaN(parseFloat(rawValue)) && isFinite(rawValue) ? parseFloat(rawValue) : rawValue;
            columnData[headerName].push(finalValue);
        }
    }
    
    console.log(`CSV format detected. Ignoring first column: "${headers[0]}"`);
    return columnData;

  } catch (csvError) {
    console.error("Failed to parse string as both JSON and CSV.", { wasmResultString, error: csvError.message });
    return {};
  }
}

// --- Main Application Logic ---
createPineVmModule()
    .then(Module => {
        statusElement.textContent = 'WebAssembly Module Loaded!';
        statusElement.style.color = 'green';
        executeButton.disabled = false;
        myChart.hideLoading();
        myChart.setOption({ title: { text: 'Ready to Execute', left: 'center', top: 'center' } });

        const runPineCalculationJS = Module.cwrap('run_pine_calculation', 'string', ['string', 'string']);

        executeButton.addEventListener('click', () => {
            executeButton.disabled = true;
            outputElement.textContent = 'Processing...';
            myChart.showLoading();

            setTimeout(() => {
                let marketData;
                let indicatorData = null;

                try {
                    const financialData = dataInput.value;
                    marketData = parseFinancialData(financialData);
                    
                    try {
                        const bytecode = bytecodeInput.value;
                        const startTime = performance.now();
                        const wasmResultString = runPineCalculationJS(bytecode, financialData);
                        const endTime = performance.now();
                        outputElement.textContent = wasmResultString;
                        console.log(`WASM execution time: ${(endTime - startTime).toFixed(2)} ms`);
                        indicatorData = parseWasmResult(wasmResultString);
                    } catch (indicatorError) {
                        outputElement.textContent += `\n\n--- WARNING: Indicator calculation failed. ---\n${indicatorError.message}`;
                        console.warn("Indicator calculation/parsing failed:", indicatorError);
                    }
                    
                    renderChart(myChart, marketData, indicatorData);

                } catch (criticalError) {
                    outputElement.textContent = `A critical error occurred: ${criticalError.message}\n\n${criticalError.stack}`;
                    myChart.hideLoading();
                    myChart.setOption({ title: { text: 'Error: Could not parse financial data.', left: 'center', top: 'center', textStyle: {color: 'red'} } }, true);
                    console.error(criticalError);
                } finally {
                    executeButton.disabled = false;
                }
            }, 10);
        });
    })
    .catch(error => {
        statusElement.textContent = 'Error loading WebAssembly module. Check console.';
        statusElement.style.color = 'red';
        myChart.hideLoading();
        myChart.setOption({ title: { text: 'WASM Load Failed', left: 'center', top: 'center', textStyle: {color: 'red'} } });
        console.error("WASM Loading Error:", error);
    });

window.addEventListener('resize', () => myChart.resize());