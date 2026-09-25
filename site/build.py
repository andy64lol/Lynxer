#!/usr/bin/env python3
"""Build the static documentation site from ``docs/*.md``.

Renders every page under ``docs/`` into a mirrored HTML tree under
``site/docs/`` so the website covers all of the documentation. Standard
library only; no third-party Markdown library is required.

    python3 site/build.py
"""

from __future__ import annotations

import html
import os
import re
from collections.abc import Callable
from pathlib import Path

Link = Callable[[str], str]

ROOT = Path(__file__).resolve().parents[1]
DOCS = ROOT / "docs"
SITE = ROOT / "site"
OUT = SITE / "docs"

# Page groups for the sidebar. Paths are relative to docs/.
GROUPS: list[tuple[str, list[str]]] = [
    ("Overview", ["README.md"]),
    ("Getting started", ["install.md", "CLI.md"]),
    ("Language", ["language.md", "operators.md", "types.md", "lists.md", "tuples.md", "modules.md", "importAs.md"]),
    ("Records", ["structs.md", "classes.md", "enums.md", "vargroups.md"]),
    ("Built-ins and modules",
     ["builtins.md", "native-module-abi.md", "stdlib-contracts.md", "extending.md"]),
    ("Reference", ["legacy-surface.md", "limitations.md"]),
]

STDLIB_MODULES = [
    "cli", "colorlib", "csv", "debug", "fileIO", "game", "image", "js", "json",
    "lua", "math", "multiprocessing", "network", "os", "path", "random", "re",
    "regex", "server", "shell", "sound", "sqldb", "sys", "text", "time", "tui",
    "typing",
]


def out_name(md_rel: str) -> str:
    """docs-relative markdown path -> site/docs-relative HTML path."""
    if md_rel == "README.md":
        return "index.html"
    return md_rel[:-3] + ".html"


# ---------------------------------------------------------------- inline ----

def slugify(text: str) -> str:
    text = re.sub(r"`([^`]*)`", r"\1", text)
    text = re.sub(r"\[([^\]]*)\]\([^)]*\)", r"\1", text)
    text = text.strip().lower()
    text = re.sub(r"[^a-z0-9 \-]", "", text)
    # GitHub replaces each space with a hyphen and keeps doubled hyphens, so
    # "switch / case / default" becomes "switch--case--default".
    return text.replace(" ", "-")


def build_inline(link: Link) -> Link:
    def render(text: str) -> str:
        spans: list[str] = []

        def stash(match: re.Match) -> str:
            spans.append(match.group(1))
            return f"\x00{len(spans) - 1}\x00"

        text = re.sub(r"`([^`]+)`", stash, text)
        text = html.escape(text, quote=False)

        def do_link(match: re.Match) -> str:
            label, target = match.group(1), match.group(2)
            # "install.md" reads badly on a web page; show "install".
            shown = label
            if re.fullmatch(r"[A-Za-z0-9_./-]+\.md", label):
                shown = label[:-3]
            return f'<a href="{html.escape(link(target), quote=True)}">{shown}</a>'

        text = re.sub(r"!\[([^\]]*)\]\(([^)\s]+)\)",
                      lambda m: f'<img src="{html.escape(m.group(2), quote=True)}" '
                                f'alt="{html.escape(m.group(1), quote=True)}">', text)
        text = re.sub(r"\[([^\]]+)\]\(([^)\s]+)\)", do_link, text)
        text = re.sub(r"\*\*([^*]+)\*\*", r"<strong>\1</strong>", text)
        text = re.sub(r"(?<!\*)\*([^*]+)\*(?!\*)", r"<em>\1</em>", text)
        text = re.sub(r"&lt;(https?://[^&]+?)&gt;",
                      r'<a href="\1">\1</a>', text)

        def unstash(match: re.Match) -> str:
            return "<code>" + html.escape(spans[int(match.group(1))]) + "</code>"

        return re.sub(r"\x00(\d+)\x00", unstash, text)

    return render


# ----------------------------------------------------------------- blocks ---

def split_row(row: str) -> list[str]:
    row = row.strip()
    row = row.removeprefix("|")
    row = row.removesuffix("|")
    return [cell.strip().replace("\\|", "|") for cell in re.split(r"(?<!\\)\|", row)]


def render_table(lines: list[str], start: int, inline) -> tuple[str, int]:
    header = split_row(lines[start])
    index = start + 2
    body_rows = []
    while index < len(lines) and lines[index].lstrip().startswith("|"):
        body_rows.append(split_row(lines[index]))
        index += 1
    out = ["<table>", "<tr>" + "".join(f"<th>{inline(c)}</th>" for c in header) + "</tr>"]
    for row in body_rows:
        cells = row + [""] * (len(header) - len(row))
        out.append("<tr>" + "".join(f"<td>{inline(c)}</td>" for c in cells[:len(header)]) + "</tr>")
    out.append("</table>")
    return "\n".join(out), index


def is_table_start(lines: list[str], index: int) -> bool:
    if index + 1 >= len(lines):
        return False
    if not lines[index].lstrip().startswith("|"):
        return False
    sep = lines[index + 1].strip()
    return bool(re.fullmatch(r"\|?[\s:|-]+\|?", sep)) and "-" in sep


LIST_ITEM = re.compile(r"^(\s*)([-*+]|\d+\.)\s+(.*)$")


def render_list(lines: list[str], start: int, inline) -> tuple[str, int]:
    ordered = bool(re.match(r"\s*\d+\.", lines[start]))
    tag = "ol" if ordered else "ul"
    items: list[str] = []
    index = start

    while index < len(lines):
        match = LIST_ITEM.match(lines[index])
        if not match:
            break
        item_ordered = bool(re.match(r"\s*\d+\.", lines[index]))
        if item_ordered != ordered:
            break
        parts = [match.group(3)]
        index += 1
        # Indented, non-item lines continue the current item.
        while index < len(lines):
            nxt = lines[index]
            if not nxt.strip():
                break
            if LIST_ITEM.match(nxt) or nxt.lstrip().startswith(("|", "#", "```", ">")):
                break
            if nxt.startswith((" ", "\t")):
                parts.append(nxt.strip())
                index += 1
            else:
                break
        items.append(inline(" ".join(parts)))

    out = [f"<{tag}>"] + [f"<li>{item}</li>" for item in items] + [f"</{tag}>"]
    return "\n".join(out), index


def render_markdown(text: str, link: Link) -> tuple[str, str]:
    inline = build_inline(link)
    lines = text.splitlines()
    out: list[str] = []
    title = ""
    index = 0

    while index < len(lines):
        line = lines[index]

        if line.startswith("```"):
            index += 1
            body = []
            while index < len(lines) and not lines[index].startswith("```"):
                body.append(lines[index])
                index += 1
            index += 1  # closing fence
            out.append("<pre><code>" + html.escape("\n".join(body)) + "</code></pre>")
            continue

        if not line.strip():
            index += 1
            continue

        heading = re.match(r"^(#{1,6})\s+(.*)$", line)
        if heading:
            level = len(heading.group(1))
            text_ = heading.group(2).strip()
            anchor = slugify(text_)
            if level == 1 and not title:
                title = re.sub(r"`([^`]*)`", r"\1", text_)
            out.append(f'<h{level} id="{anchor}">{inline(text_)}</h{level}>')
            index += 1
            continue

        if re.fullmatch(r"\s*(-{3,}|\*{3,}|_{3,})\s*", line):
            out.append("<hr>")
            index += 1
            continue

        if is_table_start(lines, index):
            table, index = render_table(lines, index, inline)
            out.append(table)
            continue

        if line.startswith(">"):
            quote = []
            while index < len(lines) and lines[index].startswith(">"):
                quote.append(lines[index].lstrip(">").strip())
                index += 1
            out.append("<blockquote>" + inline(" ".join(quote)) + "</blockquote>")
            continue

        if LIST_ITEM.match(line):
            block, index = render_list(lines, index, inline)
            out.append(block)
            continue

        paragraph = []
        while index < len(lines):
            nxt = lines[index]
            if not nxt.strip():
                break
            if (nxt.startswith(("```", ">", "|", "#"))
                    or LIST_ITEM.match(nxt)
                    or re.fullmatch(r"\s*(-{3,}|\*{3,}|_{3,})\s*", nxt)):
                break
            paragraph.append(nxt.strip())
            index += 1
        if not paragraph:
            # A line that opens a block the loop above did not handle (for
            # example a table row with no separator row). Emit it and advance so
            # the loop always makes progress instead of spinning forever.
            out.append("<p>" + inline(line.strip()) + "</p>")
            index += 1
        else:
            out.append("<p>" + inline(" ".join(paragraph)) + "</p>")

    return "\n".join(out), title


# ------------------------------------------------------------------ pages ---

PAGES: list[tuple[str, str]] = []
for _, names in GROUPS:
    PAGES.extend((name, out_name(name)) for name in names)
PAGES.extend((f"stdlib/{m}.md", f"stdlib/{m}.html") for m in STDLIB_MODULES)
OUT_BY_MD = {md: out for md, out in PAGES}


def make_link(current_out: str, md_rel: str) -> Link:
    """Return a function rewriting a docs-relative link target for the page."""
    current_dir = os.path.dirname(current_out)

    def link(target: str) -> str:
        if target.startswith(("http://", "https://", "mailto:", "#")):
            return target
        path, _, anchor = target.partition("#")
        md_dir = os.path.dirname(md_rel)
        resolved = os.path.normpath(os.path.join(md_dir, path)) if path else md_dir
        if not path:
            return "#" + anchor
        if resolved.endswith(".md"):
            html_target = OUT_BY_MD.get(resolved, out_name(resolved))
            rel = os.path.relpath(html_target, current_dir or ".")
            return rel + (("#" + anchor) if anchor else "")
        return target

    return link


def render_nav(current_out: str) -> str:
    current_dir = os.path.dirname(current_out) or "."

    def li(title: str, target_out: str) -> str:
        active = ' class="active"' if target_out == current_out else ""
        rel = os.path.relpath(target_out, current_dir)
        return f'<li><a href="{rel}"{active}>{html.escape(title)}</a></li>'

    parts = []
    for group, names in GROUPS:
        parts.append(f"<h3>{html.escape(group)}</h3><ul>")
        for name in names:
            title = re.sub(r"`([^`]*)`", r"\1", name.split("/")[-1][:-3])
            parts.append(li(title, OUT_BY_MD[name]))
        parts.append("</ul>")
    parts.append("<h3>Standard library</h3><ul>")
    parts.append(li("All modules", "stdlib/index.html"))
    parts.extend(li(module, f"stdlib/{module}.html") for module in STDLIB_MODULES)
    parts.append("</ul>")
    return "\n".join(parts)


TEMPLATE = """<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1">
    <title>{title} - Lynxer</title>
    <link rel="stylesheet" href="{rel}style.css">
</head>
<body>

<header>
    <h1><a href="{rel}index.html">Lynxer</a></h1>
    <p>Documentation</p>
</header>

<div class="layout">
    <nav class="sidebar">
{nav}
    </nav>
    <main class="doc">
{content}
    </main>
</div>

<footer>
    <a class="github" href="https://github.com/andy64lol/Lynxer" aria-label="Lynxer on GitHub" title="Lynxer on GitHub"><svg role="img" viewBox="0 0 24 24" width="20" height="20" aria-hidden="true" focusable="false"><path fill="currentColor" d="M12 .297c-6.63 0-12 5.373-12 12 0 5.303 3.438 9.8 8.205 11.385.6.113.82-.258.82-.577 0-.285-.01-1.04-.015-2.04-3.338.724-4.042-1.61-4.042-1.61C4.422 18.07 3.633 17.7 3.633 17.7c-1.087-.744.084-.729.084-.729 1.205.084 1.838 1.236 1.838 1.236 1.07 1.835 2.809 1.305 3.495.998.108-.776.417-1.305.76-1.605-2.665-.3-5.466-1.332-5.466-5.93 0-1.31.465-2.38 1.235-3.22-.135-.303-.54-1.523.105-3.176 0 0 1.005-.322 3.3 1.23.96-.267 1.98-.399 3-.405 1.02.006 2.04.138 3 .405 2.28-1.552 3.285-1.23 3.285-1.23.645 1.653.24 2.873.12 3.176.765.84 1.23 1.91 1.23 3.22 0 4.61-2.805 5.625-5.475 5.92.42.36.81 1.096.81 2.22 0 1.606-.015 2.896-.015 3.286 0 .315.21.69.825.57C20.565 22.092 24 17.592 24 12.297c0-6.627-5.373-12-12-12"/></svg></a>
    <span>Lynxer &middot; MIT License</span>
</footer>

</body>
</html>
"""


def write_page(out_rel: str, md_rel: str, content_md: str, fallback_title: str) -> None:
    link = make_link(out_rel, md_rel)
    content, title = render_markdown(content_md, link)
    rel = "../" * (out_rel.count("/") + 1)  # back to site/ from site/docs/<...>
    page = TEMPLATE.format(title=html.escape(title or fallback_title), rel=rel,
                           nav=render_nav(out_rel), content=content)
    destination = OUT / out_rel
    destination.parent.mkdir(parents=True, exist_ok=True)
    destination.write_text(page, encoding="utf-8")


def module_summary(module: str) -> str:
    path = DOCS / "stdlib" / f"{module}.md"
    if not path.is_file():
        return ""
    paragraph: list[str] = []
    for line in path.read_text(encoding="utf-8").splitlines():
        stripped = line.strip()
        if not stripped:
            if paragraph:
                break
            continue
        if stripped.startswith("#"):
            continue
        paragraph.append(stripped)
    text = re.sub(r"\s+", " ", " ".join(paragraph)).strip()
    if len(text) > 120:
        text = text[:117].rsplit(" ", 1)[0] + "…"
    return text


def main() -> int:
    count = 0
    for md_rel, out_rel in PAGES:
        source = DOCS / md_rel
        if not source.is_file():
            print(f"missing: docs/{md_rel}")
            continue
        write_page(out_rel, md_rel, source.read_text(encoding="utf-8"),
                   out_rel.split("/")[-1][:-5])
        count += 1

    # docs/stdlib has no README, so synthesise an index for the site.
    index_lines = [
        "# Standard library",
        "",
        ("Lynxer ships 27 modules. Each has a `.lynx` wrapper and a native "
         "backend behind the shared native-module ABI; pick one below or from "
         "the sidebar."),
        "",
    ]
    for module in STDLIB_MODULES:
        summary = module_summary(module)
        entry = f"- [`{module}`]({module}.md)"
        if summary:
            entry += f" — {summary}"
        index_lines.append(entry)
    write_page("stdlib/index.html", "stdlib/index.md", "\n".join(index_lines),
               "Standard library")
    count += 1

    print(f"docs: wrote {count} page(s) to {OUT.relative_to(ROOT)}/")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
