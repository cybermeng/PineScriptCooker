package com.pinevm;

import java.io.Closeable;

/**
 * PineVM 的 Java 包装器，用于执行 Hithink 脚本的字节码。
 * 这个类管理着一个底层的 C++ PineVM 实例的生命周期。
 * 实现了 {@link Closeable} 接口，强烈建议在 try-with-resources 语句中使用。
 *
 * <pre>{@code
 * try (PineVM vm = new PineVM()) {
 *     // ... 使用 vm ...
 * } catch (Exception e) {
 *     // ... 异常处理 ...
 * }
 * }</pre>
 *
 * 这个类不是线程安全的。请勿在多个线程中共享同一个 PineVM 实例。
 */
public class PineVM implements Closeable {

    // 加载包含本地方法实现的共享库
    static {
        System.loadLibrary("pinevm_jni");
    }

    /**
     * 存储指向底层 C++ PineVM 对象的指针（句柄）。
     * 值为 0 表示该对象已被销毁。
     */
    private long nativeHandle;

    /**
     * 创建并初始化一个新的 PineVM 实例。
     */
    public PineVM() {
        this.nativeHandle = nativeCreate();
    }

    /**
     * 加载要执行的字节码的文本表示，并重置VM状态。
     *
     * @param bytecodeText 由 {@link HithinkCompiler#compile(String)} 生成的字节码文本。
     */
    public void loadBytecode(String bytecodeText) {
        checkNativeHandle();
        nativeLoadBytecode(nativeHandle, bytecodeText);
    }

    /**
     * 更新或设置一个输入序列的数据。
     *
     * @param name   序列的名称 (例如, "close", "open", "high", "low", "volume", "time")。
     *               名称必须与 Hithink 脚本中使用的内置变量名（或其映射）匹配。
     * @param data   包含序列数据的 double 数组。
     */
    public void updateSeries(String name, double[] data) {
        checkNativeHandle();
        if (name == null || name.isEmpty()) {
            throw new IllegalArgumentException("Series name cannot be null or empty.");
        }
        if (data == null) {
            throw new IllegalArgumentException("Series data cannot be null.");
        }
        nativeUpdateSeries(nativeHandle, name, data);
    }

    /**
     * 执行已加载的字节码，从当前 bar_index 计算到 newTotalBars。
     * 可用于批量初始计算和后续的增量计算。
     *
     * @param newTotalBars 目标要计算到的总K线柱数量。
     * @return 0 表示成功，非 0 表示失败。
     */
    public int execute(int newTotalBars) {
        checkNativeHandle();
        return nativeExecute(nativeHandle, newTotalBars);
    }

    /**
     * 获取最近一次执行失败时的错误信息。
     *
     * @return 错误信息字符串。
     */
    public String getLastErrorMessage() {
        checkNativeHandle();
        return nativeGetLastErrorMessage(nativeHandle);
    }

    /**
     * 获取所有已绘制序列的结果，格式为 CSV 字符串。
     *
     * @return 包含绘图结果的 CSV 格式字符串。
     */
    public String getPlottedResultsAsCsv() {
        checkNativeHandle();
        return nativeGetPlottedResultsAsCsv(nativeHandle);
    }

    /**
     * 释放底层 C++ 对象。
     * 这是 {@link Closeable} 接口的实现，允许在 try-with-resources 中自动调用。
     */
    @Override
    public void close() {
        if (nativeHandle != 0) {
            nativeDestroy(nativeHandle);
            nativeHandle = 0; // 防止重复释放
        }
    }
    
    /**
     * 检查本地句柄是否有效。
     */
    private void checkNativeHandle() {
        if (nativeHandle == 0) {
            throw new IllegalStateException("PineVM has already been closed.");
        }
    }

    // --- 本地方法声明 ---

    private native long nativeCreate();
    private native void nativeDestroy(long handle);
    private native void nativeLoadBytecode(long handle, String code);
    private native void nativeUpdateSeries(long handle, String name, double[] data);
    private native int nativeExecute(long handle, int newTotalBars);
    private native String nativeGetLastErrorMessage(long handle);
    private native String nativeGetPlottedResultsAsCsv(long handle);
}