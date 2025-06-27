#include "EasyLanguageParser.h"
#include <iostream>
#include <algorithm> // For std::transform
#include <cctype>    // For std::tolower, isdigit
 
EasyLanguageParser::EasyLanguageParser(const std::string& source) : lexer(source) {
    advance(); // Prime the parser with the first token
}

std::vector<std::unique_ptr<ELStatement>> EasyLanguageParser::parse() {
    std::vector<std::unique_ptr<ELStatement>> statements;
    while (current.type != TokenType::END_OF_FILE) {
        statements.push_back(declaration());
    }
    return statements;
}

void EasyLanguageParser::advance() {
    previous = current;
    current = lexer.scanToken();
    while (current.type == TokenType::ERROR) {
        std::cerr << "EasyLanguage Parser Error on line " << current.line << ": " << current.lexeme << std::endl;
        hadError = true;
        current = lexer.scanToken();
    }
}

void EasyLanguageParser::consume(TokenType type, const std::string& message) {
    if (current.type == type) {
        advance();
        return;
    }
    throw std::runtime_error("EasyLanguage Parser Error: Line " + std::to_string(current.line) + ": " + message + " (Found: '" + current.lexeme + "')");
}

bool EasyLanguageParser::match(TokenType type) {
    if (!check(type)) return false;
    advance();
    return true;
}

bool EasyLanguageParser::check(TokenType type) {
    return current.type == type;
}

std::unique_ptr<ELStatement> EasyLanguageParser::declaration() {
    if (match(TokenType::INPUTS)) {
        return inputDeclaration();
    }
    if (match(TokenType::VARIABLES)) {
        return variableDeclaration();
    }
    // If not a declaration, it must be a regular statement
    return statement();
}

std::unique_ptr<ELStatement> EasyLanguageParser::inputDeclaration() {
    consume(TokenType::COLON, "Expect ':' after 'Inputs'.");
    // EasyLanguage inputs can be like: Inputs: Length(14);
    // Or multiple: Inputs: Length(14), Price(Close);
    std::unique_ptr<ELInputDeclaration> input_decl;
    do {
        Token name = current;
        consume(TokenType::IDENTIFIER, "Expect input name.");
        std::unique_ptr<ELExpression> defaultValue = nullptr;
        if (match(TokenType::LEFT_PAREN)) {
            defaultValue = expression();
            consume(TokenType::RIGHT_PAREN, "Expect ')' after input default value.");
        }
        input_decl = std::make_unique<ELInputDeclaration>(name, std::move(defaultValue));
    } while (match(TokenType::COMMA));
    consume(TokenType::SEMICOLON, "Expect ';' after input declaration."); // Assuming semicolon terminates declaration line
    return input_decl; // This will only return the last one if multiple are declared on one line.
                       // A real EL parser would return a list or a block. For simplicity, return one.
}

std::unique_ptr<ELStatement> EasyLanguageParser::variableDeclaration() {
    consume(TokenType::COLON, "Expect ':' after 'Variables'.");
    // EasyLanguage variables can be like: Variables: MyVar(0);
    // Or multiple: Variables: MyVar(0), AnotherVar(1);
    std::unique_ptr<ELVariableDeclaration> var_decl;
    do {
        Token name = current;
        consume(TokenType::IDENTIFIER, "Expect variable name.");
        std::unique_ptr<ELExpression> initialValue = nullptr;
        if (match(TokenType::LEFT_PAREN)) {
            initialValue = expression();
            consume(TokenType::RIGHT_PAREN, "Expect ')' after variable initial value.");
        }
        var_decl = std::make_unique<ELVariableDeclaration>(name, std::move(initialValue));
    } while (match(TokenType::COMMA));
    consume(TokenType::SEMICOLON, "Expect ';' after variable declaration."); // Assuming semicolon terminates declaration line
    return var_decl; // Similar to inputDeclaration, returns only the last one.
}


std::unique_ptr<ELStatement> EasyLanguageParser::statement() {
    // Handle explicit 'Plot' keyword (e.g., "Plot(Close);")
    if (match(TokenType::PLOT)) {
        // The 'Plot' token is now in 'previous'.
        return plotStatement(previous);
    }

    // Handle 'PlotX' as an identifier (e.g., "Plot1(Close);")
    if (current.type == TokenType::IDENTIFIER) {
        std::string lowerLexeme = current.lexeme;
        std::transform(lowerLexeme.begin(), lowerLexeme.end(), lowerLexeme.begin(),
                       [](unsigned char c){ return std::tolower(c); });

        // Check if it's a "plot" followed by a number (e.g., "plot1", "plot2")
        if (lowerLexeme.rfind("plot", 0) == 0 && lowerLexeme.length() > 4 && isdigit(lowerLexeme[4])) {
            Token plotNameToken = current; // Capture the "PlotX" token
            advance(); // Consume the "PlotX" identifier
            return plotStatement(plotNameToken);
        }

        // If not a PlotX, then check if it's a generic function call statement
        if (lexer.peekNextToken().type == TokenType::LEFT_PAREN) {
            std::unique_ptr<ELExpression> funcCallExpr = call();
            consume(TokenType::SEMICOLON, "Expect ';' after function call statement.");
            return std::make_unique<ELExpressionStatement>(std::move(funcCallExpr));
        }
    }

    if (match(TokenType::IF)) {
        return ifStatement();
    }
    // Check for assignment statement: an assignable token followed by an '='.
    // This allows for user-defined variables that might coincidentally match keyword prefixes.
    if (isAssignableToken(current.type) && lexer.peekNextToken().type == TokenType::EQUAL) {
        return assignmentStatement();
    }
    throw std::runtime_error("EasyLanguage Parser Error: Line " + std::to_string(current.line) + ": Expected a statement.");
}

std::unique_ptr<ELStatement> EasyLanguageParser::plotStatement(Token plotNameToken) {
    // Check if we have identifier followed by parenthesis, if not throw error
    if(current.type != TokenType::LEFT_PAREN){
        throw std::runtime_error("EasyLanguage Parser Error: Line " + std::to_string(current.line) + ": Expect '(' after plot name. (Found: '" + current.lexeme + "')");
    }

    // The rest remains the same...

    consume(TokenType::LEFT_PAREN, "EasyLanguage Parser Error: Line " + std::to_string(current.line) + ": Expect '(' after plot name.");
    std::unique_ptr<ELExpression> value = expression(); // Required value
    std::unique_ptr<ELExpression> color = nullptr;     // Optional color
    if (match(TokenType::COMMA)) {
        color = expression(); // Consume optional color
    }
    consume(TokenType::RIGHT_PAREN, "EasyLanguage Parser Error: Line " + std::to_string(current.line) + ": Expect ')' after plot arguments.");
    consume(TokenType::SEMICOLON, "Expect ';' after plot statement.");
    return std::make_unique<ELPlotStatement>(plotNameToken, std::move(value), std::move(color));
}

std::unique_ptr<ELStatement> EasyLanguageParser::ifStatement() {
    std::unique_ptr<ELExpression> condition = expression(); // Condition is an expression
    consume(TokenType::THEN, "Expect 'Then' after if condition.");

    std::unique_ptr<ELIfStatement> ifStmt = std::make_unique<ELIfStatement>(std::move(condition));

    // Then branch
    if (match(TokenType::BEGIN)) { // Multi-line then block
        while (!check(TokenType::END) && current.type != TokenType::END_OF_FILE) {
            ifStmt->thenBranch.push_back(statement());
        }
        consume(TokenType::END, "Expect 'End' after 'Then Begin' block.");
        consume(TokenType::SEMICOLON, "Expect ';' after 'End' of then block."); // EL blocks often end with semicolon
    } else { // Single-line then statement
        ifStmt->thenBranch.push_back(statement());
    }

    // Else branch (optional)
    if (match(TokenType::ELSE)) {
        if (match(TokenType::BEGIN)) { // Multi-line else block
            while (!check(TokenType::END) && current.type != TokenType::END_OF_FILE) {
                ifStmt->elseBranch.push_back(statement());
            }
            consume(TokenType::END, "Expect 'End' after 'Else Begin' block.");
            consume(TokenType::SEMICOLON, "Expect ';' after 'End' of else block."); // EL blocks often end with semicolon
        } else { // Single-line else statement
            ifStmt->elseBranch.push_back(statement());
        }
    }
    return ifStmt;
}

std::unique_ptr<ELStatement> EasyLanguageParser::assignmentStatement() {
    // When assignmentStatement is called, 'current' is already the variable name token.
    // We need to ensure it's an assignable token type.
    if (!isAssignableToken(current.type)) {
        throw std::runtime_error("EasyLanguage Parser Error: Line " + std::to_string(current.line) + ": Invalid assignment target.");
    }
    Token name = current; // Store the variable name token
    advance(); // Consume the variable name token (e.g., MySMA, MyRSI)
    consume(TokenType::EQUAL, "Expect '=' for assignment.");
    std::unique_ptr<ELExpression> value = expression();
    consume(TokenType::SEMICOLON, "Expect ';' after assignment statement.");
    return std::make_unique<ELAssignmentStatement>(name, std::move(value));
}

std::unique_ptr<ELExpression> EasyLanguageParser::expression() {
    return comparison();
}

std::unique_ptr<ELExpression> EasyLanguageParser::comparison() {
    std::unique_ptr<ELExpression> expr = term(); // Start with term for arithmetic

    while (match(TokenType::GREATER) || match(TokenType::LESS) ||
           match(TokenType::GREATER_EQUAL) || match(TokenType::LESS_EQUAL) ||
           match(TokenType::EQUAL_EQUAL) || match(TokenType::BANG_EQUAL))
    {
        Token op = previous;
        std::unique_ptr<ELExpression> right = term();
        expr = std::make_unique<ELBinaryExpression>(std::move(expr), op, std::move(right));
    }
    return expr;
}

std::unique_ptr<ELExpression> EasyLanguageParser::term() {
    std::unique_ptr<ELExpression> expr = factor();

    while (match(TokenType::MINUS) || match(TokenType::PLUS)) {
        Token op = previous;
        std::unique_ptr<ELExpression> right = factor();
        expr = std::make_unique<ELBinaryExpression>(std::move(expr), op, std::move(right));
    }
    return expr;
}

std::unique_ptr<ELExpression> EasyLanguageParser::factor() {
    std::unique_ptr<ELExpression> expr = call();

    while (match(TokenType::SLASH) || match(TokenType::STAR)) {
        Token op = previous;
        std::unique_ptr<ELExpression> right = call();
        expr = std::make_unique<ELBinaryExpression>(std::move(expr), op, std::move(right));
    }
    return expr;
}

std::unique_ptr<ELExpression> EasyLanguageParser::call() {
    std::unique_ptr<ELExpression> expr = primary();

    while (match(TokenType::LEFT_PAREN)) {
        // If it's an identifier, it's a function call
        if (auto var_expr = dynamic_cast<ELVariableExpression*>(expr.get())) {
            auto call_expr = std::make_unique<ELFunctionCallExpression>(var_expr->name);
            if (!check(TokenType::RIGHT_PAREN)) {
                do {
                    call_expr->arguments.push_back(expression());
                } while (match(TokenType::COMMA));
            }
            consume(TokenType::RIGHT_PAREN, "Expect ')' after arguments.");
            expr = std::move(call_expr);
        } else {
            throw std::runtime_error("EasyLanguage Parser Error: Line " + std::to_string(current.line) + ": Expected function name before '('.");
        }
    }
    return expr;
}

// New auxiliary function: Checks if a token type can be an assignment target (L-value)
bool EasyLanguageParser::isAssignableToken(TokenType type) {
    // In EasyLanguage, user-defined variables are typically IDENTIFIERs.
    // However, if the lexer is over-eager and tokenizes "MySMA" as TokenType::SMA,
    // or "MyRSI" as TokenType::RSI_EL, we need to allow them as assignable targets.
    // This list should include all token types that can appear on the left-hand side of an assignment.
    switch (type) {
        case TokenType::IDENTIFIER:
        case TokenType::SMA: // If lexer misidentifies MySMA as SMA (due to common substring)
        case TokenType::RSI_EL: // If lexer misidentifies MyRSI as RSI_EL
            // Add other keywords here if they can be assigned to (e.g., Plot1, Value1 etc. if they are not IDENTIFIERs)
            return true;
        default:
            return false;
    }
}

std::unique_ptr<ELExpression> EasyLanguageParser::primary() {
    if (match(TokenType::NUMBER)) {
        return std::make_unique<ELLiteralExpression>(std::stod(previous.lexeme));
    }
    if (match(TokenType::TRUE)) {
        return std::make_unique<ELLiteralExpression>(true);
    }
    if (match(TokenType::FALSE)) {
        return std::make_unique<ELLiteralExpression>(false);
    }
    if (match(TokenType::STRING)) {
        std::string s = previous.lexeme;
        return std::make_unique<ELLiteralExpression>(s.substr(1, s.length() - 2));
    }
    if (match(TokenType::IDENTIFIER) || match(TokenType::AVERAGE) || match(TokenType::RSI_EL)) {
        return std::make_unique<ELVariableExpression>(previous); // Treat function names as variables initially for call() to handle
    }
    // EasyLanguage also has `True`, `False` for booleans.
    // For now, we'll assume numbers (0/1) or expressions evaluate to bool.
    throw std::runtime_error("EasyLanguage Parser Error: Line " + std::to_string(current.line) + ": Expect expression.");
}