#include <jni.h>
#include <string>
#include <vector>
#include <stdexcept>

// 包含我们需要导出的 C++ 类的头文件
#include "HithinkCompiler.h"
#include "PineVM.h"

// 辅助函数，用于将 Java 的 jstring 转换为 C++ 的 std::string
std::string jstringToStdString(JNIEnv *env, jstring jStr) {
    if (!jStr) {
        return "";
    }
    const char *chars = env->GetStringUTFChars(jStr, NULL);
    if (!chars) {
        return ""; // 内存不足
    }
    std::string str = chars;
    env->ReleaseStringUTFChars(jStr, chars);
    return str;
}

// 辅助函数，用于在 JNI 中抛出 Java 异常
void ThrowJavaException(JNIEnv *env, const char *className, const char *message) {
    jclass exClass = env->FindClass(className);
    if (exClass != NULL) {
        env->ThrowNew(exClass, message);
    }
}

// extern "C" 防止 C++ 名称修饰
extern "C" {

//=============================================================================
// HithinkCompiler JNI 导出
//=============================================================================

/**
 * @brief JNI 导出函数，用于编译 Hithink 脚本。
 * 对应 Java 方法: com.pinevm.HithinkCompiler.nativeCompile
 */
JNIEXPORT jstring JNICALL
Java_com_pinevm_HithinkCompiler_nativeCompile(JNIEnv *env, jclass clazz, jstring source) {
    HithinkCompiler compiler;
    std::string source_str = jstringToStdString(env, source);
    
    try {
        std::string bytecode_txt = compiler.compile_to_str(source_str);
        if (compiler.hadError()) {
            // 编译期间的语法错误等，通过异常报告给 Java
            ThrowJavaException(env, "java/lang/RuntimeException", "Hithink compilation failed. Check console for details.");
            return nullptr;
        }
        return env->NewStringUTF(bytecode_txt.c_str());
    } catch (const std::exception& e) {
        // 捕获 C++ 异常并转换为 Java 异常
        ThrowJavaException(env, "java/lang/RuntimeException", e.what());
        return nullptr;
    }
}


//=============================================================================
// PineVM JNI 导出
//=============================================================================

/**
 * @brief 创建一个新的 PineVM 实例，并返回其指针（作为 long）。
 * 对应 Java 方法: com.pinevm.PineVM.nativeCreate
 */
JNIEXPORT jlong JNICALL
Java_com_pinevm_PineVM_nativeCreate(JNIEnv *env, jobject thiz) {
    PineVM* vm = new PineVM();
    // 将指针转换为 jlong (64位整数) 以在 Java 中存储
    return reinterpret_cast<jlong>(vm);
}

/**
 * @brief 销毁由 nativeCreate 创建的 PineVM 实例。
 * 对应 Java 方法: com.pinevm.PineVM.nativeDestroy
 */
JNIEXPORT void JNICALL
Java_com_pinevm_PineVM_nativeDestroy(JNIEnv *env, jobject thiz, jlong handle) {
    PineVM* vm = reinterpret_cast<PineVM*>(handle);
    delete vm;
}

/**
 * @brief 加载字节码。
 * 对应 Java 方法: com.pinevm.PineVM.nativeLoadBytecode
 */
JNIEXPORT void JNICALL
Java_com_pinevm_PineVM_nativeLoadBytecode(JNIEnv *env, jobject thiz, jlong handle, jstring code) {
    PineVM* vm = reinterpret_cast<PineVM*>(handle);
    std::string code_str = jstringToStdString(env, code);
    try {
        vm->loadBytecode(code_str);
    } catch (const std::exception& e) {
        ThrowJavaException(env, "java/lang/RuntimeException", e.what());
    }
}

/**
 * @brief 更新一个输入序列的数据。
 * 对应 Java 方法: com.pinevm.PineVM.nativeUpdateSeries
 */
JNIEXPORT void JNICALL
Java_com_pinevm_PineVM_nativeUpdateSeries(JNIEnv *env, jobject thiz, jlong handle, jstring name, jdoubleArray data) {
    PineVM* vm = reinterpret_cast<PineVM*>(handle);
    std::string name_str = jstringToStdString(env, name);

    Series* series = vm->getSeries(name_str);
    if (!series) {
        // 如果找不到序列，可能是一个内置序列尚未被VM自动创建。
        // 我们需要注册它。
        auto new_series = std::make_shared<Series>();
        new_series->setName(name_str);
        vm->registerSeries(name_str, new_series);
        series = new_series.get();
    }
    
    if (series) {
        jsize len = env->GetArrayLength(data);
        jdouble* elements = env->GetDoubleArrayElements(data, 0);
        
        series->data.assign(elements, elements + len);
        
        env->ReleaseDoubleArrayElements(data, elements, JNI_ABORT); // JNI_ABORT 因为我们没有修改Java数组
    } else {
         ThrowJavaException(env, "java/lang/IllegalArgumentException", ("Series not found: " + name_str).c_str());
    }
}

/**
 * @brief 执行计算。
 * 对应 Java 方法: com.pinevm.PineVM.nativeExecute
 */
JNIEXPORT jint JNICALL
Java_com_pinevm_PineVM_nativeExecute(JNIEnv *env, jobject thiz, jlong handle, jint new_total_bars) {
    PineVM* vm = reinterpret_cast<PineVM*>(handle);
    return vm->execute(new_total_bars);
}

/**
 * @brief 获取最后的错误信息。
 * 对应 Java 方法: com.pinevm.PineVM.nativeGetLastErrorMessage
 */
JNIEXPORT jstring JNICALL
Java_com_pinevm_PineVM_nativeGetLastErrorMessage(JNIEnv *env, jobject thiz, jlong handle) {
    PineVM* vm = reinterpret_cast<PineVM*>(handle);
    return env->NewStringUTF(vm->getLastErrorMessage().c_str());
}

/**
 * @brief 获取以 CSV 格式表示的绘图结果。
 * 对应 Java 方法: com.pinevm.PineVM.nativeGetPlottedResultsAsCsv
 */
JNIEXPORT jstring JNICALL
Java_com_pinevm_PineVM_nativeGetPlottedResultsAsCsv(JNIEnv *env, jobject thiz, jlong handle) {
    PineVM* vm = reinterpret_cast<PineVM*>(handle);
    std::string csv_data = vm->getPlottedResultsAsString();
    return env->NewStringUTF(csv_data.c_str());
}

} // extern "C"