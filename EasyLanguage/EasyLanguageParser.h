#pragma once
#include "EasyLanguageLexer.h"
#include "EasyLanguageAST.h"
#include <vector>
#include <memory>
#include <stdexcept>

class EasyLanguageParser {
public:
    EasyLanguageParser(const std::string& source);
    std::vector<std::unique_ptr<ELStatement>> parse();

private:
    EasyLanguageLexer lexer;
    Token current;
    Token previous;
    bool hadError = false;

    void advance();
    void consume(TokenType type, const std::string& message);
    bool match(TokenType type);
    bool check(TokenType type);

    std::unique_ptr<ELStatement> declaration();
    std::unique_ptr<ELStatement> statement();
    std::unique_ptr<ELStatement> inputDeclaration();
    std::unique_ptr<ELStatement> variableDeclaration(); // Keep this
    std::unique_ptr<ELStatement> ifStatement();
    std::unique_ptr<ELStatement> plotStatement(Token plotNameToken); // Declare plotStatement with token parameter
    std::unique_ptr<ELStatement> assignmentStatement(); // For `VarName = Expression;`

    std::unique_ptr<ELExpression> expression();
    std::unique_ptr<ELExpression> comparison();
    std::unique_ptr<ELExpression> term(); // + -
    std::unique_ptr<ELExpression> factor(); // * /
    bool isAssignableToken(TokenType type); // 新增：检查一个词元是否可以作为赋值的目标
    std::unique_ptr<ELExpression> primary();
    std::unique_ptr<ELExpression> call();
};