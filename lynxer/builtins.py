"""Lynxer built-in functions, implementations, and runtime registry.

The interpreter value types live in :mod:`lynxer.lynxer`.  This module is
imported after those types have been defined, so it can own the complete
implementation of every built-in without making the runtime import cycle
fragile.
"""

from __future__ import annotations

import sys
from collections.abc import Callable
from typing import Any

from . import lynxer as _runtime


BaseFunction = _runtime.BaseFunction
CoroutineValue = _runtime.CoroutineValue
List = _runtime.List
LynxTuple = _runtime.LynxTuple
Number = _runtime.Number
RTError = _runtime.RTError
RTResult = _runtime.RTResult
String = _runtime.String
_get_cython_inline = _runtime._get_cython_inline


class BuiltInFunction(BaseFunction):
    """A callable implemented by Python and exposed to Lynxer programs."""

    def execute(self, args):
        res = RTResult()
        exec_ctx = self.generate_new_context()

        method_name = f"execute_{self.name}"
        method = getattr(self, method_name, self.no_visit_method)
        return_value = res.register(method(args, exec_ctx))

        if res.should_return():
            return res
        return res.success(return_value)

    def no_visit_method(self, node, context):
        raise Exception(f"No execute_{self.name} method defined")

    def copy(self):
        c = BuiltInFunction(self.name)
        c.set_context(self.context)
        c.set_pos(self.pos_start, self.pos_end)
        return c

    def __repr__(self):
        return f"<built-in {self.name}>"

    def execute_print(self, args, exec_ctx):
        output = "".join(str(a) for a in args)
        sys.stdout.write(output)
        sys.stdout.flush()
        return RTResult().success(Number.null)

    def execute_println(self, args, exec_ctx):
        output = "".join(str(a) for a in args)
        sys.stdout.write(output + "\n")
        sys.stdout.flush()
        return RTResult().success(Number.null)

    def execute_input(self, args, exec_ctx):
        if len(args) > 1:
            return RTResult().failure(
                RTError(
                    self.pos_start,
                    self.pos_end,
                    "input() takes 0 or 1 arguments",
                    exec_ctx,
                )
            )
        prompt = str(args[0]) if args else ""
        text = input(prompt)
        return RTResult().success(String(text))

    def execute_inputln(self, args, exec_ctx):
        if len(args) > 1:
            return RTResult().failure(
                RTError(
                    self.pos_start,
                    self.pos_end,
                    "inputln() takes 0 or 1 arguments",
                    exec_ctx,
                )
            )
        prompt = str(args[0]) if args else ""
        text = input(prompt)
        return RTResult().success(String(text + "\n"))

    def execute_rawPy(self, args, exec_ctx):
        if len(args) != 1 or not isinstance(args[0], String):
            return RTResult().failure(
                RTError(
                    self.pos_start,
                    self.pos_end,
                    'rawPy() expects exactly one string argument — rawPy("python code")',
                    exec_ctx,
                )
            )
        try:
            exec(args[0].value, {"__builtins__": __builtins__})
        except Exception as e:
            return RTResult().failure(
                RTError(
                    self.pos_start,
                    self.pos_end,
                    f"Python error in rawPy(): {e}",
                    exec_ctx,
                )
            )
        return RTResult().success(Number.null)

    def execute_strOf(self, args, exec_ctx):
        if len(args) != 1:
            return RTResult().failure(
                RTError(
                    self.pos_start,
                    self.pos_end,
                    "strOf() takes exactly 1 argument",
                    exec_ctx,
                )
            )
        return RTResult().success(String(str(args[0])))

    def execute_intOf(self, args, exec_ctx):
        if len(args) != 1:
            return RTResult().failure(
                RTError(
                    self.pos_start,
                    self.pos_end,
                    "intOf() takes exactly 1 argument",
                    exec_ctx,
                )
            )
        v = args[0]
        try:
            return RTResult().success(Number(int(float(v.value))))
        except Exception:
            return RTResult().failure(
                RTError(
                    self.pos_start,
                    self.pos_end,
                    f"Cannot convert '{v}' to int",
                    exec_ctx,
                )
            )

    def execute_floatOf(self, args, exec_ctx):
        if len(args) != 1:
            return RTResult().failure(
                RTError(
                    self.pos_start,
                    self.pos_end,
                    "floatOf() takes exactly 1 argument",
                    exec_ctx,
                )
            )
        v = args[0]
        try:
            return RTResult().success(Number(float(v.value)))
        except Exception:
            return RTResult().failure(
                RTError(
                    self.pos_start,
                    self.pos_end,
                    f"Cannot convert '{v}' to float",
                    exec_ctx,
                )
            )

    def execute_rawPyx(self, args, exec_ctx):
        if len(args) != 1 or not isinstance(args[0], String):
            return RTResult().failure(
                RTError(
                    self.pos_start,
                    self.pos_end,
                    'rawPyx() expects exactly one string argument — rawPyx("cython code")',
                    exec_ctx,
                )
            )
        try:
            cython_inline = _get_cython_inline()
            cy_locals = {}
            cython_inline(args[0].value, locals=cy_locals, globals=cy_locals, quiet=True)
        except Exception as e:
            return RTResult().failure(
                RTError(
                    self.pos_start,
                    self.pos_end,
                    f"Cython error in rawPyx(): {type(e).__name__}: {e}",
                    exec_ctx,
                )
            )
        return RTResult().success(Number.null)

    def execute_returnType(self, args, exec_ctx):
        if len(args) != 1:
            return RTResult().failure(
                RTError(
                    self.pos_start,
                    self.pos_end,
                    "returnType() takes exactly 1 argument",
                    exec_ctx,
                )
            )
        return RTResult().success(String(_runtime.value_type_name(args[0])))

    def execute_returnLength(self, args, exec_ctx):
        if len(args) != 1:
            return RTResult().failure(
                RTError(
                    self.pos_start,
                    self.pos_end,
                    "returnLength() takes exactly 1 argument",
                    exec_ctx,
                )
            )
        v = args[0]
        if isinstance(v, String):
            return RTResult().success(Number(len(v.value)))
        if isinstance(v, (List, LynxTuple)):
            return RTResult().success(Number(len(v.elements)))
        return RTResult().failure(
            RTError(
                self.pos_start,
                self.pos_end,
                f"returnLength() does not support values of type '{type(v).__name__}'",
                exec_ctx,
            )
        )

    def execute_seqFromTo(self, args, exec_ctx):
        if len(args) != 3 or not all(isinstance(a, Number) for a in args):
            return RTResult().failure(
                RTError(
                    self.pos_start,
                    self.pos_end,
                    "seqFromTo() expects exactly 3 numeric arguments — seqFromTo(start, stop, step)",
                    exec_ctx,
                )
            )
        start, stop, step = (int(a.value) for a in args)
        if step == 0:
            return RTResult().failure(
                RTError(
                    self.pos_start,
                    self.pos_end,
                    "seqFromTo() step cannot be 0",
                    exec_ctx,
                )
            )
        elements = [Number(n).set_context(exec_ctx) for n in range(start, stop, step)]
        return RTResult().success(List(elements))

    def execute_range(self, args, exec_ctx):
        """range(stop), range(start, stop), or range(start, stop, step)."""
        if not args or len(args) > 3 or not all(isinstance(a, Number) for a in args):
            return RTResult().failure(
                RTError(
                    self.pos_start,
                    self.pos_end,
                    "range() expects 1, 2, or 3 integer arguments: "
                    "range(stop), range(start, stop), or range(start, stop, step)",
                    exec_ctx,
                )
            )
        if len(args) == 1:
            start, stop, step = 0, int(args[0].value), 1
        elif len(args) == 2:
            start, stop, step = int(args[0].value), int(args[1].value), 1
        else:
            start, stop, step = (
                int(args[0].value),
                int(args[1].value),
                int(args[2].value),
            )
        if step == 0:
            return RTResult().failure(
                RTError(
                    self.pos_start,
                    self.pos_end,
                    "range() step cannot be 0",
                    exec_ctx,
                )
            )
        elements = [Number(n).set_context(exec_ctx) for n in range(start, stop, step)]
        return RTResult().success(List(elements))

    def execute_cleanRawPyxCache(self, args, exec_ctx):
        import os
        import shutil

        cache_dir = os.path.expanduser("~/.cython/inline")
        try:
            if os.path.isdir(cache_dir):
                shutil.rmtree(cache_dir)
        except Exception as e:
            return RTResult().failure(
                RTError(
                    self.pos_start,
                    self.pos_end,
                    f"cleanRawPyxCache() failed: {e}",
                    exec_ctx,
                )
            )
        return RTResult().success(Number.null)

    # list built-ins

    def execute_listJsonArray(self, args, exec_ctx):
        import json as _json

        if len(args) != 1 or not isinstance(args[0], List):
            return RTResult().failure(
                RTError(
                    self.pos_start,
                    self.pos_end,
                    "listJsonArray(list) expects a list",
                    exec_ctx,
                )
            )
        try:
            items = [
                e.value if isinstance(e, (Number, String)) else str(e)
                for e in args[0].elements
            ]
            return RTResult().success(String(_json.dumps(items)))
        except Exception as e:
            return RTResult().failure(
                RTError(
                    self.pos_start,
                    self.pos_end,
                    f"listJsonArray() failed: {e}",
                    exec_ctx,
                )
            )

    def execute_listJsonObject(self, args, exec_ctx):
        import json as _json

        if len(args) != 1 or not isinstance(args[0], List):
            return RTResult().failure(
                RTError(
                    self.pos_start,
                    self.pos_end,
                    "listJsonObject(list) expects a flat key/value list",
                    exec_ctx,
                )
            )
        els = args[0].elements
        if len(els) % 2 != 0:
            return RTResult().failure(
                RTError(
                    self.pos_start,
                    self.pos_end,
                    "listJsonObject() requires an even-length list (key, value, key, value, ...)",
                    exec_ctx,
                )
            )
        try:
            obj = {}
            for i in range(0, len(els), 2):
                k = els[i].value if isinstance(els[i], (Number, String)) else str(els[i])
                v = (
                    els[i + 1].value
                    if isinstance(els[i + 1], (Number, String))
                    else str(els[i + 1])
                )
                obj[str(k)] = v
            return RTResult().success(String(_json.dumps(obj)))
        except Exception as e:
            return RTResult().failure(
                RTError(
                    self.pos_start,
                    self.pos_end,
                    f"listJsonObject() failed: {e}",
                    exec_ctx,
                )
            )

    def execute_splitStr(self, args, exec_ctx):
        if (
            len(args) != 2
            or not isinstance(args[0], String)
            or not isinstance(args[1], String)
        ):
            return RTResult().failure(
                RTError(
                    self.pos_start,
                    self.pos_end,
                    "splitStr(str, sep) expects two string arguments",
                    exec_ctx,
                )
            )
        parts = args[0].value.split(args[1].value)
        elements = [String(p).set_context(exec_ctx) for p in parts]
        return RTResult().success(List(elements))

    def execute_listFlatten(self, args, exec_ctx):
        if len(args) != 1 or not isinstance(args[0], List):
            return RTResult().failure(
                RTError(
                    self.pos_start,
                    self.pos_end,
                    "listFlatten(list) expects a list",
                    exec_ctx,
                )
            )
        flat = []
        for el in args[0].elements:
            if isinstance(el, List):
                flat.extend(el.elements)
            else:
                flat.append(el)
        return RTResult().success(List(flat))

    def execute_listUnique(self, args, exec_ctx):
        if len(args) != 1 or not isinstance(args[0], List):
            return RTResult().failure(
                RTError(
                    self.pos_start,
                    self.pos_end,
                    "listUnique(list) expects a list",
                    exec_ctx,
                )
            )
        seen_strs: list[str] = []
        unique_els = []
        for el in args[0].elements:
            s = str(el)
            if s not in seen_strs:
                seen_strs.append(s)
                unique_els.append(el)
        return RTResult().success(List(unique_els))

    def execute_listPush(self, args, exec_ctx):
        if len(args) != 2 or not isinstance(args[0], List):
            return RTResult().failure(
                RTError(
                    self.pos_start,
                    self.pos_end,
                    "listPush(list, item) expects a list and a value",
                    exec_ctx,
                )
            )
        new_elements = list(args[0].elements) + [args[1]]
        return RTResult().success(List(new_elements))

    def execute_listPop(self, args, exec_ctx):
        if len(args) != 1 or not isinstance(args[0], List):
            return RTResult().failure(
                RTError(
                    self.pos_start,
                    self.pos_end,
                    "listPop(list) expects a list",
                    exec_ctx,
                )
            )
        if not args[0].elements:
            return RTResult().failure(
                RTError(
                    self.pos_start,
                    self.pos_end,
                    "listPop() called on an empty list",
                    exec_ctx,
                )
            )
        return RTResult().success(args[0].elements.pop())

    def execute_listGet(self, args, exec_ctx):
        if (
            len(args) != 2
            or not isinstance(args[0], List)
            or not isinstance(args[1], Number)
        ):
            return RTResult().failure(
                RTError(
                    self.pos_start,
                    self.pos_end,
                    "listGet(list, idx) expects a list and an integer index",
                    exec_ctx,
                )
            )
        lst = args[0]
        idx = int(args[1].value)
        if idx < -len(lst.elements) or idx >= len(lst.elements):
            return RTResult().failure(
                RTError(
                    self.pos_start,
                    self.pos_end,
                    f"listGet() index {idx} out of range for list of length {len(lst.elements)}",
                    exec_ctx,
                )
            )
        return RTResult().success(lst.elements[idx])

    def execute_listSet(self, args, exec_ctx):
        if (
            len(args) != 3
            or not isinstance(args[0], List)
            or not isinstance(args[1], Number)
        ):
            return RTResult().failure(
                RTError(
                    self.pos_start,
                    self.pos_end,
                    "listSet(list, idx, val) expects a list, an integer index, and a value",
                    exec_ctx,
                )
            )
        lst = args[0]
        idx = int(args[1].value)
        if idx < -len(lst.elements) or idx >= len(lst.elements):
            return RTResult().failure(
                RTError(
                    self.pos_start,
                    self.pos_end,
                    f"listSet() index {idx} out of range for list of length {len(lst.elements)}",
                    exec_ctx,
                )
            )
        new_elements = list(lst.elements)
        new_elements[idx] = args[2]
        return RTResult().success(List(new_elements))

    def execute_listSlice(self, args, exec_ctx):
        if (
            len(args) != 3
            or not isinstance(args[0], List)
            or not isinstance(args[1], Number)
            or not isinstance(args[2], Number)
        ):
            return RTResult().failure(
                RTError(
                    self.pos_start,
                    self.pos_end,
                    "listSlice(list, start, stop) expects a list and two integer indices",
                    exec_ctx,
                )
            )
        start = int(args[1].value)
        stop = int(args[2].value)
        return RTResult().success(List(args[0].elements[start:stop]))

    def execute_listContains(self, args, exec_ctx):
        if len(args) != 2 or not isinstance(args[0], List):
            return RTResult().failure(
                RTError(
                    self.pos_start,
                    self.pos_end,
                    "listContains(list, item) expects a list and a value",
                    exec_ctx,
                )
            )
        target = str(args[1])
        found = any(str(e) == target for e in args[0].elements)
        return RTResult().success(Number(1 if found else 0, is_bool=True))

    def execute_contains(self, args, exec_ctx):
        """contains(sequence, value) — membership for lists and tuples."""
        if len(args) != 2 or not isinstance(args[0], (List, LynxTuple)):
            return RTResult().failure(
                RTError(
                    self.pos_start,
                    self.pos_end,
                    "contains(list_or_tuple, value) expects a list or tuple and a value",
                    exec_ctx,
                )
            )
        target = str(args[1])
        found = any(str(element) == target for element in args[0].elements)
        return RTResult().success(Number(1 if found else 0, is_bool=True))

    def execute_listJoin(self, args, exec_ctx):
        if (
            len(args) != 2
            or not isinstance(args[0], List)
            or not isinstance(args[1], String)
        ):
            return RTResult().failure(
                RTError(
                    self.pos_start,
                    self.pos_end,
                    "listJoin(list, sep) expects a list and a string separator",
                    exec_ctx,
                )
            )
        sep = args[1].value
        result = sep.join(str(e) for e in args[0].elements)
        return RTResult().success(String(result))

    def execute_listIndex(self, args, exec_ctx):
        if len(args) != 2 or not isinstance(args[0], List):
            return RTResult().failure(
                RTError(
                    self.pos_start,
                    self.pos_end,
                    "listIndex(list, item) expects a list and a value",
                    exec_ctx,
                )
            )
        target = str(args[1])
        for i, e in enumerate(args[0].elements):
            if str(e) == target:
                return RTResult().success(Number(i))
        return RTResult().success(Number(-1))

    def execute_listRemove(self, args, exec_ctx):
        if (
            len(args) != 2
            or not isinstance(args[0], List)
            or not isinstance(args[1], Number)
        ):
            return RTResult().failure(
                RTError(
                    self.pos_start,
                    self.pos_end,
                    "listRemove(list, idx) expects a list and an integer index",
                    exec_ctx,
                )
            )
        lst = args[0]
        idx = int(args[1].value)
        if idx < -len(lst.elements) or idx >= len(lst.elements):
            return RTResult().failure(
                RTError(
                    self.pos_start,
                    self.pos_end,
                    f"listRemove() index {idx} out of range for list of length {len(lst.elements)}",
                    exec_ctx,
                )
            )
        new_elements = list(lst.elements)
        new_elements.pop(idx)
        return RTResult().success(List(new_elements))

    def execute_anyOf(self, args, exec_ctx):
        if len(args) != 1 or not isinstance(args[0], List):
            return RTResult().failure(
                RTError(
                    self.pos_start,
                    self.pos_end,
                    "anyOf(list) expects a list",
                    exec_ctx,
                )
            )
        result = any(e.is_true() for e in args[0].elements)
        return RTResult().success(Number(1 if result else 0, is_bool=True))

    def execute_allOf(self, args, exec_ctx):
        if len(args) != 1 or not isinstance(args[0], List):
            return RTResult().failure(
                RTError(
                    self.pos_start,
                    self.pos_end,
                    "allOf(list) expects a list",
                    exec_ctx,
                )
            )
        result = all(e.is_true() for e in args[0].elements)
        return RTResult().success(Number(1 if result else 0, is_bool=True))

    def execute_sumOf(self, args, exec_ctx):
        if len(args) != 1 or not isinstance(args[0], List):
            return RTResult().failure(
                RTError(
                    self.pos_start,
                    self.pos_end,
                    "sumOf(list) expects a list",
                    exec_ctx,
                )
            )
        try:
            total = sum(e.value for e in args[0].elements if isinstance(e, Number))
            return RTResult().success(Number(total))
        except Exception as e:
            return RTResult().failure(
                RTError(
                    self.pos_start,
                    self.pos_end,
                    f"sumOf() failed: {e}",
                    exec_ctx,
                )
            )

    def _list_sort_key(self, e):
        if isinstance(e, (Number, String)):
            return e.value
        return str(e)

    def execute_sortList(self, args, exec_ctx):
        if len(args) not in (1, 2) or not isinstance(args[0], List):
            return RTResult().failure(
                RTError(
                    self.pos_start,
                    self.pos_end,
                    "sortList(list) or sortList(list, reverse) expects a list",
                    exec_ctx,
                )
            )
        reverse = args[1].is_true() if len(args) == 2 else False
        try:
            sorted_els = sorted(
                args[0].elements, key=self._list_sort_key, reverse=reverse
            )
            return RTResult().success(List(sorted_els))
        except Exception as e:
            return RTResult().failure(
                RTError(
                    self.pos_start,
                    self.pos_end,
                    f"sortList() failed: {e}",
                    exec_ctx,
                )
            )

    def execute_reverseList(self, args, exec_ctx):
        if len(args) != 1 or not isinstance(args[0], List):
            return RTResult().failure(
                RTError(
                    self.pos_start,
                    self.pos_end,
                    "reverseList(list) expects a list",
                    exec_ctx,
                )
            )
        return RTResult().success(List(list(reversed(args[0].elements))))

    def execute_listMin(self, args, exec_ctx):
        if len(args) != 1 or not isinstance(args[0], List):
            return RTResult().failure(
                RTError(
                    self.pos_start,
                    self.pos_end,
                    "listMin(list) expects a list",
                    exec_ctx,
                )
            )
        if not args[0].elements:
            return RTResult().failure(
                RTError(
                    self.pos_start,
                    self.pos_end,
                    "listMin() called on an empty list",
                    exec_ctx,
                )
            )
        try:
            return RTResult().success(
                min(args[0].elements, key=self._list_sort_key)
            )
        except Exception as e:
            return RTResult().failure(
                RTError(
                    self.pos_start,
                    self.pos_end,
                    f"listMin() failed: {e}",
                    exec_ctx,
                )
            )

    def execute_listMax(self, args, exec_ctx):
        if len(args) != 1 or not isinstance(args[0], List):
            return RTResult().failure(
                RTError(
                    self.pos_start,
                    self.pos_end,
                    "listMax(list) expects a list",
                    exec_ctx,
                )
            )
        if not args[0].elements:
            return RTResult().failure(
                RTError(
                    self.pos_start,
                    self.pos_end,
                    "listMax() called on an empty list",
                    exec_ctx,
                )
            )
        try:
            return RTResult().success(
                max(args[0].elements, key=self._list_sort_key)
            )
        except Exception as e:
            return RTResult().failure(
                RTError(
                    self.pos_start,
                    self.pos_end,
                    f"listMax() failed: {e}",
                    exec_ctx,
                )
            )

    # tuple built-ins

    def execute_tupleCreate(self, args, exec_ctx):
        """tupleCreate(v1, v2, ...) — create a tuple from any number of arguments."""
        return RTResult().success(LynxTuple(args))

    def execute_tupleGet(self, args, exec_ctx):
        if (
            len(args) != 2
            or not isinstance(args[0], LynxTuple)
            or not isinstance(args[1], Number)
        ):
            return RTResult().failure(
                RTError(
                    self.pos_start,
                    self.pos_end,
                    "tupleGet(tuple, idx) expects a tuple and an integer index",
                    exec_ctx,
                )
            )
        t = args[0]
        idx = int(args[1].value)
        if idx < -len(t.elements) or idx >= len(t.elements):
            return RTResult().failure(
                RTError(
                    self.pos_start,
                    self.pos_end,
                    f"tupleGet() index {idx} out of range for tuple of length {len(t.elements)}",
                    exec_ctx,
                )
            )
        return RTResult().success(t.elements[idx])

    def execute_tupleLen(self, args, exec_ctx):
        if len(args) != 1 or not isinstance(args[0], LynxTuple):
            return RTResult().failure(
                RTError(
                    self.pos_start,
                    self.pos_end,
                    "tupleLen(tuple) expects a tuple",
                    exec_ctx,
                )
            )
        return RTResult().success(Number(len(args[0].elements)))

    def execute_tupleContains(self, args, exec_ctx):
        if len(args) != 2 or not isinstance(args[0], LynxTuple):
            return RTResult().failure(
                RTError(
                    self.pos_start,
                    self.pos_end,
                    "tupleContains(tuple, val) expects a tuple and a value",
                    exec_ctx,
                )
            )
        target = str(args[1])
        found = any(str(e) == target for e in args[0].elements)
        return RTResult().success(Number(1 if found else 0, is_bool=True))

    def execute_tupleIndex(self, args, exec_ctx):
        if len(args) != 2 or not isinstance(args[0], LynxTuple):
            return RTResult().failure(
                RTError(
                    self.pos_start,
                    self.pos_end,
                    "tupleIndex(tuple, val) expects a tuple and a value",
                    exec_ctx,
                )
            )
        target = str(args[1])
        for i, e in enumerate(args[0].elements):
            if str(e) == target:
                return RTResult().success(Number(i))
        return RTResult().success(Number(-1))

    def execute_tupleSlice(self, args, exec_ctx):
        if (
            len(args) != 3
            or not isinstance(args[0], LynxTuple)
            or not isinstance(args[1], Number)
            or not isinstance(args[2], Number)
        ):
            return RTResult().failure(
                RTError(
                    self.pos_start,
                    self.pos_end,
                    "tupleSlice(tuple, start, stop) expects a tuple and two integer indices",
                    exec_ctx,
                )
            )
        start = int(args[1].value)
        stop = int(args[2].value)
        return RTResult().success(LynxTuple(args[0].elements[start:stop]))

    def execute_tupleToList(self, args, exec_ctx):
        if len(args) != 1 or not isinstance(args[0], LynxTuple):
            return RTResult().failure(
                RTError(
                    self.pos_start,
                    self.pos_end,
                    "tupleToList(tuple) expects a tuple",
                    exec_ctx,
                )
            )
        return RTResult().success(List(list(args[0].elements)))

    def execute_listToTuple(self, args, exec_ctx):
        if len(args) != 1 or not isinstance(args[0], List):
            return RTResult().failure(
                RTError(
                    self.pos_start,
                    self.pos_end,
                    "listToTuple(list) expects a list",
                    exec_ctx,
                )
            )
        return RTResult().success(LynxTuple(args[0].elements))

    def execute_tupleConcat(self, args, exec_ctx):
        if (
            len(args) != 2
            or not isinstance(args[0], LynxTuple)
            or not isinstance(args[1], LynxTuple)
        ):
            return RTResult().failure(
                RTError(
                    self.pos_start,
                    self.pos_end,
                    "tupleConcat(t1, t2) expects two tuples",
                    exec_ctx,
                )
            )
        return RTResult().success(LynxTuple(args[0].elements + args[1].elements))

    def execute_tupleCount(self, args, exec_ctx):
        if len(args) != 2 or not isinstance(args[0], LynxTuple):
            return RTResult().failure(
                RTError(
                    self.pos_start,
                    self.pos_end,
                    "tupleCount(tuple, val) expects a tuple and a value",
                    exec_ctx,
                )
            )
        target = str(args[1])
        count = sum(1 for e in args[0].elements if str(e) == target)
        return RTResult().success(Number(count))

    def execute_tupleFirst(self, args, exec_ctx):
        if len(args) != 1 or not isinstance(args[0], LynxTuple):
            return RTResult().failure(
                RTError(
                    self.pos_start,
                    self.pos_end,
                    "tupleFirst(tuple) expects a tuple",
                    exec_ctx,
                )
            )
        if not args[0].elements:
            return RTResult().failure(
                RTError(
                    self.pos_start,
                    self.pos_end,
                    "tupleFirst() called on an empty tuple",
                    exec_ctx,
                )
            )
        return RTResult().success(args[0].elements[0])

    def execute_tupleLast(self, args, exec_ctx):
        if len(args) != 1 or not isinstance(args[0], LynxTuple):
            return RTResult().failure(
                RTError(
                    self.pos_start,
                    self.pos_end,
                    "tupleLast(tuple) expects a tuple",
                    exec_ctx,
                )
            )
        if not args[0].elements:
            return RTResult().failure(
                RTError(
                    self.pos_start,
                    self.pos_end,
                    "tupleLast() called on an empty tuple",
                    exec_ctx,
                )
            )
        return RTResult().success(args[0].elements[-1])

    def execute_tupleJsonArray(self, args, exec_ctx):
        import json as _json

        if len(args) != 1 or not isinstance(args[0], LynxTuple):
            return RTResult().failure(
                RTError(
                    self.pos_start,
                    self.pos_end,
                    "tupleJsonArray(tuple) expects a tuple",
                    exec_ctx,
                )
            )
        try:
            items = [
                e.value if isinstance(e, (Number, String)) else str(e)
                for e in args[0].elements
            ]
            return RTResult().success(String(_json.dumps(items)))
        except Exception as e:
            return RTResult().failure(
                RTError(
                    self.pos_start,
                    self.pos_end,
                    f"tupleJsonArray() failed: {e}",
                    exec_ctx,
                )
            )

    # async built-ins

    def execute_asyncRun(self, args, exec_ctx):
        """asyncRun(coro) — run a coroutine."""
        import asyncio

        if len(args) != 1 or not isinstance(args[0], CoroutineValue):
            return RTResult().failure(
                RTError(
                    self.pos_start,
                    self.pos_end,
                    "asyncRun(coro) expects a single coroutine argument "
                    "(the result of calling an 'async' function)",
                    exec_ctx,
                )
            )
        try:
            coro_res = asyncio.run(args[0].coro)
        except Exception as e:
            return RTResult().failure(
                RTError(
                    self.pos_start,
                    self.pos_end,
                    f"asyncRun() raised an exception: {type(e).__name__}: {e}",
                    exec_ctx,
                )
            )
        if isinstance(coro_res, RTResult):
            if coro_res.error:
                return RTResult().failure(coro_res.error)
            return RTResult().success(
                coro_res.value if coro_res.value is not None else Number.null
            )
        return RTResult().success(Number.null)

    def execute_asyncGather(self, args, exec_ctx):
        """asyncGather(coro1, coro2, ...) — return a coroutine."""
        for i, arg in enumerate(args):
            if not isinstance(arg, CoroutineValue):
                return RTResult().failure(
                    RTError(
                        self.pos_start,
                        self.pos_end,
                        f"asyncGather() argument {i + 1} is not a coroutine "
                        "(expected the result of calling an 'async' function)",
                        exec_ctx,
                    )
                )

        import asyncio

        coros = [arg.coro for arg in args]

        async def _gather():
            results = await asyncio.gather(*coros)
            elements = []
            for r in results:
                if isinstance(r, RTResult):
                    if r.error:
                        return r
                    elements.append(r.value if r.value is not None else Number.null)
                else:
                    elements.append(Number.null)
            return RTResult().success(List(elements))

        return RTResult().success(CoroutineValue(_gather()))

    def execute_sleep(self, args, exec_ctx):
        """sleep(seconds) — block the current execution for a number of seconds."""
        import time

        if len(args) != 1 or not isinstance(args[0], Number) or args[0].is_bool:
            return RTResult().failure(
                RTError(
                    self.pos_start,
                    self.pos_end,
                    "sleep(num) expects exactly one int or float argument",
                    exec_ctx,
                )
            )

        seconds = float(args[0].value)
        if seconds < 0:
            return RTResult().failure(
                RTError(
                    self.pos_start,
                    self.pos_end,
                    "sleep(num) cannot use a negative number of seconds",
                    exec_ctx,
                )
            )

        time.sleep(seconds)
        return RTResult().success(Number.null)

    def execute_asyncSleep(self, args, exec_ctx):
        """asyncSleep(seconds) — return a coroutine."""
        import asyncio

        if len(args) != 1 or not isinstance(args[0], Number):
            return RTResult().failure(
                RTError(
                    self.pos_start,
                    self.pos_end,
                    "asyncSleep(seconds) expects a single numeric argument",
                    exec_ctx,
                )
            )
        seconds = args[0].value

        async def _sleep():
            await asyncio.sleep(seconds)
            return RTResult().success(Number.null)

        return RTResult().success(CoroutineValue(_sleep()))

    def execute_foreverDelay(self, args, exec_ctx):
        """foreverDelay(seconds) — configure the delay used by forever()."""
        if not _runtime._setup_in_progress:
            return RTResult().failure(
                RTError(
                    self.pos_start,
                    self.pos_end,
                    "foreverDelay() may only be called inside global setup(){}",
                    exec_ctx,
                )
            )
        if len(args) != 1 or not isinstance(args[0], Number):
            return RTResult().failure(
                RTError(
                    self.pos_start,
                    self.pos_end,
                    "foreverDelay(seconds) expects exactly one number",
                    exec_ctx,
                )
            )
        delay = float(args[0].value)
        if delay < 0:
            return RTResult().failure(
                RTError(
                    self.pos_start,
                    self.pos_end,
                    "foreverDelay(seconds) cannot be negative",
                    exec_ctx,
                )
            )
        _runtime._forever_delay = delay
        return RTResult().success(Number.null)

    def execute_suppressForeverWarning(self, args, exec_ctx):
        """Suppress the warning for forever() bodies without break."""
        if not _runtime._setup_in_progress:
            return RTResult().failure(
                RTError(
                    self.pos_start,
                    self.pos_end,
                    "suppressForeverWarning() may only be called inside global setup(){}",
                    exec_ctx,
                )
            )
        if args:
            return RTResult().failure(
                RTError(
                    self.pos_start,
                    self.pos_end,
                    "suppressForeverWarning() takes no arguments",
                    exec_ctx,
                )
            )
        _runtime._forever_warning_suppressed = True
        return RTResult().success(Number.null)

    def execute_overrideMain(self, args, exec_ctx):
        """overrideMain("funcName") — redirect the program."""
        if len(args) != 1 or not isinstance(args[0], String):
            return RTResult().failure(
                RTError(
                    self.pos_start,
                    self.pos_end,
                    "overrideMain() expects exactly one string argument — "
                    "the name of the global function to use as the program entry point.\n"
                    '  Example:  overrideMain("start");',
                    exec_ctx,
                )
            )
        _runtime._main_override = args[0].value
        return RTResult().success(Number.null)


BuiltinHandler = Callable[[BuiltInFunction, list[Any], Any], Any]

# Keep this list as the single source of truth for functions available to both
# programs and imported modules.  Adding a function here and registering its
# handler below is all that is needed to expose it everywhere.
BUILTIN_FUNCTION_NAMES = (
    "print",
    "println",
    "input",
    "inputln",
    "rawPy",
    "rawPyx",
    "strOf",
    "intOf",
    "floatOf",
    "returnType",
    "returnLength",
    "seqFromTo",
    "range",
    "cleanRawPyxCache",
    "listJsonArray",
    "listJsonObject",
    "splitStr",
    "listFlatten",
    "listUnique",
    "listPush",
    "listPop",
    "listGet",
    "listSet",
    "listSlice",
    "listContains",
    "contains",
    "listJoin",
    "listIndex",
    "listRemove",
    "anyOf",
    "allOf",
    "sumOf",
    "sortList",
    "reverseList",
    "listMin",
    "listMax",
    "asyncRun",
    "asyncGather",
    "sleep",
    "asyncSleep",
    "foreverDelay",
    "suppressForeverWarning",
    "tupleCreate",
    "tupleGet",
    "tupleLen",
    "tupleContains",
    "tupleIndex",
    "tupleSlice",
    "tupleToList",
    "listToTuple",
    "tupleConcat",
    "tupleCount",
    "tupleFirst",
    "tupleLast",
    "tupleJsonArray",
    "overrideMain",
)


BUILTIN_FUNCTIONS: dict[str, BuiltInFunction] = {}


def register_builtin(name: str, handler: BuiltinHandler | None = None) -> BuiltInFunction:
    """Register and return a built-in function.

    ``handler`` is an optional callable receiving ``(builtin, args,
    exec_ctx)`` and returning an ``RTResult``. The common in-tree case is
    adding a name whose ``execute_<name>`` method is defined above.
    """
    if not name.isidentifier():
        raise ValueError(f"Invalid builtin name: {name!r}")
    if handler is not None:
        setattr(BuiltInFunction, f"execute_{name}", handler)
    function = BuiltInFunction(name)
    setattr(BuiltInFunction, name, function)
    BUILTIN_FUNCTIONS[name] = function
    # The global table is created after this module is imported, so the
    # startup registrations are installed by lynxer.py.  Extensions added
    # later should become available immediately as well.
    global_symbol_table = getattr(_runtime, "global_symbol_table", None)
    if global_symbol_table is not None:
        global_symbol_table.set(name, function)
    return function


def register_builtins(symbol_table: Any) -> None:
    """Install every registered builtin into a Lynxer symbol table."""
    for name, function in BUILTIN_FUNCTIONS.items():
        symbol_table.set(name, function)


def builtin(name: str) -> Callable[[BuiltinHandler], BuiltinHandler]:
    """Decorator for adding a new builtin implementation in this module."""
    def decorator(handler: BuiltinHandler) -> BuiltinHandler:
        register_builtin(name, handler)
        return handler

    return decorator


# Create the public instances from the complete implementation above.
for _name in BUILTIN_FUNCTION_NAMES:
    register_builtin(_name)
