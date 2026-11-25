# Wynxer Page Format (.wpf)

Pages are written in `.wpf`, a small DSL that replaces hand-written HTML. A
page declares imports, a title, contents and optional inline scripts:

```wpf
import script app;
import styles styles;

page index {
  page.title = "My page";

  contents {
    element.header1("Welcome"){styles.blue-text};
    element.text("This is written in Wynxer!");
    element.newline();
    element.button("Click me!", app.onClick);
  }

  scripts {
  }
}
```

## Element syntax

Every element is a call:

```wpf
element.<kind>("text", handler){style};
```

- **Kind** — one of the 56 registered kinds.
- **Argument 1** — text content (or `value` / `src` for inputs and images).
- **Argument 2** — an event handler (`app.onClick` or `local.main`) emitted as
  `onclick="onClick()"`.
- **Style binding** — `{styles.blue-text}` or `{local.card}`; the part after
  the final `.` becomes the HTML `class`.

`wynxer elements` lists all 56 kinds. Groups:

| Group | Kinds |
| --- | --- |
| Headings | `header1`–`header6`, `header` |
| Text | `text`, `paragraph`, `span`, `label`, `caption`, `quote`, `code` |
| Interactive | `button`, `link`, `input`, `textarea`, `checkbox`, `radio`, `select`, `option`, `submit` |
| Layout | `div`, `section`, `article`, `header`, `footer`, `nav`, `aside`, `main`, `column`, `row`, `grid`, `stack`, `container`, `card`, `panel` |
| Media | `image`, `video`, `audio`, `icon`, `avatar` |
| Lists/tables | `list`, `orderedList`, `listItem`, `table`, `thead`, `tbody`, `tr`, `th`, `td` |
| Misc | `newline`, `spacer`, `divider`, `hr`, `br` |

## Void elements

Write `element.newline();` instead of `<br>` in HTML. The void kinds
(`br`, `hr`, `input`, `img`, plus `newline`, `divider`, `spacer`) need no
content and are emitted self-closing.

## Special kinds

`checkbox` / `radio` / `submit` get a `type` attribute; `image` / `avatar` get
`alt=""`; `input` uses argument 1 as `value`.

## Build output

`wynxer build <dir>` renders the first `.wpf` page to `build/index.html`,
transpiles `.wynx` files and inline scripts to `build/app.js`, and
concatenates `.css` files into `build/styles.css`.
