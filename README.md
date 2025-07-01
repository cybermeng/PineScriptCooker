```mermaid
graph TD
    subgraph "用户接口 (User Interface)"
        direction LR
        UserInput["用户输入<br>(选择语言, 数据源)"] --> Main["main.cpp<br>(应用入口)"]
    end

    subgraph "编译阶段 <br>(Compilation Frontend)"
        direction LR
        style Bytecode fill:#bbf,stroke:#333,stroke-width:2px
        
        Main -- "1\. 选择编译器" --> CompilerChoice
        
        subgraph CompilerChoice["语言编译器"]
            direction TB
            PineCompiler["PineScript<br>Compiler"]
            ELCompiler["EasyLanguage<br>Compiler"]
            HithinkCompiler["Hithink<br>Compiler"]
        end

        CompilerChoice -- "2\. 处理源码" --> ProcessingFlow
        
        subgraph ProcessingFlow["编译流程 (以PineScript为例)"]
            SourceString["源码字符串<br>'ma = ta.sma(close, 14)'"]
            SourceString --> Lexer["PineLexer<br>(词法分析)"]
            Lexer --> Tokens["Token序列<br>[IDENTIFIER(ma), EQUAL, ... ]"]
            Tokens --> Parser["PineParser<br>(语法分析)"]
            Parser --> AST["抽象语法树 (AST)<br>(AssignmentStmt)"]
            AST --> CodeGen["PineCompiler<br>(代码生成 / AST Visitor)"]
            CodeGen --> Bytecode["通用字节码<br>(Bytecode)"]
        end
    end

    subgraph "数据准备<br> (Data Preparation)"
        direction LR
        Main -- "创建" --> DataSources["数据源 (DataSource)"]
        DataSources -- "加载数据到" --> PineVM
    end

    subgraph "执行阶段<br> (Execution Backend)"
        direction LR
        style PineVM fill:#9f9,stroke:#333,stroke-width:2px
        Bytecode -- "加载指令到" --> PineVM["PineVM<br>(虚拟机)"]
        PineVM -- "执行字节码" --> Result["执行结果<br>(指标序列)"]
    end

