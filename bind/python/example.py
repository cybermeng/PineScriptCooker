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
# 创建一个简单的正弦波加上一些趋势和噪声，以模拟股价
print("--- 1. Preparing mock data ---")
total_bars = 200
time_index = pd.to_datetime(pd.date_range(start='2023-01-01', periods=total_bars, freq='D'))
trend = np.linspace(100, 150, total_bars)
seasonality = 10 * np.sin(np.linspace(0, 4 * np.pi, total_bars))
noise = np.random.normal(0, 2, total_bars)
close_prices = trend + seasonality + noise

# 创建一个简单的 DataFrame
mock_data_df = pd.DataFrame({
    'time': time_index,
    'open': close_prices - np.random.uniform(0, 2, total_bars),
    'high': close_prices + np.random.uniform(0, 2, total_bars),
    'low': close_prices - np.random.uniform(2, 4, total_bars),
    'close': close_prices,
    'volume': np.random.randint(10000, 50000, total_bars)
})

# 将 time 列转换为 Hithink/通达信期望的 YYYYMMDD 整数格式
mock_data_df['time_int'] = mock_data_df['time'].dt.strftime('%Y%m%d').astype(int)

print(f"Generated {len(mock_data_df)} bars of data.")
print("Sample data:")
print(mock_data_df.head())

# 将 DataFrame 转换为 C++ 后端期望的字典格式
# 注意：key 必须是小写，以匹配 VM 中的内置变量名
# 我们的编译器会将 Hithink 的大写 'CLOSE' 映射为小写的 'close'
input_data_dict = {
    'time': mock_data_df['time_int'].tolist(),
    'open': mock_data_df['open'].tolist(),
    'high': mock_data_df['high'].tolist(),
    'low': mock_data_df['low'].tolist(),
    'close': mock_data_df['close'].tolist(),
    'volume': mock_data_df['volume'].tolist(),
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
    result_df.index = mock_data_df.index[-len(result_df):]
    full_df = mock_data_df.join(result_df)

except RuntimeError as e:
    print(f"VM execution failed: {e}")
    exit()
except Exception as e:
    print(f"An unexpected error occurred during execution: {e}")
    exit()


# 5. 可视化结果进行验证
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