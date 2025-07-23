#include <pybind11/pybind11.h>
#include <pybind11/stl.h>
#include <pybind11/numpy.h> // 必须包含 NumPy 头文件
#include "../../Hithink/HithinkCompiler.h"
#include "../../PineVM.h"
#include <memory> // for std::make_shared

namespace py = pybind11;

// 我们需要确保 Series 类对 pybind11 是已知的，即使我们不完全导出它
// 这样我们才能在 lambda 中使用 std::shared_ptr<Series>
namespace pybind11 { namespace detail {
    template <> struct type_caster<std::shared_ptr<Series>> : public copyable_holder_caster<Series, std::shared_ptr<Series>> {};
}}

PYBIND11_MODULE(pine_vm_core, m) {
    m.doc() = "Core C++ module for the PineVM library";

    // py::class_ 用于绑定一个 C++ 类
    py::class_<HithinkCompiler>(m, "HithinkCompiler")
        // 绑定构造函数
        .def(py::init<>())
        // 将 C++ 的 compile_to_str 方法在 Python 中暴露为 compile
        .def("compile", &HithinkCompiler::compile_to_str, "Compiles Hithink script to bytecode string.")
        .def("hadError", &HithinkCompiler::hadError, "Checks if the last compilation had errors.");

    // 绑定 PineVM 类
    py::class_<PineVM>(m, "PineVM")
        // 绑定构造函数 PineVM()
        .def(py::init<>())
        .def("load_bytecode", &PineVM::loadBytecode, "Loads bytecode for execution.")
        .def("execute", &PineVM::execute, "Executes the loaded bytecode.")
        .def("get_plotted_results_as_string", &PineVM::getPlottedResultsAsString, "Gets plotted results as a CSV formatted string.")
        
        // **修正的核心部分**
        // 创建一个新的绑定方法来处理 NumPy 数组
        // Python 中调用 vm.register_series("close", np_array) 将会执行这个 lambda
        .def("register_series", [](PineVM &vm, const std::string &name, py::array_t<double> arr) {
            // 1. 创建一个新的 C++ Series 对象
            auto series_ptr = std::make_shared<Series>();

            // 2. 从 NumPy 数组中获取数据
            py::buffer_info buf = arr.request();
            if (buf.ndim != 1) {
                throw std::runtime_error("NumPy array must be a 1D array.");
            }
            double *ptr = static_cast<double *>(buf.ptr);

            // 3. 将数据复制到 Series 对象的内部向量中
            series_ptr->data.assign(ptr, ptr + buf.shape[0]);
            series_ptr->setName(name);

            // 4. 调用原始的 C++ registerSeries 方法
            vm.registerSeries(name, series_ptr);
        }, "Registers a data series from a NumPy array.");
}