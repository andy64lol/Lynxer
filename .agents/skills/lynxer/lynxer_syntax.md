# Lynxer syntax guide

A summarized syntax guide for the Lynxer language as the C++ interpreter in
`lynxer/` parses and runs it. For the full reference see
[`docs/language.md`](../../../docs/language.md).

---

## Program shape

Every program defines `global setup()` first and `global main()` last:

```lynx
global setup(){
    importAs("math", "m");
    const str LANG = "Lynxer";
}

global main(){
    println("Hello, ", LANG, " ", m.sqrt(144));
}
```

## Variables and types

```lynx
int x = 42;
float y = 3.14;
str name = "Lynxer";
bool flag = true;
any value = x;
const int frozen = 1;      // cannot be reassigned
list nums = [int 1, int 2, int 3];
tuple point = (10, 20);
```

## Functions

```lynx
global greet(str who) -> str { return "Hello, " + who; }
func helper(int a) -> int { return a + 1; }   // file-wide
global main(){
    local double_(int a) -> int { return a * 2; }  // nested
    println(greet(LANG));
}
```

## Control flow

```lynx
if (x > 10) { println("large"); }
elif (x > 5) { println("medium"); }
else { println("small"); }

while (x > 0) { x -= 1; }
for (int i = 0; i < 5; i = i + 1) { println(i); }
doWhile (x < 3) { x += 1; }
iterate 3 { println("tick"); }
forever { break; }

switch (x) {
    case 1 { println("one"); }
    default { println("other"); }
}

try { int z = 1 /% 0; } catch (str error) { println(error); }
```

## Operators

Word forms are canonical: `and`, `or`, `not`, `is`, `isnt`, `nand`, `nor`,
`bitand`, `bitor`, `bitxor`, `bitnot`, `bitleft`, `bitright`. Floor division is
`/%`, exponentiation `^`. Symbolic spellings (`==`, `&&`, `||`, `!`, `&`, `|`,
`<<`, `>>`) still parse but warn.

## Records

```lynx
struct Point { int x; int y; }
vargroup Player { str name = "Ada"; int score = 0; }
class Counter { int value = 0; local inc() -> int { this.value += 1; return this.value; } }
enum status = [ Ready, Failed(str reason) ]{}
```

## Modules

```lynx
import("math");              // global.math.sqrt(...)
importAs("os", "operating"); // global.operating.getcwd()
```

## Codeblocks

```lynx
codeblock greet = { println("hi"); };
exec(){{greet}}
```

## Command line

```bash
./lynxer/lynxer prog.lynx                 # run
./lynxer/lynxer --lint prog.lynx          # parse only
./lynxer/lynxer --ast prog.lynx           # print the AST
./lynxer/lynxer --format prog.lynx        # rewrite in canonical form
./lynxer/lynxer --compile prog.lynx -o prog   # standalone ELF executable
```

## Deliberate constraints

See [`docs/limitations.md`](../../../docs/limitations.md) for the behaviour that
is intentionally constrained or not implemented (no bytecode, no `rawPy`, no
Python module interop, module self-call rules, and so on).
