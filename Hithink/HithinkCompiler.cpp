#include "HithinkCompiler.h"
#include "HithinkParser.h" // 使用解析器来构建AST
#include "HithinkAST.h"   // 包含 HithinkStatement 的完整定义

HithinkCompiler::HithinkCompiler() : hadError_(false) {}

std::vector<std::unique_ptr<HithinkStatement>> HithinkCompiler::compile(std::string_view source) {
    HithinkParser parser(source);
    std::vector<std::unique_ptr<HithinkStatement>> statements = parser.parse();

    // 从解析器获取错误状态
    hadError_ = parser.hadError();

    if (hadError_) {
        return {}; // 返回空列表表示编译失败
    }

    return statements;
}

bool HithinkCompiler::hadError() const {
    return hadError_;
}