# Elements

The complete element API of Wynxer's `.wpf` page format. Every element is
written as a **`element.<kind>(...)` call** inside a page's `contents { }`
block — there is no hand-written HTML. Use `wynxer elements` to print the
list of all 56 kinds.

## Syntax

```wpf
element.<kind>("text", handler){style};
```

Every call has three optional parts:

| Part | Syntax | Meaning |
| --- | --- | --- |
| kind | `element.header1` | The element to create (see the tables below) |
| arguments | `("Hello", app.onClick)` | Positional arguments inside `(...)` |
| style binding | `{styles.blue-text}` | A CSS class to attach to the element |

The trailing `;` is required. Arguments and the style binding can be omitted
when a kind does not need them.

### Arguments

Arguments are comma-separated inside the parentheses. The WPF parser splits
them at top-level commas only, so quoted strings and nested
`(`/`{`/`[` groups are kept intact.

- **Argument 1 — the content/text.** A quoted string becomes the element's
  text content (or its `value`/`src` for inputs and images).
- **Argument 2 — an event handler.** An `app.<name>` or `local.<name>` reference
  is emitted as an `onclick="<name>()"` attribute calling a Wynxer function.

```wpf
element.header1("Hello World!"){styles.blue-text};  // heading with a class
element.button("Click me!", app.onClick);           // text + click handler
element.text("Plain text");                         // text only
element.newline();                                  // no arguments at all
```

Argument order matters: `element.button("Click me!", app.onClick)` puts the
label first and the handler second. Passing a handler as the only argument
would treat it as the button's text.

### Style bindings

A `{...}` block after the arguments binds a CSS class. Both
`{styles.blue-text}` (an imported stylesheet) and `{local.highlight}` work;
only the part after the final `.` becomes the HTML `class`:

```wpf
import styles styles;

page index {
  contents {
    element.text("Blue text"){styles.blue-text};
  }
}
```

### Void and self-closing elements

Elements that render as HTML void tags (`br`, `hr`, `input`, `img`) need no
content and are emitted self-closing. The convenient way to write them is with
no arguments:

```wpf
element.newline();   // <br />  — a line break
element.br();        // <br />  — same effect
element.hr();        // <hr />  — a horizontal rule
element.divider();   // <hr />  — divider line
element.spacer();    // empty spacer
```

So instead of writing `<br>` in HTML, you write `element.newline();`.

### Special kinds

A few kinds get built-in attributes even when you pass no arguments:

| Kind | Emitted attribute / behaviour |
| --- | --- |
| `checkbox` | `type="checkbox"` |
| `radio` | `type="radio"` |
| `submit` | `type="submit"` |
| `image` / `avatar` | `alt=""` (arg 1 is used as `src`) |
| `input` | arg 1 becomes `value="..."` |
| `button` | arg 2 (`app.x` / `local.x`) becomes `onclick="x()"` |

### Nested content and layout

Layout kinds (`row`, `column`, `grid`, `stack`, `container`, `card`, `panel`)
are ordinary elements; nesting and child bindings are tracked by the parser
and can be styled through the same `{...}` class binding.

## Headings

| Kind | Call | Meaning |
| --- | --- | --- |
| `header1` | `element.header1("Title");` | Top-level heading (`<h1>`) |
| `header2` | `element.header2("Title");` | Second-level heading (`<h2>`) |
| `header3` | `element.header3("Title");` | Third-level heading (`<h3>`) |
| `header4` | `element.header4("Title");` | Fourth-level heading (`<h4>`) |
| `header5` | `element.header5("Title");` | Fifth-level heading (`<h5>`) |
| `header6` | `element.header6("Title");` | Sixth-level heading (`<h6>`) |
| `header` | `element.header("Title");` | A generic header element |

## Text

| Kind | Call | Meaning |
| --- | --- | --- |
| `text` | `element.text("Hello");` | Plain text |
| `paragraph` | `element.paragraph("Hello");` | A paragraph |
| `span` | `element.span("Hello");` | An inline span |
| `label` | `element.label("Name");` | A label |
| `caption` | `element.caption("Caption");` | A caption |
| `quote` | `element.quote("Quote");` | A blockquote |
| `code` | `element.code("x = 5;");` | Inline code |

## Interactive

| Kind | Call | Meaning |
| --- | --- | --- |
| `button` | `element.button("Click", app.onClick);` | A clickable button |
| `link` | `element.link("Home");` | A hyperlink |
| `input` | `element.input("default");` | A text input |
| `textarea` | `element.textarea("text");` | A multi-line text input |
| `checkbox` | `element.checkbox();` | A checkbox |
| `radio` | `element.radio();` | A radio button |
| `select` | `element.select();` | A dropdown |
| `option` | `element.option("One");` | An option in a select |
| `submit` | `element.submit("Send");` | A submit button |

## Layout

| Kind | Call | Meaning |
| --- | --- | --- |
| `div` | `element.div();` | A generic container |
| `section` | `element.section();` | A section |
| `article` | `element.article();` | An article |
| `header` | `element.header();` | A page header |
| `footer` | `element.footer();` | A page footer |
| `nav` | `element.nav();` | A navigation bar |
| `aside` | `element.aside();` | A sidebar |
| `main` | `element.main();` | The main content area |
| `column` | `element.column();` | A vertical column |
| `row` | `element.row();` | A horizontal row |
| `grid` | `element.grid();` | A grid layout |
| `stack` | `element.stack();` | A stacked layout |
| `container` | `element.container();` | A generic container |
| `card` | `element.card();` | A card |
| `panel` | `element.panel();` | A panel |

## Media

| Kind | Call | Meaning |
| --- | --- | --- |
| `image` | `element.image("photo.png");` | An image |
| `video` | `element.video();` | A video |
| `audio` | `element.audio();` | An audio clip |
| `icon` | `element.icon();` | An icon |
| `avatar` | `element.avatar("me.png");` | An avatar image |

## Lists and tables

| Kind | Call | Meaning |
| --- | --- | --- |
| `list` | `element.list();` | An unordered list |
| `orderedList` | `element.orderedList();` | An ordered list |
| `listItem` | `element.listItem("Item");` | A list item |
| `table` | `element.table();` | A table |
| `thead` | `element.thead();` | A table head |
| `tbody` | `element.tbody();` | A table body |
| `tr` | `element.tr();` | A table row |
| `th` | `element.th("Head");` | A table header cell |
| `td` | `element.td("Cell");` | A table data cell |

## Misc

| Kind | Call | Meaning |
| --- | --- | --- |
| `newline` | `element.newline();` | A line break (`<br />`) |
| `spacer` | `element.spacer();` | Empty space |
| `divider` | `element.divider();` | A divider line (`<hr />`) |
| `hr` | `element.hr();` | A horizontal rule (`<hr />`) |
| `br` | `element.br();` | A line break (`<br />`) |

## Full example

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
    ignore(.blue-text behind the elements from the css);
  }

  scripts {
  }
}
```

## See also

- [wpf.md](wpf.md) — the page format
- [language.md](language.md) — the Wynxer language
- [CLI.md](CLI.md) — the command line
