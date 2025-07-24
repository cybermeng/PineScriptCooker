import pandas as pd
import numpy as np
import io
from typing import List, Dict, Any

# Note: We have removed 'from . import pine_vm_core' from the top level
# to break the circular import cycle during package initialization.

def compile(code: str) -> str:
    """
    Compiles Hithink/Pine Script code using the C++ backend.
    """
    # Import the C++ module locally, inside the function.
    # This delays the import until after the 'pine_vm' package is fully initialized.
    from . import pine_vm_core

    # 1. 创建 HithinkCompiler 的实例
    compiler = pine_vm_core.HithinkCompiler()
    # 2. 调用其 compile 方法
    bytecode = compiler.compile(code)
    if compiler.hadError():
        # 如果需要，可以添加更复杂的错误处理
        raise RuntimeError(f"Compilation failed: {bytecode}")
    return bytecode

def run(bytecode: str, data: Dict[str, List[float]]) -> pd.DataFrame:
    """
    Runs compiled script bytecode with given data using the C++ backend.

    Args:
        bytecode (str): The compiled bytecode string.
        data (Dict[str, List[float]]): A dictionary where keys are series names
                                        (e.g., 'close', 'open') and values are lists of floats.

    Returns:
        pd.DataFrame: A pandas DataFrame containing the output series from the script execution.
    """
    # Import the C++ module locally here as well.
    from . import pine_vm_core

    if not data:
        raise ValueError("Input data cannot be empty.")

    # 1. 确定 K 线总数
    # 取第一个序列的长度作为 K 线总数
    first_series = next(iter(data.values()))
    total_bars = len(first_series)
    if total_bars == 0:
        return pd.DataFrame() # 如果没有数据，返回空的 DataFrame

    # 2. 创建 PineVM 实例
    vm = pine_vm_core.PineVM()

    # 3. 注册所有输入序列
    for name, series_list in data.items():
        if len(series_list) != total_bars:
            raise ValueError(f"All series must have the same length. Series '{name}' has length {len(series_list)}, expected {total_bars}.")
        # 将 Python 列表转换为 NumPy 数组，因为我们的绑定需要它
        np_array = np.array(series_list, dtype=np.float64)
        vm.register_series(name, np_array)

    # 4. 加载并执行字节码
    vm.load_bytecode(bytecode)
    exit_code = vm.execute(total_bars)
    if exit_code != 0:
        raise RuntimeError("VM execution failed: " + vm.error_message())

    # 5. 获取结果并解析
    # C++ 端返回一个 CSV 格式的字符串
    result_csv_string = vm.get_plotted_results_as_string()

    if not result_csv_string.strip():
        # 如果脚本没有绘制任何内容，则返回空 DataFrame
        return pd.DataFrame()

    # 使用 pandas 将 CSV 字符串直接读入 DataFrame
    # io.StringIO 将字符串模拟成文件
    result_df = pd.read_csv(io.StringIO(result_csv_string))

    # 如果有 'time' 列，通常我们希望它作为索引
    if 'time' in result_df.columns:
        result_df['time'] = pd.to_datetime(result_df['time'].astype(str), format='%Y%m%d', errors='coerce')
        result_df.set_index('time', inplace=True)

    return result_df