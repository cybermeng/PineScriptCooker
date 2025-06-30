#pragma once
#include <vector>
#include <memory>
#include <string_view>

// 前向声明以减少头文件依赖
struct HithinkStatement;

class HithinkCompiler {
public:
    HithinkCompiler();

    std::vector<std::unique_ptr<HithinkStatement>> compile(std::string_view source);

    bool hadError() const;

private:
    bool hadError_ = false;
};
