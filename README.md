```mermaid
graph TD
    subgraph "入口与数据准备 (Entry & Data Prep)"
        main_cpp["main.cpp"]
    end

    subgraph "语言前端 (Language Frontends)"
        direction LR
        subgraph "PineScript"
            PineCompiler["PineCompiler.cpp"]
            PineParser["PineParser.cpp"]
            PineLexer["PineLexer.cpp"]
            PineAST["PineAST.h"]
        end
        subgraph "EasyLanguage"
            ELCompiler["EasyLanguageCompiler.cpp"]
            ELParser["EasyLanguageParser.cpp"]
            ELLexer["EasyLanguageLexer.cpp"]
            ELAST["EasyLanguageAST.h"]
        end
        subgraph "Hithink"
            HithinkCompiler["HithinkCompiler.cpp"]
            HithinkParser["HithinkParser.cpp"]
            HithinkLexer["HithinkLexer.cpp"]
            HithinkAST["HithinkAST.h"]
        end
    end

    subgraph "共享后端与执行 (Shared Backend & Execution)"
        PineVM["PineVM.cpp (虚拟机)"]
        Bytecode["[Bytecode]"]
        SeriesData["[Series Data]"]
    end
    
    subgraph "公共组件 (Common Components)"
        CompilerCommon["CompilerCommon.h"]
    end

    %% --- 流程定义 (Flow Definition) ---

    main_cpp -- "1\. 选择语言" --> PineCompiler
    main_cpp -- "1\. 选择语言" --> ELCompiler
    main_cpp -- "1\. 选择语言" --> HithinkCompiler

    PineCompiler -- "2\. 使用" --> PineParser
    PineParser -- "3\. 使用" --> PineLexer
    PineLexer -- "4\. 扫描源码生成 [Tokens]" --> PineParser
    PineParser -- "5\. 解析 Tokens 生成" --> PineAST
    PineCompiler -- "6\. 遍历 AST 生成" --> Bytecode

    ELCompiler -- "2\. 使用" --> ELParser
    ELParser -- "3\. 使用" --> ELLexer
    ELLexer -- "4\. 扫描源码生成 [Tokens]" --> ELParser
    ELParser -- "5\. 解析 Tokens 生成" --> ELAST
    ELCompiler -- "6\. 遍历 AST 生成" --> Bytecode

    HithinkCompiler -- "2\. 使用" --> HithinkParser
    HithinkParser -- "3\. 使用" --> HithinkLexer
    HithinkLexer -- "4\. 扫描源码生成 [Tokens]" --> HithinkParser
    HithinkParser -- "5\. 解析 Tokens 生成" --> HithinkAST
    HithinkCompiler -- "6\. 遍历 AST 生成" --> Bytecode

    Bytecode -- "7\. 加载字节码" --> PineVM
    main_cpp -- "创建" --> SeriesData
    SeriesData -- "8\. 注册数据 (registerSeries)" --> PineVM
    main_cpp -- "9\. 调用 vm.execute()" --> PineVM
    PineVM -- "10\. 解释执行" --> Bytecode

    %% --- 依赖关系 (Dependencies) ---
    PineCompiler -- "依赖" --> CompilerCommon
    ELCompiler -- "依赖" --> CompilerCommon
    HithinkCompiler -- "依赖" --> CompilerCommon
    PineVM -- "依赖" --> CompilerCommon
    PineAST -- "依赖" --> CompilerCommon
    ELAST -- "依赖" --> CompilerCommon
    HithinkAST -- "依赖" --> CompilerCommon

    %% --- 样式定义 (Styling) ---
    classDef frontend fill:#e6f3ff,stroke:#0066cc,stroke-width:2px;
    class PineCompiler,PineParser,PineLexer,PineAST,ELCompiler,ELParser,ELLexer,ELAST,HithinkCompiler,HithinkParser,HithinkLexer,HithinkAST frontend;

    classDef backend fill:#e6ffe6,stroke:#009933,stroke-width:2px;
    class PineVM,Bytecode,SeriesData backend;

    classDef main fill:#fff0e6,stroke:#ff6600,stroke-width:2px;
    class main_cpp main;
    
    classDef common fill:#f2f2f2,stroke:#595959,stroke-width:2px;
    class CompilerCommon common;
