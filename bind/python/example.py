import pandas as pd
import numpy as np
import matplotlib.pyplot as plt
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

# 将 time 列转换为 Hithink/通达信期望的 YYYYMMDD 整数格式
# 这行代码与你的mock数据处理逻辑完全相同
data_df['time_int'] = data_df['time'].dt.strftime('%Y%m%d').astype(int)

# 将 DataFrame 转换为 C++ 后端期望的字典格式
# 注意：key 必须是小写，以匹配 VM 中的内置变量名
# 我们的编译器会将 Hithink 的大写 'CLOSE' 映射为小写的 'close'
input_data_dict = {
    'time': data_df['time_int'].tolist(),
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

    # 将结果与原始数据合并，方便绘图
    # 确保索引对齐
    result_df.index = data_df.index[-len(result_df):]
    full_df = data_df.join(result_df)

except RuntimeError as e:
    print(f"VM execution failed: {e}")
    exit()
except Exception as e:
    print(f"An unexpected error occurred during execution: {e}")
    exit()


# 5. 打印结果进行验证
print("\n--- 4. Plotting results for verification ---")
if 'MA20_OUTPUT' not in full_df.columns:
    print("Warning: 'MA20_OUTPUT' not found in results. Skipping plot.")
else:
    fig, (ax1, ax2) = plt.subplots(2, 1, figsize=(15, 10), sharex=True, gridspec_kw={'height_ratios': [3, 1]})
    fig.suptitle('Test Case Verification: CLOSE vs SMA(20) and Cross Signal', fontsize=16)

    # 上图：收盘价和计算出的 SMA
    ax1.plot(full_df['time'], full_df['close'], label='Close Price', color='blue', alpha=0.8)
    ax1.plot(full_df['time'], full_df['MA20_OUTPUT'], label='MA(20) from PineVM', color='orange', linestyle='--')
    ax1.set_title('Price and Moving Average')
    ax1.set_ylabel('Price')
    ax1.legend()
    ax1.grid(True)

    # 下图：交叉信号
    # 找到信号为1的点
    cross_up_points = full_df[full_df['CROSS_SIGNAL'] == 1]
    ax2.plot(full_df['time'], full_df['CROSS_SIGNAL'], label='Cross Signal (0 or 1)', color='gray', alpha=0.5, drawstyle='steps-post')
    # 在交叉点上绘制红色圆圈，以便清晰看到
    if not cross_up_points.empty:
        ax1.scatter(cross_up_points['time'], cross_up_points['close'], color='red', s=100, zorder=5, label='Cross Up Signal')
        ax2.scatter(cross_up_points['time'], cross_up_points['CROSS_SIGNAL'], color='red', s=100, zorder=5)

    ax1.legend() # 再次调用以包含 scatter 的标签
    ax2.set_title('CROSS_SIGNAL Output')
    ax2.set_xlabel('Date')
    ax2.set_ylabel('Signal Value')
    ax2.set_yticks([0, 1])
    ax2.grid(True)

    plt.tight_layout(rect=[0, 0, 1, 0.96])
    print("Displaying plot. Close the plot window to exit.")
    plt.show()    