# typing

String, list and simple type utilities.

Main helpers:

- Conversion: `toStr(n)`, `toInt(s)`, `toFloat(s)`, `toBool(n)`.
- Checks: `isNumeric(s)`, `isList(val)`, `isAlpha(s)`, `isDigit(s)`, `isAlphaNum(s)`, `isSpace(s)`.
- String length/list length: `lenStr(s)`, `lenList(lst)`.
- List and string helpers: `toList(s,sep)`, `repeat(s,n)`, `repeatStr(s,n)`, `contains(haystack,needle)`, `indexOf(s,sub)`, `splitFirst(s,sep)`.
- Transformations: `trim(s)`, `upper(s)`, `lower(s)`, `substr(s,start,end)`, `padLeft(s,width,ch)`, `padRight(s,width,ch)`, `strReverse(s)`, `charCode(s)`, `charOf(code)`, `countOccurrences(s,sub)`, `replace(s,old,new)`, `titleCase(s)`, `swapCase(s)`, `center(s,width,ch)`.
- List utilities: `flatten(lst)`, `unique(lst)`.
- Helpers: `spaces(n)`, `wordWrap(s,width)`, `expandTabs(s,tabSize)`.

Notes:
- Many functions use Python fallbacks and return safe defaults on error.