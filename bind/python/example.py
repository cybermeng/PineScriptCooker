import pandas as pd
import numpy as np
from pine_vm import compile, run

# 1. 定义 Hithink/Pine 脚本代码
# 这个脚本会计算20周期的简单移动平均线 (SMA)
# 并绘制一个信号：当收盘价上穿SMA时，信号为1，否则为0
hithink_code = """
// 计算20周期的移动平均线
MA20: MA(CLOSE, 20);

// 定义一个交叉信号
// CROSS(A, B) 在 Hithink 中通常表示 A 上穿 B
// 我们用 C > MA20 AND REF(C, 1) <= REF(MA20, 1) 来模拟
IS_CROSS_UP: C > MA20 AND REF(C, 1) <= REF(MA20, 1);

// 将交叉信号绘制出来，以便我们能看到结果
// 当 IS_CROSS_UP 为真 (1) 时，绘制一个点，否则为假 (0)
CROSS_SIGNAL: IF(IS_CROSS_UP, 1, 0);

// 我们也可以直接绘制 MA20
// Hithink 中的 ':' 是输出和绘图的语法
// 我们的编译器将其映射为 STORE_AND_PLOT_GLOBAL
MA20_OUTPUT: MA20;
"""

# 2. 准备模拟的K线数据
# --- 1. 从文件读取数据来替换mock数据 ---
print("--- 1. Reading data from file ---")

# 定义文件名
file_path = 'stock_data.csv'

# 使用 pandas.read_csv 读取数据
# parse_dates=['time'] 会自动将 'time' 列从字符串转换为 datetime 对象，非常方便
try:
    data_df = pd.read_csv(file_path, parse_dates=['time'])
except FileNotFoundError:
    print(f"错误: 文件 '{file_path}' 未找到。请确保文件存在于正确的路径。")
    exit() # 如果文件不存在，则退出脚本

# 你的原始数据中没有 'volume' 列，但mock数据中有。
# 为了保持DataFrame结构一致，我们可以添加一个默认的 'volume' 列。
# 这里我们用0填充，你也可以根据需要使用其他值，如 1 或 np.nan。
if 'volume' not in data_df.columns:
    data_df['volume'] = 0

# 将 time 列转换为Unix Timestamp
#data_df['time_int'] = pd.to_datetime(data_df['time'], format='%Y%m%d').astype(int)
data_df['time_int'] = (pd.to_datetime(data_df['time']).astype(int) // 10**9)

# YYYYMMDD 整数格式
data_df['date_int'] = data_df['time'].dt.strftime('%Y%m%d').astype(int)

print("\n转换后的数据类型:")
print(data_df.dtypes)
print("\n转换后的 DataFrame:")
print(data_df)

# 将 DataFrame 转换为 C++ 后端期望的字典格式
# 注意：key 必须是小写，以匹配 VM 中的内置变量名
# 我们的编译器会将 Hithink 的大写 'CLOSE' 映射为小写的 'close'
input_data_dict = {
    'time': data_df['time_int'].tolist(),
    'date': data_df['date_int'].tolist(),
    'open': data_df['open'].tolist(),
    'high': data_df['high'].tolist(),
    'low': data_df['low'].tolist(),
    'close': data_df['close'].tolist(),
    'volume': data_df['volume'].tolist(),
}

# 3. 编译 Hithink 脚本
print("\n--- 2. Compiling Hithink script ---")
try:
    bytecode = compile(hithink_code)
    print("Compilation successful!")
    print("Bytecode:\n" + bytecode)
except RuntimeError as e:
    print(f"Compilation failed: {e}")
    exit()

# 4. 运行虚拟机
print("\n--- 3. Running PineVM ---")
try:
    # 运行 VM 并获取结果
    result_df = run(bytecode, input_data_dict)
    print("VM execution successful!")
    print("Result DataFrame (last 10 rows):")
    print(result_df.tail(10))

except RuntimeError as e:
    print(f"VM execution failed: {e}")
    exit()
except Exception as e:
    print(f"An unexpected error occurred during execution: {e}")
    exit()

