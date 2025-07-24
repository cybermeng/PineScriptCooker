import com.pinevm.HithinkCompiler;
import com.pinevm.PineVM;

public class Main {

    public static void main(String[] args) {
        // 1. 定义 Hithink 脚本
        String hithinkScript =
                "MA5: MA(CLOSE, 5);\n" +
                "MA10: MA(CLOSE, 10);\n" +
                "MA20: MA(CLOSE, 20);\n" +
                "CROSS_UP: CROSS(MA5, MA10);\n";

        System.out.println("--- Compiling Hithink Script ---");
        String bytecodeText;
        try {
            // 2. 编译脚本
            bytecodeText = HithinkCompiler.compile(hithinkScript);
            System.out.println("Compilation successful. Bytecode text:");
            System.out.println(bytecodeText);
        } catch (Exception e) {
            System.err.println("Compilation failed: " + e.getMessage());
            e.printStackTrace();
            return;
        }

        System.out.println("\n--- Executing in PineVM ---");
        // 3. 使用 try-with-resources 创建和管理 PineVM 实例
        try (PineVM vm = new PineVM()) {
            // 4. 加载字节码
            vm.loadBytecode(bytecodeText);

            // 5. 准备并更新输入数据
            double[] closePrices = new double[30];
            double[] lowPrices = new double[30];
            for (int i = 0; i < closePrices.length; i++) {
                // 生成一些模拟数据
                closePrices[i] = 100.0 + i + Math.sin(i / 5.0) * 5;
                lowPrices[i] = closePrices[i] - 2.0;
            }
            // 模拟 MA5 上穿 MA10 的情况
            closePrices[15] = 110;
            closePrices[16] = 118;
            lowPrices[16] = 115;


            vm.updateSeries("close", closePrices);
            vm.updateSeries("low", lowPrices);

            // 6. 执行计算
            int status = vm.execute(closePrices.length);

            if (status == 0) {
                System.out.println("Execution successful.");
                // 7. 获取并打印结果
                String csvResult = vm.getPlottedResultsAsCsv();
                System.out.println("\n--- Plotted Results (CSV) ---");
                System.out.println(csvResult);
            } else {
                System.err.println("Execution failed with status: " + status);
                System.err.println("Error message: " + vm.getLastErrorMessage());
            }

        } catch (Exception e) {
            System.err.println("An error occurred while running the VM:");
            e.printStackTrace();
        }
    }
}