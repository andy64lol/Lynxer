# Error and warning messages

This is a message reference, not a description of diagnostic categories.
`{name}`, `{type}`, `{value}`, and similar placeholders stand for text Lynxer
inserts into a message. The set of possible final strings is therefore
runtime-dependent. Operating-system, compiler, loader, and native-library
messages are supplied by those external components and cannot be listed
verbatim here.

Source diagnostics are normally printed as
`file.lynx:line:column: [diagnosticCode] message`. Imported-source errors name
the imported file. Lint diagnostics use
`file.lynx:line:column: error[diagnosticCode]: message` or
`warning[diagnosticCode]: message`. Error-code identifiers are stable names
for a diagnostic category; names and dynamic values in messages are not part
of the code.

| Message | Code |
| --- | --- |
| `unterminated string` | `stringUnterminated` |
| `unterminated multiline comment; expected matching slash delimiter` | `commentMultilineUnterminated` |
| `unknown string escape ...` | `stringEscapeCharacterUnknown` |
| `expected macro declaration ...` | `macroDeclarationExpected` |
| `expected macro body` | `macroBodyExpected` |
| `duplicate macro ...` | `macroDuplicated` |
| `duplicate imported macro ...` | `macroImportDuplicated` |
| `expected parameter ...` | `parameterExpected` |
| `division by zero` | `divisionByZero` |
| `numeric value required` | `numericValueExpected` |
| `unary '-' requires a number` | `unaryNumberRequired` |
| `native value result is null` | `nativeValueResultNull` |
| `Struct '...' has no field '...'` | `structHasNoField` |

Other diagnostics also receive non-empty lower-camel-case codes. For source
errors not in the explicit code table, Lynxer forms the code from the
message's static words, omitting quoted names and numeric values.

## Lexer

- `unexpected character '{character}'`
- `unterminated multiline comment; expected matching slash delimiter`
- `unterminated string`
- `unterminated string escape`
- `unknown string escape \{character}`
- `unterminated char literal`
- `unterminated char escape`
- `unknown char escape \{character}`
- `char literal must contain exactly one character`

## Parser and declarations

- `expected macro declaration 'macro name!(*args){...}'`
- `macro declarations must use the variadic parameter '*args'`
- `expected macro body`
- `unterminated macro body`
- `macro expansion exceeded 64 nested expansions`
- `unknown macro '{name}'`
- `unterminated macro invocation`
- `macro '{name}' expects {count} argument(s), received {count}`
- `unterminated caller-supplied macro codeblock`
- `duplicate imported macro '{name}'`
- `duplicate macro '{name}'`
- `expected parameter after ','`
- `duplicate parameter name '{name}'`
- `a generic type parameter cannot have a default value`
- `required parameters cannot follow a parameter with a default value`
- `expected codeblock parameter after ','`
- `entry-point functions cannot use generic type parameters`
- `duplicate codeblock parameter '{name}'`
- `duplicate codeblock name '{name}'`
- `duplicate function parameter/codeblock name '{name}'`
- `entry-point functions cannot declare codeblock parameters`
- `cannot export entry-point function '{name}'`
- `exported functions cannot use generic type parameters`
- `exported functions cannot declare codeblock parameters`
- `duplicate named type '{name}'`
- `duplicate template parameter '{name}'`
- `a template declares its constructor parameters in its header; 'init' methods are not allowed`
- `unterminated enum body`
- `duplicate function '{name}'`
- `expected top-level function declaration`
- `program must define fn main()`
- `'build' requires a template; '{name}' is a class`
- `unknown template '{name}'`
- `expected 'named' after build arguments`
- `super(...) is only valid inside an init() constructor of a class that extends another class`
- `file-wide func declarations are only allowed at top level`
- `expected ';' after call`
- `missing '}' in interpolated string`
- `empty '{}' interpolation`
- `invalid expression in interpolated string`
- `stray '}' in interpolated string`
- `switch may contain only one default case`
- `expected 'case' or 'default' in switch`
- `expected 'str' type keyword for the catch variable`
- `unsupported compound assignment operator '{operator}'`
- `expected assignment or call`
- `expected '..' or '..=' in for range`
- `expected for-loop initializer`
- `expected ';' or ')' after for condition`
- `'{keyword}' is only valid inside a loop`
- `expected assignment operator`
- `unterminated {construct}`
- `integer literal '{literal}' is out of range`
- `expected ',' or ';' between vargroup fields`
- `expected an expression`
- `expected value after element type`
- `only named functions can be called directly`

Many grammar errors use an expected-token message, such as `expected
function name`, `expected '(' after function name`, `expected ')' after
parameters`, or `expected '{' before function body`. The specific expected
text depends on the construct being parsed.

Entry-point diagnostics:

- Warning: `setup() is optional; no setup() function was declared`
- Error: `program must define fn main()`
- Error: `entry-point functions cannot use generic type parameters`
- Error: `entry-point functions cannot declare codeblock parameters`

## Lint

The following are emitted as `error` or `warning` diagnostics by `--lint`:

- `{subject} has type '{actual}', which cannot be assigned to '{expected}'`
- `{subject} converts '{actual}' to '{expected}'; the value may be rejected or lose information at runtime`
- `function '{name}' expects {minimum} to {maximum} arguments, received {count}`
- `value '{name}' of type '{type}' is not callable`
- `method '{name}' expects {expected} arguments, received {count}`
- `struct '{name}' expects {expected} arguments, received {count}`
- `template '{name}' expects {expected} argument(s), received {count}`
- `constructor for '{name}' expects {expected} arguments, received {count}`
- `class '{name}' has no init() constructor but arguments were given`
- `unknown type '{name}'`
- `parent constructor expects {expected} arguments, received {count}`
- `parent class '{name}' has no init() constructor but arguments were given`
- `unused parameter '{name}'`
- `unused variable '{name}'`
- `unused method parameter or variable '{name}'`
- `unreachable statement`

## Runtime

- `unknown variable '{name}'`
- `unknown function '{name}'`
- `generic type '{name}' was inferred as both '{first type}' and '{second type}'`
- `Cannot write to borrowed variable '{name}'; the borrow is read-only`
- `variable '{name}' is constant and cannot be reassigned`
- `value cannot be assigned to type '{type}'`
- `value {value} is out of range for type '{type}'`
- `numeric value required`
- `division by zero`
- `shift count must be in range 0..63`
- `unsupported binary operator '{operator}'`
- `unsupported unary operator '{operator}'`
- `unary '-' requires a number`
- `for range bounds and step must be integers`
- `for range step must not be zero`
- `iterate() count must be an integer`
- `cannot access field of none`
- `value is not a function`
- `'{name}' is not a codeblock`
- `entry-point function '{name}' was not found`
- `module '{path}' was not found`
- `module path has no name`
- `could not load native module '{path}': {loader detail}`
- `native module lifecycle failure: missing lynxer_module_init_v1 entry point`
- `native module lifecycle failure: {detail}`
- `native module lifecycle failure: attach rejected the host API`
- `native value nesting exceeds 128 levels`
- `native value contains too many items`
- `native value ABI does not support type '{type}'`
- `native value contains an invalid text length`
- `native value result is null`
- `native value contains an invalid array`
- `native value contains an invalid record`
- `native value contains an invalid enum`
- `native value result has an unknown type tag`

Other runtime messages interpolate a name, type, field, method, or argument
count. Common templates include:

- `value of type '{type}' has no field '{name}'`
- `value of type '{type}' has no method '{name}'`
- `Instance of class '{type}' has no field or method '{name}'`
- `Field '{name}' of instance '{type}' is const and cannot be changed`
- `vargroup '{name}' has no field '{field}'`
- `Struct '{type}' has no field '{field}'`
- `method '{method}' of class '{type}' expects {expected} arguments, received {actual}`
- `struct '{type}' expects {expected} arguments, received {actual}`
- `template '{type}' expects {expected} arguments, received {actual}`
- `class '{type}' has no init() constructor but arguments were given`
- `unknown type '{type}'`
- `function '{name}' expects at most {expected} arguments, received {actual}`
- `function '{name}' expects at least {expected} arguments, received {actual}`
- `function '{name}' expects exactly {expected} code block(s), but got {actual}`
- `exec() expects {expected} values, received {actual}`

Builtins have additional argument, type, range, and state errors. Representative
exact messages include:

- `an integer value is required`
- `input() takes 0 or 1 arguments`
- `inputln() takes 0 or 1 arguments`
- `Cannot convert '{value}' to int`
- `Cannot convert '{value}' to float`
- `range() step cannot be 0`
- `listPop() called on an empty list`
- `listHead() count cannot be negative`
- `tupleGet(tuple, idx) expects a tuple and an integer index`
- `assert(condition[, message]) expects a boolean or number condition and an optional string message`
- `sleep() duration cannot be negative`
- `memory access is out of bounds`
- `address refers to freed memory`
- `invalid native memory address`
- `native handle has been freed`
- `nativeCall() function address must be non-zero`

The complete builtin API and its argument conventions are in
[builtins.md](builtins.md). Native standard-library functions may instead
return documented sentinels or error strings; those are listed in
[stdlib-contracts.md](stdlib-contracts.md#error-sentinels) and the individual
module references, and are not necessarily Lynxer diagnostics.

## Warnings

- `setup() is optional; no setup() function was declared`
- `'{keyword}' function declarations are deprecated; use 'fn'`
- `operator '{operator}' is deprecated; use '{replacement}' instead.`
- `tuple assigned from a bracketed list is deprecated; use the '(int 1, int 2)' form instead.`
- `forever() has no break; it will run until the process is stopped. Add break; or call suppressForeverWarning() in fn setup(){}.`
- `unused parameter '{name}'`
- `unused variable '{name}'`
- `unused method parameter or variable '{name}'`
- `unreachable statement`
- `{subject} converts '{actual}' to '{expected}'; the value may be rejected or lose information at runtime`

Deprecation warnings can be suppressed with `suppressDeprecationWarning()` in
`setup()`. The no-`break` warning can be suppressed with
`suppressForeverWarning()` in `setup()`. Lint warnings are not suppressed by
these functions.

## CLI and build

- `lynxer: file not found: '{path}'`
- `lynxer: could not read '{path}': {detail}`
- `lynxer: {command} requires exactly one file argument`
- `lynxer: --compile requires a .lynx file, optionally followed by --include <file> inputs and an output name`
- `lynxer: compile failed: {detail}`
- `lynxer: the embedded program payload is invalid`
- `lynxer: bytecode files are no longer supported; compile the .lynx source with --compile instead`
- `lynxer: interpreter failure in '{path}': {detail}`
- `lynxer: comprehensive validator is not available`
- `lynxer: could not write '{path}'`
- `lynxer: --debug requires a source file`
- `lynxer: unexpected extra argument '{argument}'`
- `lynxer: {option} requires a file`
- `lynxer: {option} requires a name`
- `lynxer: the output name was given twice`
- `lynxer: the first input must be a .lynx program file`

`--emit-library`, install, uninstall, and linker failures include additional
operation-specific text and may include operating-system or toolchain messages.
See [CLI](CLI.md) for command behavior and exit codes.

## Standard-library returned errors

These are returned by APIs rather than raised as Lynxer runtime errors:

- `Error: node not found on PATH`
- `Error: node timed out after {seconds}s`
- `Error: node exited with status {status}`
- `ERROR: could not read '{path}'`
- `ERROR: rows must be a JSON array`
- `ERROR: rows must be a JSON array of objects`
- `ERROR: could not write '{path}'`
- `ERROR: write failed`
- `ERROR: row must be a JSON array`

Other modules return their documented scalar/status sentinels or backend error
text; consult the module-specific references for those results.
