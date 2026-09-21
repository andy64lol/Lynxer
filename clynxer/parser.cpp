#include "parser.hpp"

#include "error.hpp"
#include "types.hpp"

#include <algorithm>
#include <cstdint>
#include <functional>
#include <unordered_set>

namespace clynxer {

std::string Parser::parseTypeName(const std::string& message) {
    if (!check(TokenKind::Identifier)) {
        fail(message, current());
    }
    return advance().text;
}

bool Parser::isTypeName(const Token& token) const {
    if (token.kind != TokenKind::Identifier) {
        return false;
    }
    static const std::unordered_set<std::string> scalarTypes = {
        "any", "int", "float", "num", "char", "str", "bool", "numBool",
        "bit", "byte", "int8", "int16", "int32", "int64", "uint8",
        "uint16", "uint32", "uint64", "float32", "float64", "list",
        "tuple", "sentinel", "object", "codeblock", "functionAddress"};
    return scalarTypes.count(token.text) != 0 ||
           TypeRegistry::instance().hasNamedType(token.text);
}

std::vector<std::pair<std::string, std::string>> Parser::parseParameters() {
    std::vector<std::pair<std::string, std::string>> parameters;
    if (!checkText(")")) {
        while (true) {
            const std::string type = parseTypeName("expected parameter type");
            const Token name =
                expect(TokenKind::Identifier, "expected parameter name");
            parameters.emplace_back(type, name.text);
            if (!match(",")) {
                break;
            }
        }
    }
    expectText(")", "expected ')' after parameters");
    return parameters;
}

std::vector<Parameter> Parser::parseFunctionParameters() {
    std::vector<Parameter> parameters;
    std::unordered_set<std::string> names;
    bool sawDefault = false;
    if (!checkText(")")) {
        while (true) {
            std::string type = "any";
            if (isTypeName(current()) && peekAt(1).kind == TokenKind::Identifier) {
                type = advance().text;
            }
            const Token name =
                expect(TokenKind::Identifier, "expected parameter name");
            if (!names.insert(name.text).second) {
                fail("duplicate parameter name '" + name.text + "'", name);
            }
            ExpressionPtr defaultValue;
            if (match("=")) {
                sawDefault = true;
                defaultValue = parseExpression();
            } else if (sawDefault) {
                fail("required parameters cannot follow a parameter with a default value",
                     name);
            }
            parameters.push_back(
                {std::move(type), name.text, std::move(defaultValue)});
            if (!match(",")) {
                break;
            }
            if (checkText(")")) {
                fail("expected parameter after ','", current());
            }
        }
    }
    expectText(")", "expected ')' after parameters");
    return parameters;
}

bool Parser::looksLikeCodeblockSignature() const {
    if (!checkText("{")) {
        return false;
    }
    int depth = 0;
    for (std::size_t cursor = index_; cursor < tokens_.size(); ++cursor) {
        if (tokens_[cursor].text == "{") {
            ++depth;
        } else if (tokens_[cursor].text == "}") {
            --depth;
            if (depth == 0) {
                return cursor + 1 < tokens_.size() &&
                       tokens_[cursor + 1].text == "{";
            }
        }
    }
    return false;
}

std::vector<std::string> Parser::parseCodeblockSignature() {
    expectText("{", "expected '{' before codeblock parameters");
    std::vector<std::string> names;
    while (!checkText("}")) {
        names.push_back(
            expect(TokenKind::Identifier, "expected codeblock parameter name")
                .text);
        if (!match(",")) {
            break;
        }
        if (checkText("}")) {
            fail("expected codeblock parameter after ','", current());
        }
    }
    expectText("}", "expected '}' after codeblock parameters");
    return names;
}

Function Parser::parseFunction(const std::string& kind, bool topLevel) {
    const Token keyword = expectText(kind, "expected function declaration");
    const Token name = expect(TokenKind::Identifier, "expected function name");
    expectText("(", "expected '(' after function name");
    Function function;
    function.name = name.text;
    function.isGlobal = kind == "global";
    function.isFileFunction = kind == "func";
    function.parameters = parseFunctionParameters();
    if (match("-")) {
        expectText(">", "expected '>' after '-' in return type");
        function.returnType = parseTypeName("expected return type after '->'");
    } else if (match(":")) {
        function.returnType = parseTypeName("expected return type after ':'");
    }
    while (looksLikeCodeblockSignature()) {
        std::vector<std::string> names = parseCodeblockSignature();
        for (const std::string& blockName : names) {
            if (std::find(function.codeblockParameters.begin(),
                          function.codeblockParameters.end(),
                          blockName) != function.codeblockParameters.end()) {
                fail("duplicate codeblock parameter '" + blockName + "'", name);
            }
            if (!codeblockNames_.insert(blockName).second) {
                fail("duplicate codeblock name '" + blockName + "'", name);
            }
            if (std::find_if(function.parameters.begin(),
                             function.parameters.end(),
                             [&](const Parameter& parameter) {
                                 return parameter.name == blockName;
                             }) != function.parameters.end()) {
                fail("duplicate function parameter/codeblock name '" + blockName +
                         "'",
                     name);
            }
            function.codeblockParameters.push_back(blockName);
        }
    }
    if ((name.text == "setup" || name.text == "main") &&
        !function.codeblockParameters.empty()) {
        fail("entry-point functions cannot declare codeblock parameters", name);
    }
    function.statements = parseBlock("function body");
    (void)keyword;
    (void)topLevel;
    return function;
}

StatementPtr Parser::parseLocalFunction() {
    const Token keyword = expectText("local", "expected 'local' function");
    const Token name = expect(TokenKind::Identifier, "expected function name");
    expectText("(", "expected '(' after function name");
    auto function = std::make_shared<Function>();
    function->name = name.text;
    function->parameters = parseFunctionParameters();
    if (match("-")) {
        expectText(">", "expected '>' after '-' in return type");
        function->returnType = parseTypeName("expected return type");
    } else if (match(":")) {
        function->returnType = parseTypeName("expected return type");
    }
    while (looksLikeCodeblockSignature()) {
        std::vector<std::string> names = parseCodeblockSignature();
        for (const std::string& blockName : names) {
            if (std::find(function->codeblockParameters.begin(),
                          function->codeblockParameters.end(),
                          blockName) != function->codeblockParameters.end()) {
                fail("duplicate codeblock parameter '" + blockName + "'", name);
            }
            if (!codeblockNames_.insert(blockName).second) {
                fail("duplicate codeblock name '" + blockName + "'", name);
            }
            if (std::find_if(function->parameters.begin(),
                             function->parameters.end(),
                             [&](const Parameter& parameter) {
                                 return parameter.name == blockName;
                             }) != function->parameters.end()) {
                fail("duplicate function parameter/codeblock name '" + blockName +
                         "'",
                     name);
            }
            function->codeblockParameters.push_back(blockName);
        }
    }
    function->statements = parseBlock("local function body");
    (void)keyword;
    return std::make_unique<FunctionDeclarationStatement>(std::move(function));
}

void Parser::parseStructDefinition() {
    const Token start = expectText("struct", "expected 'struct'");
    const Token name = expect(TokenKind::Identifier, "expected struct name");
    if (TypeRegistry::instance().hasNamedType(name.text)) {
        fail("duplicate named type '" + name.text + "'", name);
    }
    expectText("{", "expected '{' before struct fields");
    StructDef definition;
    definition.name = name.text;
    while (!checkText("}")) {
        const std::string type = parseTypeName("expected struct field type");
        const Token field =
            expect(TokenKind::Identifier, "expected struct field name");
        expectText(";", "expected ';' after struct field");
        definition.fields.push_back({type, field.text, false});
    }
    expectText("}", "expected '}' after struct definition");
    TypeRegistry::instance().addStruct(std::move(definition));
    (void)start;
}

void Parser::parseClassDefinition() {
    expectText("class", "expected 'class'");
    const Token name = expect(TokenKind::Identifier, "expected class name");
    if (TypeRegistry::instance().hasNamedType(name.text)) {
        fail("duplicate named type '" + name.text + "'", name);
    }
    expectText("{", "expected '{' before class body");
    ClassDef definition;
    definition.name = name.text;
    while (!checkText("}")) {
        if (checkText("local") && peekAt(1).kind == TokenKind::Identifier &&
            peekAt(2).text == "(") {
            advance();
            const Token method =
                expect(TokenKind::Identifier, "expected method name");
            expectText("(", "expected '(' after method name");
            ClassMethod classMethod;
            classMethod.name = method.text;
            classMethod.params = parseParameters();
            classMethod.body = parseBlock("method body");
            definition.methods.push_back(std::move(classMethod));
            continue;
        }
        bool constant = false;
        if (checkText("const")) {
            constant = true;
            advance();
        }
        const std::string type = parseTypeName("expected class field type");
        const Token field =
            expect(TokenKind::Identifier, "expected class field name");
        ExpressionPtr initializer;
        if (match("=")) {
            initializer = parseExpression();
        }
        expectText(";", "expected ';' after class field");
        definition.fields.push_back(
            {type, field.text, constant, std::move(initializer)});
    }
    expectText("}", "expected '}' after class definition");
    TypeRegistry::instance().addClass(std::move(definition));
}

void Parser::parseEnumDefinition() {
    expectText("enum", "expected 'enum'");
    const Token name = expect(TokenKind::Identifier, "expected enum name");
    if (TypeRegistry::instance().hasNamedType(name.text)) {
        fail("duplicate named type '" + name.text + "'", name);
    }
    expectText("=", "expected '=' after enum name");
    expectText("[", "expected '[' before enum variants");
    EnumDef definition;
    definition.name = name.text;
    while (!checkText("]")) {
        const Token variantName =
            expect(TokenKind::Identifier, "expected enum variant name");
        EnumVariant variant;
        variant.name = variantName.text;
        if (match("(")) {
            if (!checkText(")")) {
                while (true) {
                    const std::string type =
                        parseTypeName("expected enum payload type");
                    const Token field = expect(
                        TokenKind::Identifier, "expected enum payload name");
                    variant.fields.push_back({type, field.text, false});
                    if (!match(",")) {
                        break;
                    }
                }
            }
            expectText(")", "expected ')' after enum payload");
        }
        definition.variants.push_back(std::move(variant));
        if (!match(",")) {
            break;
        }
    }
    expectText("]", "expected ']' after enum variants");
    expectText("{", "expected '{' after enum variants");
    int depth = 1;
    while (depth > 0) {
        if (check(TokenKind::End)) {
            fail("unterminated enum body", current());
        }
        if (checkText("{")) {
            ++depth;
        } else if (checkText("}")) {
            --depth;
        }
        advance();
    }
    TypeRegistry::instance().addEnum(std::move(definition));
}

std::unordered_map<std::string, Function> Parser::parseProgram() {
    TypeRegistry::reset();
    std::unordered_map<std::string, Function> functions;
    bool sawSetup = false;
    bool sawMain = false;
    bool sawAnyDeclaration = false;
    while (!check(TokenKind::End)) {
        if (checkText("struct")) {
            if (sawMain) {
                fail("declarations may not follow global main()", current());
            }
            parseStructDefinition();
            sawAnyDeclaration = true;
            continue;
        }
        if (checkText("class")) {
            if (sawMain) {
                fail("declarations may not follow global main()", current());
            }
            parseClassDefinition();
            sawAnyDeclaration = true;
            continue;
        }
        if (checkText("enum")) {
            if (sawMain) {
                fail("declarations may not follow global main()", current());
            }
            parseEnumDefinition();
            sawAnyDeclaration = true;
            continue;
        }
        if (checkText("func")) {
            const Token name = peekAt(1);
            if (sawMain) {
                fail("declarations may not follow global main()", current());
            }
            Function function = parseFunction("func", true);
            if (functions.find(function.name) != functions.end()) {
                fail("duplicate function '" + function.name + "'", name);
            }
            functions.emplace(function.name, std::move(function));
            sawAnyDeclaration = true;
            continue;
        }
        if (!checkText("global")) {
            fail("expected top-level function declaration", current());
        }
        const Token name = peekAt(1);
        if (name.text == "setup") {
            if (sawSetup || sawAnyDeclaration || sawMain) {
                fail("global setup() must be the first declaration", name);
            }
            Function function = parseFunction("global", true);
            if (function.name != "setup") {
                fail("global setup() must be the first declaration", name);
            }
            functions.emplace(function.name, std::move(function));
            sawSetup = true;
            sawAnyDeclaration = true;
            continue;
        }
        Function function = parseFunction("global", true);
        if (functions.find(function.name) != functions.end()) {
            fail("duplicate function '" + function.name + "'", name);
        }
        if (function.name == "main") {
            sawMain = true;
        } else if (sawMain) {
            fail("declarations may not follow global main()", name);
        }
        functions.emplace(function.name, std::move(function));
        sawAnyDeclaration = true;
    }
    if (functions.find("setup") == functions.end()) {
        fail("program must define global setup()", current());
    }
    return functions;
}

StatementPtr Parser::parseStatement() {
    if (checkText("import")) {
        return parseImport();
    }
    if (checkText("importAs")) {
        return parseImportAs();
    }
    if (checkText("local") && peekAt(1).kind == TokenKind::Identifier &&
        peekAt(2).text == "(") {
        return parseLocalFunction();
    }
    if (checkText("async") && peekAt(1).kind == TokenKind::Identifier &&
        peekAt(2).text == "(") {
        Function function = parseFunction("async", false);
        auto owned = std::make_shared<Function>(std::move(function));
        return std::make_unique<FunctionDeclarationStatement>(
            std::move(owned));
    }

    if (checkText("func")) {
        fail("file-wide func declarations are only allowed at top level",
             current());
    }
    if (checkText("global") && peekAt(1).kind == TokenKind::Identifier &&
        peekAt(2).text == "(") {
        Function function = parseFunction("global", false);
        auto owned = std::make_shared<Function>(std::move(function));
        return std::make_unique<FunctionDeclarationStatement>(std::move(owned));
    }
    if (checkText("return")) {
        return parseReturn();
    }
    if (checkText("switch")) {
        return parseSwitch();
    }
    if (checkText("try")) {
        return parseTryCatch();
    }
    if (checkText("exec")) {
        return parseExec();
    }
    if (checkText("if")) {
        return parseIf();
    }
    if (checkText("while")) {
        return parseWhile();
    }
    if (checkText("for")) {
        return parseFor();
    }
    if (checkText("doWhile")) {
        return parseDoWhile();
    }
    if (checkText("iterate")) {
        return parseIterate();
    }
    if (checkText("forever")) {
        return parseForever();
    }
    if (checkText("break") || checkText("continue") ||
        checkText("restart")) {
        return parseLoopControl();
    }
    if (checkText("await")) {
        return parseCallStatement();
    }
    bool dottedCall = false;
    if (check(TokenKind::Identifier) && peekAt(1).text == ".") {
        std::size_t offset = 1;
        while (peekAt(offset).text == "." &&
               peekAt(offset + 1).kind == TokenKind::Identifier) {
            offset += 2;
        }
        dottedCall = peekAt(offset).text == "(";
    }
    if ((check(TokenKind::Identifier) &&
         (peekAt(1).text == "(" || dottedCall)) ||
        (checkText("global") && peekAt(1).text == ".")) {
        return parseCallStatement();
    }
    return parseSimpleStatement(true);
}

StatementPtr Parser::parseImport() {
    const Token keyword = expectText("import", "expected 'import'");
    expectText("(", "expected '(' after import");
    const Token path = expect(TokenKind::String, "expected module path string");
    expectText(")", "expected ')' after module path");
    expectText(";", "expected ';' after import");
    imports_.push_back(ImportRecord{path.text, "", keyword.line,
                                    keyword.column});
    return std::make_unique<ImportStatement>(path.text, "", keyword.line,
                                              keyword.column);
}

StatementPtr Parser::parseImportAs() {
    const Token keyword = expectText("importAs", "expected 'importAs'");
    expectText("(", "expected '(' after importAs");
    const Token path = expect(TokenKind::String, "expected module path string");
    expectText(",", "expected ',' after module path");
    const Token alias = expect(TokenKind::String, "expected module alias string");
    expectText(")", "expected ')' after importAs arguments");
    expectText(";", "expected ';' after importAs");
    imports_.push_back(ImportRecord{path.text, alias.text, keyword.line,
                                    keyword.column});
    return std::make_unique<ImportStatement>(path.text, alias.text, keyword.line,
                                              keyword.column);
}

StatementPtr Parser::parseCallStatement() {
    ExpressionPtr call = parseExpression();
    auto* callExpression = dynamic_cast<CallExpression*>(call.get());
    bool hadCodeblocks = false;
    if (callExpression != nullptr) {
        while (checkText("{")) {
            hadCodeblocks = true;
            if (peekAt(1).text == "{") {
                advance();
                advance();
                const std::string name =
                    expect(TokenKind::Identifier,
                           "expected codeblock name inside '{{...}}'")
                        .text;
                expectText("}", "expected '}' after codeblock name");
                expectText("}", "expected '}' after codeblock reference");
                callExpression->addNamedCodeblock(name);
            } else {
                callExpression->addInlineCodeblock(parseBlock("codeblock"));
            }
        }
    }
    if (checkText(";")) {
        advance();
    } else if (!hadCodeblocks) {
        fail("expected ';' after call", current());
    }
    return std::make_unique<ExpressionStatement>(std::move(call));
}

ExpressionPtr Parser::parseCallExpression() {
    const Token start = advance();
    std::string name = start.text;
    if (name == "global") {
        expectText(".", "expected '.' after 'global'");
        name = expect(TokenKind::Identifier,
                      "expected function name after 'global.'")
                   .text;
    }
    while (match(".")) {
        const Token part =
            expect(TokenKind::Identifier, "expected name after '.'");
        name += "." + part.text;
    }
    expectText("(", "expected '(' after function name");
    std::vector<ExpressionPtr> arguments = parseArguments(name);
    return std::make_unique<CallExpression>(std::move(name),
                                             std::move(arguments), start.line,
                                             start.column);
}

std::vector<ExpressionPtr> Parser::parseArguments(const std::string& name) {
    std::string resolved = name;
    if (resolved.rfind("global.", 0) == 0) {
        resolved = resolved.substr(7);
    }
    const bool allowInter = resolved == "print" || resolved == "println" ||
                            resolved == "input" || resolved == "inputln";
    std::vector<ExpressionPtr> arguments;
    if (!checkText(")")) {
        for (;;) {
            if (allowInter && check(TokenKind::InterpString)) {
                const Token token = advance();
                arguments.push_back(parseInterpString(token));
            } else {
                arguments.push_back(parseExpression());
            }
            if (!match(",")) {
                break;
            }
        }
    }
    expectText(")", "expected ')' after arguments");
    return arguments;
}

ExpressionPtr Parser::parseInterpString(const Token& token) {
    auto expression = std::make_unique<InterpStringExpression>();
    const std::string& raw = token.text;
    std::string literal;
    std::size_t index = 0;
    while (index < raw.size()) {
        const char current = raw[index];
        if (current == '\\' && index + 1 < raw.size()) {
            const char escaped = raw[index + 1];
            index += 2;
            switch (escaped) {
            case 'n':
                literal += '\n';
                break;
            case 'r':
                literal += '\r';
                break;
            case 't':
                literal += '\t';
                break;
            case '\\':
                literal += '\\';
                break;
            case '"':
                literal += '"';
                break;
            case 'e':
                literal += '\x1b';
                break;
            case '{':
                literal += '{';
                break;
            case '}':
                literal += '}';
                break;
            default:
                fail("unknown string escape \\" + std::string(1, escaped),
                     token.line, token.column);
            }
            continue;
        }
        if (current == '{') {
            std::size_t cursor = index + 1;
            bool inString = false;
            std::size_t end = std::string::npos;
            while (cursor < raw.size()) {
                const char inner = raw[cursor];
                if (inString) {
                    if (inner == '\\') {
                        cursor += 2;
                        continue;
                    }
                    if (inner == '"') {
                        inString = false;
                    }
                } else if (inner == '"') {
                    inString = true;
                } else if (inner == '}') {
                    end = cursor;
                    break;
                }
                ++cursor;
            }
            if (end == std::string::npos) {
                fail("missing '}' in interpolated string", token.line,
                     token.column);
            }
            const std::string body = raw.substr(index + 1, end - index - 1);
            if (body.find_first_not_of(" \t") == std::string::npos) {
                fail("empty '{}' interpolation", token.line, token.column);
            }
            expression->addLiteral(literal);
            literal.clear();
            try {
                Lexer lexer(body, "");
                Parser parser(lexer.scan());
                ExpressionPtr inner = parser.parseExpression();
                if (!parser.check(TokenKind::End)) {
                    fail("invalid expression in interpolated string",
                         token.line, token.column);
                }
                expression->addExpression(std::move(inner));
            } catch (const SourceError& error) {
                fail(error.what(), token.line, token.column);
            }
            index = end + 1;
            continue;
        }
        if (current == '}') {
            fail("stray '}' in interpolated string", token.line, token.column);
        }
        literal += current;
        ++index;
    }
    expression->addLiteral(literal);
    return expression;
}

StatementPtr Parser::parseReturn() {
    const Token token = expectText("return", "expected 'return'");
    ExpressionPtr value;
    if (!checkText(";")) {
        value = parseExpression();
    }
    expectText(";", "expected ';' after return");
    return std::make_unique<ReturnStatement>(std::move(value), token.line,
                                             token.column);
}

StatementPtr Parser::parseSwitch() {
    const Token start = expectText("switch", "expected 'switch'");
    expectText("(", "expected '(' after 'switch'");
    ExpressionPtr value = parseExpression();
    expectText(")", "expected ')' after switch value");
    expectText("{", "expected '{' before switch cases");
    std::vector<SwitchCase> cases;
    bool sawDefault = false;
    while (!checkText("}")) {
        if (checkText("case")) {
            advance();
            expectText("(", "expected '(' after 'case'");
            ExpressionPtr pattern = parseExpression();
            expectText(")", "expected ')' after case pattern");
            cases.push_back(
                {std::move(pattern), parseBlock("case body")});
            continue;
        }

        if (checkText("default")) {
            if (sawDefault) {
                fail("switch may contain only one default case", current());
            }
            sawDefault = true;
            advance();
            expectText("(", "expected '(' after 'default'");
            expectText(")", "expected ')' after 'default'");
            cases.push_back({nullptr, parseBlock("default body")});
            continue;
        }
        fail("expected 'case' or 'default' in switch", current());
    }
    expectText("}", "expected '}' after switch");
    return std::make_unique<SwitchStatement>(std::move(value),
                                              std::move(cases), start.line,
                                              start.column);
}

StatementPtr Parser::parseTryCatch() {
    const Token start = expectText("try", "expected 'try'");
    StatementList tryStatements = parseBlock("try body");
    expectText("catch", "expected 'catch' after 'try' body");

    std::string catchName;
    int catchLine = start.line;
    int catchColumn = start.column;
    if (match("(")) {
        const Token type =
            expect(TokenKind::Identifier, "expected 'str' in catch clause");
        if (type.text != "str") {
            fail("expected 'str' type keyword for the catch variable", type);
        }
        const Token name =
            expect(TokenKind::Identifier, "expected catch variable name");
        catchName = name.text;
        catchLine = name.line;
        catchColumn = name.column;
        expectText(")", "expected ')' after catch variable");
    }
    StatementList catchStatements = parseBlock("catch body");
    return std::make_unique<TryCatchStatement>(
        std::move(tryStatements), std::move(catchName),
        std::move(catchStatements), catchLine, catchColumn);
}

StatementPtr Parser::parseExec() {
    const Token start = expectText("exec", "expected 'exec'");
    expectText("(", "expected '(' after exec");
    std::vector<ExpressionPtr> arguments;
    if (!checkText(")")) {
        while (true) {
            arguments.push_back(parseExpression());
            if (!match(",")) {
                break;
            }
        }
    }
    expectText(")", "expected ')' after exec arguments");
    if (checkText("{") && peekAt(1).text == "{") {
        advance();
        advance();
        const std::string name =
            expect(TokenKind::Identifier, "expected codeblock name").text;
        expectText("}", "expected '}' after codeblock name");
        expectText("}", "expected '}' after codeblock reference");
        if (checkText(";")) {
            advance();
        }
        return std::make_unique<ExecStatement>(
            std::move(arguments), name,
            std::vector<std::pair<std::string, std::string>>{}, StatementList{},
            start.line, start.column);
    }
    StatementList body = parseBlock("codeblock");
    if (checkText(";")) {
        advance();
    }
    return std::make_unique<ExecStatement>(
        std::move(arguments), "", std::vector<std::pair<std::string, std::string>>{},
        std::move(body), start.line, start.column);
}

StatementPtr Parser::parseSimpleStatement(bool requireSemicolon) {
    if (checkText("codeblock")) {
        return parseCodeblockDeclaration();
    }
    if (checkText("const")) {
        advance();
        if (checkText("vargroup")) {
            return parseVargroupDeclaration(true);
        }
        return parseDeclaration(true);
    }
    if (checkText("vargroup")) {
        return parseVargroupDeclaration(false);
    }
    if (isTypeName(current()) &&
        peekAt(1).kind == TokenKind::Identifier &&
        (peekAt(2).text == "=" || peekAt(2).text == ";" ||
         peekAt(2).text == ".")) {
        if (peekAt(2).text == ".") {
            return parseTypedDotAssignment();
        }
        return parseDeclaration(false);
    }

    if (check(TokenKind::Identifier) && peekAt(1).text == "(") {
        ExpressionPtr call = parseCallExpression();
        if (requireSemicolon) {
            expectText(";", "expected ';' after call");
        }
        return std::make_unique<ExpressionStatement>(std::move(call));
    }

    return parseAssignmentOrExpressionStatement(requireSemicolon);
}

StatementPtr Parser::parseCodeblockDeclaration() {
    const Token keyword = expectText("codeblock", "expected 'codeblock'");
    const Token name = expect(TokenKind::Identifier, "expected codeblock name");
    if (!codeblockNames_.insert(name.text).second) {
        fail("duplicate codeblock name '" + name.text + "'", name);
    }
    expectText("=", "expected '=' in codeblock declaration");
    StatementList body = parseBlock("codeblock");
    std::vector<std::pair<std::string, std::string>> params;
    if (match("[")) {
        while (!checkText("]")) {
            std::string type = "any";
            if (isTypeName(current()) &&
                peekAt(1).kind == TokenKind::Identifier) {
                type = advance().text;
            }
            const Token param = expect(TokenKind::Identifier,
                                       "expected codeblock parameter name");
            params.emplace_back(std::move(type), param.text);
            if (!match(",")) {
                break;
            }
        }
        expectText("]", "expected ']' after codeblock parameters");
    }
    if (checkText(";")) {
        advance();
    }
    return std::make_unique<CodeblockDeclarationStatement>(
        name.text, std::move(params), std::move(body), keyword.line,
        keyword.column);
}

StatementPtr Parser::parseDeclaration(bool constant) {
    const Token type = advance();
    const Token name =
        expect(TokenKind::Identifier, "expected variable name");
    expectText("=", "expected '=' in declaration");
    ExpressionPtr value = parseExpression();
    expectText(";", "expected ';' after declaration");
    if (constant) {
        return std::make_unique<DeclarationStatement>(
            type.text, name.text, std::move(value), type.line, type.column,
            true);
    }
    return std::make_unique<DeclarationStatement>(
        type.text, name.text, std::move(value), type.line, type.column);
}

StatementPtr Parser::parseTypedDotAssignment() {
    const Token type = advance();
    std::vector<std::string> path{
        expect(TokenKind::Identifier, "expected variable name").text};
    while (match(".")) {
        path.push_back(expect(TokenKind::Identifier,
                              "expected field name after '.'")
                           .text);
    }
    const Token operation = expectAssignmentOperator();
    ExpressionPtr value = parseExpression();
    expectText(";", "expected ';' after assignment");
    if (operation.text != "=") {
        fail("compound assignment is not supported for typed field assignment",
             operation.line, operation.column);
    }
    return std::make_unique<DotAssignmentStatement>(
        std::move(path), type.text, std::move(value), type.line, type.column);
}

StatementPtr Parser::parseVargroupDeclaration(bool constant) {
    const Token type = expectText("vargroup", "expected 'vargroup'");
    const Token name = expect(TokenKind::Identifier, "expected variable name");
    expectText("=", "expected '=' in vargroup declaration");
    ExpressionPtr value = parsePrimary();
    expectText(";", "expected ';' after vargroup declaration");
    if (constant) {
        return std::make_unique<DeclarationStatement>(
            "vargroup", name.text, std::move(value), type.line, type.column,
            true);
    }
    return std::make_unique<DeclarationStatement>(
        "vargroup", name.text, std::move(value), type.line, type.column);
}

StatementPtr Parser::parseAssignmentOrExpressionStatement(bool requireSemicolon) {
    const Token name = expect(
        TokenKind::Identifier,
        "expected a declaration, assignment, or print call");
    if (checkText(".") || checkText("=") || checkText("+=") ||
        checkText("-=") || checkText("*=") || checkText("/=") ||
        checkText("%=") || checkText("**=") || checkText("/%=")) {
        std::vector<std::string> path{name.text};
        while (match(".")) {
            path.push_back(expect(TokenKind::Identifier,
                                  "expected field name after '.'")
                               .text);
        }
    const Token operation = expectAssignmentOperator();
    ExpressionPtr value = parseExpression();
    if (requireSemicolon) {
        expectText(";", "expected ';' after assignment");
    }
    if (operation.text != "=") {
        std::string binaryOperation;
        if (operation.text == "+=") {
            binaryOperation = "+";
        } else if (operation.text == "-=") {
            binaryOperation = "-";
        } else if (operation.text == "*=") {
            binaryOperation = "*";
        } else if (operation.text == "/=") {
            binaryOperation = "/";
        } else if (operation.text == "%=") {
            binaryOperation = "%";
        } else if (operation.text == "**=") {
            binaryOperation = "**";
        } else if (operation.text == "/%=") {
            binaryOperation = "/%";
        } else {
            fail("unsupported compound assignment operator '" +
                     operation.text + "'",
                 operation);
        }
        value = std::make_unique<BinaryExpression>(
            binaryOperation,
            std::make_unique<VariableExpression>(name.text, name.line,
                                                  name.column),
            std::move(value), operation.line, operation.column);
    }
        if (path.size() == 1) {
            return std::make_unique<AssignmentStatement>(
                name.text, std::move(value), name.line, name.column);
        }
        return std::make_unique<DotAssignmentStatement>(
            std::move(path), "", std::move(value), name.line, name.column);
    }
    fail("expected assignment or call", current());
}

StatementPtr Parser::parseIf() {
    expectText("if", "expected 'if'");
    expectText("(", "expected '(' after 'if'");
    ExpressionPtr condition = parseExpression();
    expectText(")", "expected ')' after condition");
    StatementList thenStatements = parseBlock("if body");

    std::vector<std::pair<ExpressionPtr, StatementList>> branches;
    while (checkText("elif")) {
        advance();
        expectText("(", "expected '(' after 'elif'");
        ExpressionPtr elifCondition = parseExpression();
        expectText(")", "expected ')' after 'elif' condition");
        branches.emplace_back(std::move(elifCondition),
                              parseBlock("elif body"));
    }
    StatementList elseStatements;
    bool hasElse = false;
    if (checkText("else")) {
        advance();
        hasElse = true;
        if (checkText("if")) {
            elseStatements.push_back(parseIf());
        } else {
            elseStatements = parseBlock("else body");
        }
    }
    StatementList tail = std::move(elseStatements);
    bool tailHasElse = hasElse;
    for (auto branch = branches.rbegin(); branch != branches.rend(); ++branch) {
        StatementList nested;
        nested.push_back(std::make_unique<IfStatement>(
            std::move(branch->first), std::move(branch->second),
            std::move(tail), tailHasElse));
        tail = std::move(nested);
        tailHasElse = true;
    }
    return std::make_unique<IfStatement>(
        std::move(condition), std::move(thenStatements),
        std::move(tail), branches.empty() ? hasElse : true);
}

StatementPtr Parser::parseWhile() {
    expectText("while", "expected 'while'");
    expectText("(", "expected '(' after 'while'");
    ExpressionPtr condition = parseExpression();
    expectText(")", "expected ')' after condition");
    return std::make_unique<WhileStatement>(
        std::move(condition), parseLoopBlock("while body"));
}

StatementPtr Parser::parseFor() {
    expectText("for", "expected 'for'");
    expectText("(", "expected '(' after 'for'");
    const Token initStart = current();
    std::string initName;
    if (checkText("int") || checkText("float") || checkText("str") ||
        checkText("bool") || checkText("any") || checkText("list") ||
        checkText("tuple")) {
        if (index_ + 1 < tokens_.size()) {
            initName = tokens_[index_ + 1].text;
        }
    } else if (check(TokenKind::Identifier)) {
        initName = current().text;
    }
    if (initName.empty()) {
        fail("expected for-loop initializer", initStart);
    }
    StatementPtr initializer = parseSimpleStatement(true);
    ExpressionPtr condition = parseExpression();
    StatementPtr update;
    if (checkText(";")) {
        advance();
        if (!checkText(")")) {
            update = parseSimpleStatement(false);
        }
    } else if (!checkText(")")) {
        fail("expected ';' or ')' after for condition", current());
    }
    if (update == nullptr) {
        update = std::make_unique<AssignmentStatement>(
            initName,
            std::make_unique<BinaryExpression>(
                "+",
                std::make_unique<VariableExpression>(
                    initName, initStart.line, initStart.column),
                std::make_unique<LiteralExpression>(
                    static_cast<std::int64_t>(1)),
                initStart.line, initStart.column),
            initStart.line, initStart.column);
    }
    expectText(")", "expected ')' after for clauses");
    return std::make_unique<ForStatement>(
        std::move(initializer), std::move(condition), std::move(update),
        parseLoopBlock("for body"));
}

StatementPtr Parser::parseDoWhile() {
    const Token loop = expectText("doWhile", "expected 'doWhile'");
    expectText("(", "expected '(' after 'doWhile'");
    ExpressionPtr condition;
    if (!checkText(")")) {
        condition = parseExpression();
    }
    expectText(")", "expected ')' after doWhile condition");
    return std::make_unique<DoWhileStatement>(
        std::move(condition), parseLoopBlock("doWhile body"));
}

StatementPtr Parser::parseIterate() {
    const Token loop = expectText("iterate", "expected 'iterate'");
    expectText("(", "expected '(' after 'iterate'");
    ExpressionPtr count = parseExpression();
    expectText(")", "expected ')' after iterate count");
    return std::make_unique<IterateStatement>(
        std::move(count), parseLoopBlock("iterate body"), loop.line,
        loop.column);
}

StatementPtr Parser::parseForever() {
    const Token loop = expectText("forever", "expected 'forever'");
    expectText("(", "expected '(' after 'forever'");
    expectText(")", "forever() does not accept arguments");
    return std::make_unique<ForeverStatement>(
        parseLoopBlock("forever body"), loop.line, loop.column);
}

StatementPtr Parser::parseLoopControl() {
    const Token control = advance();
    if (loopDepth_ == 0) {
        fail("'" + control.text + "' is only valid inside a loop", control);
    }
    expectText(";", "expected ';' after '" + control.text + "'");
    const LoopControlKind kind =
        control.text == "break" ? LoopControlKind::Break
                                 : LoopControlKind::Continue;
    return std::make_unique<LoopControlStatement>(kind);
}

Token Parser::expectAssignmentOperator() {
    if (checkText("=") || checkText("+=") || checkText("-=") ||
        checkText("*=") || checkText("/=") || checkText("%=") ||
        checkText("**=") || checkText("/%=")) {
        return advance();
    }
    fail("expected assignment operator", current());
}

StatementList Parser::parseBlock(const std::string& description) {
    expectText("{", "expected '{' before " + description);
    StatementList statements;
    while (!checkText("}")) {
        if (check(TokenKind::End)) {
            fail("unterminated " + description, current());
        }
        statements.push_back(parseStatement());
    }
    expectText("}", "expected '}' after " + description);
    return statements;
}

StatementList Parser::parseLoopBlock(const std::string& description) {
    ++loopDepth_;
    StatementList statements = parseBlock(description);
    --loopDepth_;
    return statements;
}

ExpressionPtr Parser::parseExpression() { return parseOrExpr(); }

// or / || / !||  (lowest precedence)
ExpressionPtr Parser::parseOrExpr() {
    ExpressionPtr expression = parseAndExpr();
    while (match("||") || matchKeyword("or") || match("!||")) {
        const Token operation = previous();
        const std::string op = operation.text == "or" ? "||" : operation.text;
        expression = std::make_unique<BinaryExpression>(
            op, std::move(expression), parseAndExpr(), operation.line,
            operation.column);
    }
    return expression;
}

// and / && / !&&
ExpressionPtr Parser::parseAndExpr() {
    ExpressionPtr expression = parseNotExpr();
    while (match("&&") || matchKeyword("and") || match("!&&")) {
        const Token operation = previous();
        const std::string op = operation.text == "and" ? "&&" : operation.text;
        expression = std::make_unique<BinaryExpression>(
            op, std::move(expression), parseNotExpr(), operation.line,
            operation.column);
    }
    return expression;
}

// unary logical NOT: '!!' or the 'not' keyword
ExpressionPtr Parser::parseNotExpr() {
    if (match("!!") || matchKeyword("not")) {
        const Token operation = previous();
        return std::make_unique<UnaryExpression>(
            "!!", parseNotExpr(), operation.line, operation.column);
    }
    return parseCompExpr();
}

// comparisons, legacy 'is' / 'not is'
ExpressionPtr Parser::parseCompExpr() {
    ExpressionPtr expression = parseBitwiseOr();
    if (checkKeyword("not") && peekAt(1).text == "is") {
        const Token operation = advance();  // 'not'
        advance();                          // 'is'
        ExpressionPtr right = parseBitwiseOr();
        expression = std::make_unique<BinaryExpression>(
            "not is", std::move(expression), std::move(right), operation.line,
            operation.column);
        return expression;
    }
    if (matchKeyword("is")) {
        const Token operation = previous();
        ExpressionPtr right = parseBitwiseOr();
        expression = std::make_unique<BinaryExpression>(
            "is", std::move(expression), std::move(right), operation.line,
            operation.column);
        return expression;
    }
    while (match("==") || match("!=") || match("<") || match("<=") ||
           match(">") || match(">=")) {
        const Token operation = previous();
        expression = std::make_unique<BinaryExpression>(
            operation.text, std::move(expression), parseBitwiseOr(),
            operation.line, operation.column);
    }
    return expression;
}

// | / !|
ExpressionPtr Parser::parseBitwiseOr() {
    ExpressionPtr expression = parseBitwiseXor();
    while (match("|") || match("!|")) {
        const Token operation = previous();
        expression = std::make_unique<BinaryExpression>(
            operation.text, std::move(expression), parseBitwiseXor(),
            operation.line, operation.column);
    }
    return expression;
}

// ^ / !^
ExpressionPtr Parser::parseBitwiseXor() {
    ExpressionPtr expression = parseBitwiseAnd();
    while (match("^") || match("!^")) {
        const Token operation = previous();
        expression = std::make_unique<BinaryExpression>(
            operation.text, std::move(expression), parseBitwiseAnd(),
            operation.line, operation.column);
    }
    return expression;
}

// & / !&
ExpressionPtr Parser::parseBitwiseAnd() {
    ExpressionPtr expression = parseShift();
    while (match("&") || match("!&")) {
        const Token operation = previous();
        expression = std::make_unique<BinaryExpression>(
            operation.text, std::move(expression), parseShift(),
            operation.line, operation.column);
    }
    return expression;
}

// << >>
ExpressionPtr Parser::parseShift() {
    ExpressionPtr expression = parseArith();
    while (match("<<") || match(">>")) {
        const Token operation = previous();
        expression = std::make_unique<BinaryExpression>(
            operation.text, std::move(expression), parseArith(),
            operation.line, operation.column);
    }
    return expression;
}

// + -
ExpressionPtr Parser::parseArith() {
    ExpressionPtr expression = parseTerm();
    while (match("+") || match("-")) {
        const Token operation = previous();
        expression = std::make_unique<BinaryExpression>(
            operation.text, std::move(expression), parseTerm(),
            operation.line, operation.column);
    }
    return expression;
}

// * / % /%
ExpressionPtr Parser::parseTerm() {
    ExpressionPtr expression = parsePower();
    while (match("*") || match("/") || match("%") || match("/%")) {
        const Token operation = previous();
        expression = std::make_unique<BinaryExpression>(
            operation.text, std::move(expression), parsePower(),
            operation.line, operation.column);
    }
    return expression;
}

// **  (right-associative)
ExpressionPtr Parser::parsePower() {
    ExpressionPtr expression = parseFactor();
    if (match("**")) {
        const Token operation = previous();
        expression = std::make_unique<BinaryExpression>(
            "**", std::move(expression), parsePower(), operation.line,
            operation.column);
    }
    return expression;
}

// Integer literals may be as large as 2^64-1: the unsigned memory accessors
// need the full range, and the reference (Python) has unbounded integers.
// A value that fits a signed 64-bit integer stays `int64_t`; a larger
// non-negative value becomes `UInt64Value`. Anything beyond 2^64-1 is an error
// rather than a silent wrap.
static Value integerLiteralValue(const std::string& text, const Token& token) {
    try {
        std::size_t consumed = 0;
        const long long value = std::stoll(text, &consumed);
        if (consumed == text.size()) {
            return static_cast<std::int64_t>(value);
        }
    } catch (const std::exception&) {
        // Fall through to the unsigned form.
    }
    if (!text.empty() && text[0] != '-') {
        try {
            std::size_t consumed = 0;
            const unsigned long long value = std::stoull(text, &consumed);
            if (consumed == text.size()) {
                return UInt64Value{static_cast<std::uint64_t>(value)};
            }
        } catch (const std::exception&) {
            // Fall through to the error below.
        }
    }
    throw SourceError("integer literal '" + text + "' is out of range",
                      token.line, token.column);
}

// unary + - ~  (highest precedence, recursive)
ExpressionPtr Parser::parseFactor() {
    if (match("+")) {
        return parseFactor();
    }
    if (match("-") || match("~")) {
        const Token operation = previous();
        // Fold a leading '-' into the literal so that -9223372036854775808
        // (INT64_MIN, whose magnitude does not fit a signed 64-bit integer)
        // parses exactly, as it does in the reference.
        if (operation.text == "-" && check(TokenKind::Number)) {
            const std::string& text = current().text;
            if (text.find('.') == std::string::npos) {
                const Token number = advance();
                return std::make_unique<LiteralExpression>(
                    integerLiteralValue("-" + text, number));
            }
        }
        return std::make_unique<UnaryExpression>(
            operation.text, parseFactor(), operation.line, operation.column);
    }
    return parsePrimary();
}

ExpressionPtr Parser::parsePrimary() {
    const Token token = advance();
    if (token.kind == TokenKind::Identifier && token.text == "await") {
        return std::make_unique<AwaitExpression>(parsePrimary());
    }
    if (token.kind == TokenKind::Number) {
        if (token.text.find('.') != std::string::npos) {
            return std::make_unique<LiteralExpression>(
                std::stod(token.text));
        }
        return std::make_unique<LiteralExpression>(
            integerLiteralValue(token.text, token));
    }
    if (token.kind == TokenKind::String) {
        return std::make_unique<LiteralExpression>(token.text);
    }
    if (token.kind == TokenKind::Char) {
        return std::make_unique<LiteralExpression>(CharValue{token.text});
    }
    if (token.kind == TokenKind::InterpString) {
        fail("interpolated strings are only allowed as arguments of print, "
             "println, input, and inputln",
             token);
    }
    if (token.kind == TokenKind::Identifier) {
        if (token.text == "true") {
            return std::make_unique<LiteralExpression>(true);
        }
        if (token.text == "false") {
            return std::make_unique<LiteralExpression>(false);
        }
        if (token.text == "none") {
            return std::make_unique<LiteralExpression>(Value{});
        }
        if (token.text == "new") {
            const Token type =
                expect(TokenKind::Identifier, "expected type after 'new'");
            expectText("(", "expected '(' after type name");
            return std::make_unique<NewExpression>(
                type.text, parseArguments(type.text), token.line, token.column);
        }
        return parsePostfix(
            std::make_unique<VariableExpression>(token.text, token.line,
                                                  token.column),
            token);
    }
    if (token.text == "(") {
        if (checkText(")")) {
            advance();
            return std::make_unique<TupleLiteralExpression>(
                std::vector<ExpressionPtr>{});
        }
        if (isTypeName(current()) && peekAt(1).kind != TokenKind::End) {
            std::vector<ExpressionPtr> elements;
            elements.push_back(parseTypedElement());
            while (match(",")) {
                if (checkText(")")) {
                    break;
                }
                elements.push_back(parseTypedElement());
            }
            expectText(")", "expected ')' after tuple");
            return std::make_unique<TupleLiteralExpression>(
                std::move(elements));
        }
        ExpressionPtr first = parseExpression();
        if (match(",")) {
            std::vector<ExpressionPtr> elements;
            elements.push_back(std::move(first));
            while (!checkText(")")) {
                elements.push_back(parseExpression());
                if (!match(",")) {
                    break;
                }
            }
            expectText(")", "expected ')' after tuple");
            return std::make_unique<TupleLiteralExpression>(
                std::move(elements));
        }
        expectText(")", "expected ')' after expression");
        return first;
    }
    if (token.text == "[") {
        std::vector<ExpressionPtr> elements;
        if (!checkText("]")) {
            for (;;) {
                if (isTypeName(current())) {
                    elements.push_back(parseTypedElement());
                } else {
                    elements.push_back(parseExpression());
                }
                if (!match(",")) {
                    break;
                }
            }
        }
        expectText("]", "expected ']' after list literal");
        return std::make_unique<ListLiteralExpression>(std::move(elements));
    }
    if (token.text == "{") {
        std::vector<VarGroupFieldInit> fields;
        while (!checkText("}")) {
            bool constant = false;
            if (checkText("const")) {
                constant = true;
                advance();
            }
            const std::string type = parseTypeName("expected vargroup field type");
            const Token name =
                expect(TokenKind::Identifier, "expected vargroup field name");
            expectText("=", "expected '=' after vargroup field name");
            fields.push_back(
                {type, name.text, parseExpression(), constant});
            if (!match(",")) {
                match(";");
                if (!checkText("}")) {
                    fail("expected ',' or ';' between vargroup fields",
                         current());
                }
            }
        }
        expectText("}", "expected '}' after vargroup literal");
        return std::make_unique<VarGroupLiteralExpression>(std::move(fields));
    }
    fail("expected an expression", token);
}

ExpressionPtr Parser::parseTypedElement() {
    const std::string type = parseTypeName("expected element type");
    if (checkText(",") || checkText("]") || checkText(")")) {
        fail("expected value after element type", current());
    }
    return std::make_unique<TypeCoerceExpression>(parseFactor(), type);
}

ExpressionPtr Parser::parsePostfix(ExpressionPtr expression,
                                   const Token& start) {
    while (true) {
        if (match("(")) {
            auto* variable = dynamic_cast<VariableExpression*>(expression.get());
            if (variable != nullptr) {
                std::vector<ExpressionPtr> arguments =
                    parseArguments(variable->name());
                expression = std::make_unique<CallExpression>(
                    variable->name(), std::move(arguments), start.line,
                    start.column);
                continue;
            }
            std::function<bool(const Expression&, std::string&)> qualify =
                [&](const Expression& node, std::string& result) {
                    if (const auto* root =
                            dynamic_cast<const VariableExpression*>(&node)) {
                        result = root->name();
                        return true;
                    }
                    const auto* access =
                        dynamic_cast<const DotAccessExpression*>(&node);
                    if (access == nullptr || !qualify(access->object(), result)) {
                        return false;
                    }
                    result += "." + access->fieldName();
                    return true;
                };
            std::string qualified;
            if (!qualify(*expression, qualified) ||
                (qualified.rfind("global.", 0) != 0 &&
                 qualified.rfind("local.", 0) != 0)) {
                fail("only named functions can be called directly", current());
            }
            std::vector<ExpressionPtr> arguments =
                parseArguments(qualified);
            expression = std::make_unique<CallExpression>(
                std::move(qualified), std::move(arguments), start.line,
                start.column);
            continue;
        }
        if (!match(".")) {
            break;
        }
        const Token field =
            expect(TokenKind::Identifier, "expected name after '.'");
        if (match("(")) {
            std::function<bool(const Expression&, std::string&)> qualify =
                [&](const Expression& node, std::string& result) {
                    if (const auto* root =
                            dynamic_cast<const VariableExpression*>(&node)) {
                        result = root->name();
                        return true;
                    }
                    const auto* access =
                        dynamic_cast<const DotAccessExpression*>(&node);
                    if (access == nullptr || !qualify(access->object(), result)) {
                        return false;
                    }
                    result += "." + access->fieldName();
                    return true;
                };
            std::string qualified;
            if (qualify(*expression, qualified) &&
                (qualified == "global" || qualified == "local" ||
                 qualified.rfind("global.", 0) == 0 ||
                 qualified.rfind("local.", 0) == 0)) {
                qualified += "." + field.text;
                expression = std::make_unique<CallExpression>(
                    std::move(qualified), parseArguments(field.text), start.line,
                    start.column);
            } else {
                expression = std::make_unique<MethodCallExpression>(
                    std::move(expression), field.text, parseArguments(field.text),
                    start.line, start.column);
            }
        } else {
            expression = std::make_unique<DotAccessExpression>(
                std::move(expression), field.text, start.line, start.column);
        }
    }
    return expression;
}

bool Parser::check(TokenKind kind) const { return current().kind == kind; }

bool Parser::checkText(const std::string& text) const {
    return current().text == text;
}

bool Parser::checkKeyword(const std::string& text) const {
    return current().kind == TokenKind::Identifier && current().text == text;
}

bool Parser::match(const std::string& text) {
    if (current().kind != TokenKind::Symbol || !checkText(text)) {
        return false;
    }
    advance();
    return true;
}

bool Parser::matchKeyword(const std::string& text) {
    if (!checkKeyword(text)) {
        return false;
    }
    advance();
    return true;
}

Token Parser::expect(TokenKind kind, const std::string& message) {
    if (!check(kind)) {
        fail(message, current());
    }
    return advance();
}

Token Parser::expectText(const std::string& text, const std::string& message) {
    if (!checkText(text)) {
        fail(message, current());
    }
    return advance();
}

const Token& Parser::current() const { return tokens_[index_]; }

const Token& Parser::peekAt(std::size_t offset) const {
    const std::size_t position = index_ + offset;
    if (position >= tokens_.size()) {
        return tokens_.back();
    }
    return tokens_[position];
}

const Token& Parser::previous() const { return tokens_[index_ - 1]; }

Token Parser::advance() {
    const Token token = current();
    if (!check(TokenKind::End)) {
        ++index_;
    }
    return token;
}

void Parser::fail(const std::string& message, const Token& token) const {
    throw SourceError(message, token.line, token.column);
}

} // namespace clynxer
