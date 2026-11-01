#pragma once

#include <stdexcept>
#include <string>
#include <utility>

namespace lynxer {

inline std::string diagnosticCodeForMessage(const std::string& message) {
    const auto startsWith = [&message](const char* prefix) {
        return message.rfind(prefix, 0) == 0;
    };

    if (startsWith("unterminated string escape")) return "stringEscapeUnterminated";
    if (startsWith("unterminated string")) return "stringUnterminated";
    if (startsWith("unknown string escape")) return "stringEscapeCharacterUnknown";
    if (startsWith("unterminated char escape")) return "charEscapeUnterminated";
    if (startsWith("unterminated char literal")) return "charUnterminated";
    if (startsWith("unknown char escape")) return "charEscapeCharacterUnknown";
    if (startsWith("char literal must contain")) return "charLengthInvalid";
    if (startsWith("unterminated multiline comment")) {
        return "commentMultilineUnterminated";
    }
    if (startsWith("expected macro declaration")) return "macroDeclarationExpected";
    if (startsWith("expected macro body")) return "macroBodyExpected";
    if (startsWith("duplicate imported macro")) return "macroImportDuplicated";
    if (startsWith("duplicate macro")) return "macroDuplicated";
    if (startsWith("expected parameter")) return "parameterExpected";
    if (startsWith("division by zero")) return "divisionByZero";
    if (startsWith("numeric value required")) return "numericValueExpected";
    if (startsWith("unary '-' requires a number")) return "unaryNumberRequired";
    if (startsWith("native value result is null")) return "nativeValueResultNull";
    if (startsWith("Struct '") && message.find("' has no field '") !=
                                      std::string::npos) {
        return "structHasNoField";
    }
    if (startsWith("unknown variable")) return "variableUnknown";
    if (startsWith("unknown function")) return "functionUnknown";
    if (startsWith("unknown type")) return "typeUnknown";
    if (startsWith("unknown class")) return "classUnknown";
    if (startsWith("unknown macro")) return "macroUnknown";
    if (startsWith("module '") && message.find(" was not found") !=
                                      std::string::npos) {
        return "moduleNotFound";
    }
    if (startsWith("expected ")) return "parserExpected";
    if (startsWith("unexpected character")) return "characterUnexpected";
    if (startsWith("duplicate function")) return "functionDuplicated";
    if (startsWith("duplicate named type")) return "typeDuplicated";
    if (startsWith("integer literal")) return "integerLiteralOutOfRange";
    if (startsWith("native module lifecycle failure")) {
        return "nativeModuleLifecycleFailure";
    }
    if (startsWith("could not load native module")) {
        return "nativeModuleLoadFailed";
    }
    if (startsWith("value cannot be assigned")) return "valueTypeMismatch";
    if (startsWith("unsupported binary operator")) return "binaryOperatorUnsupported";
    if (startsWith("unsupported unary operator")) return "unaryOperatorUnsupported";
    if (startsWith("for range step must not be zero")) return "rangeStepZero";
    if (startsWith("for range bounds and step")) return "rangeTypeInvalid";
    if (startsWith("iterate() count")) return "iterateCountInvalid";
    if (startsWith("value is not a function")) return "valueNotCallable";
    if (startsWith("native value contains too many items")) {
        return "nativeValueTooManyItems";
    }
    if (startsWith("native value nesting exceeds")) return "nativeValueNestingExceeded";
    if (startsWith("native value ABI does not support")) return "nativeValueTypeUnsupported";
    if (startsWith("native value result has an unknown type tag")) {
        return "nativeValueTypeTagUnknown";
    }
    if (startsWith("native value contains an invalid")) return "nativeValueInvalid";
    if (startsWith("macro expansion exceeded")) return "macroExpansionLimitExceeded";
    if (startsWith("unterminated macro invocation")) return "macroInvocationUnterminated";
    if (startsWith("unterminated caller-supplied macro codeblock")) {
        return "macroCodeblockUnterminated";
    }
    if (startsWith("program must define fn main")) return "mainFunctionMissing";
    if (startsWith("setup() is optional")) return "setupFunctionMissing";
    if (message.find("tuple assigned from a bracketed list is deprecated") !=
        std::string::npos) {
        return "deprecatedTupleCoercion";
    }
    if (message.find("function declarations are deprecated") !=
        std::string::npos) {
        return "deprecatedFunctionDeclaration";
    }
    if (message.find(" is deprecated; use ") != std::string::npos) {
        return "deprecatedOperator";
    }
    if (message.find("WinAPI built-in calls are deprecated") !=
        std::string::npos) {
        return "deprecatedWinApiBuiltin";
    }
    if (message.find("forever() has no break") != std::string::npos) {
        return "foreverMissingBreak";
    }
    if (message.find("unused parameter") != std::string::npos) {
        return "lintUnusedParameter";
    }
    if (message.find("unused variable") != std::string::npos) {
        return "lintUnusedVariable";
    }
    if (message == "unreachable statement") return "lintUnreachableStatement";
    if (message.find("converts '") != std::string::npos) {
        return "lintConversionMayLoseInformation";
    }
    if (message.find("cannot be assigned to '") != std::string::npos) {
        return "lintTypeMismatch";
    }
    if (startsWith("module path has no name")) return "moduleNameMissing";
    if (startsWith("cannot access field of none")) return "fieldAccessOnNone";
    if (startsWith("value of type") && message.find(" has no field ") !=
                                           std::string::npos) {
        return "fieldUnknown";
    }
    if (startsWith("value of type") && message.find(" has no method ") !=
                                           std::string::npos) {
        return "methodUnknown";
    }
    if (startsWith("unknown ")) return "nameUnknown";
    if (startsWith("duplicate ")) return "nameDuplicated";

    std::string code;
    bool capitalize = false;
    bool inQuotedText = false;
    char quote = '\0';
    for (unsigned char character : message) {
        if (inQuotedText) {
            if (character == static_cast<unsigned char>(quote)) {
                inQuotedText = false;
                capitalize = true;
            }
            continue;
        }
        if (character == '\'' || character == '"') {
            inQuotedText = true;
            quote = static_cast<char>(character);
            capitalize = true;
            continue;
        }
        if (character >= '0' && character <= '9') {
            capitalize = true;
            continue;
        }
        if ((character >= 'a' && character <= 'z') ||
            (character >= 'A' && character <= 'Z')) {
            const char letter = static_cast<char>(character);
            if (code.empty()) {
                code += static_cast<char>(letter >= 'A' && letter <= 'Z'
                                              ? letter - 'A' + 'a'
                                              : letter);
            } else if (capitalize) {
                code += static_cast<char>(letter >= 'a' && letter <= 'z'
                                              ? letter - 'a' + 'A'
                                              : letter);
            } else {
                code += letter;
            }
            capitalize = false;
        } else {
            capitalize = true;
        }
    }
    return code.empty() ? "runtimeError" : code;
}

struct SourceError : std::runtime_error {
    SourceError(std::string message, int line, int column)
        : std::runtime_error(message), line(line), column(column),
          code(diagnosticCodeForMessage(message)) {}

    SourceError(std::string message, int line, int column, std::string source)
        : std::runtime_error(message), line(line), column(column),
          source(std::move(source)), code(diagnosticCodeForMessage(message)) {}

    SourceError(std::string message, int line, int column, std::string source,
                std::string code)
        : std::runtime_error(std::move(message)), line(line), column(column),
          source(std::move(source)), code(std::move(code)) {}

    int line;
    int column;
    // The file the line/column refer to. Empty for the program the user ran;
    // set to the module path when the failure happened inside an imported
    // module, so the diagnostic is not misattributed to the importing program.
    std::string source;
    std::string code;
};

} // namespace lynxer
