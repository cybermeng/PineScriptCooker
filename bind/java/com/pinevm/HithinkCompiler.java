package com.pinevm;

/**
 * 一个静态工具类，用于将 Hithink 脚本源代码编译成 PineVM 可执行的字节码文本。
 * 这个类是线程安全的，因为它不维护任何状态。
 */
public final class HithinkCompiler {

    // 加载包含本地方法实现的共享库。
    // 库名 'pinevm_jni' 会被系统映射为 'libpinevm_jni.so' (Linux),
    // 'pinevm_jni.dll' (Windows), 或 'libpinevm_jni.dylib' (macOS)。
    static {
        System.loadLibrary("pinevm_jni");
    }

    /**
     * 私有构造函数，防止实例化。
     */
    private HithinkCompiler() {}

    /**
     * 编译 Hithink 脚本源代码。
     *
     * @param source Hithink 脚本的字符串。
     * @return 编译后的字节码的文本表示形式。
     * @throws RuntimeException 如果编译失败。
     */
    public static String compile(String source) {
        if (source == null || source.isEmpty()) {
            throw new IllegalArgumentException("Source code cannot be null or empty.");
        }
        return nativeCompile(source);
    }

    /**
     * 连接到 C++ 编译器的本地方法。
     *
     * @param source 要编译的 Hithink 源代码。
     * @return 编译后的字节码文本。
     */
    private static native String nativeCompile(String source);
}