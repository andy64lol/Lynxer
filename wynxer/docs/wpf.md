# Wynxer Page Format (`.wpf`)

`.wpf` is a very simplified DSL that describes a page's structure. Wynxer
turns it into HTML and CSS, so the author never has to write HTML
directly.

## File layout

A `.wpf` file starts with imports, then one or more `page` blocks.

```wpf
import script app;
import styles styles;

page index {
    page.title = "Main";
    contents {
        element.header1("Hello World!"){styles.blue-text};
        ignore(".blue-text behind the elements from the css");
        element.button("Click me!", app.onClick);
        element.newline();
        element.text("This is written in Wynxer!"){styles.blue-text};
        element.button("Greet me!", local.main);
    };
    scripts {
        fn main() {
            alert("Hello!");
        }
        export main;
    }
}
```

## Imports

| Form | Meaning |
| --- | --- |
| `import script <name>;` | import a Wynxer script module |
| `import styles <name>;` | import a CSS stylesheet |

## Page

- `page <name> { ... }` — declares a page. The entry page is `index`.
- `page.title = "...";` — sets the page title.
- `contents { ... }` — the body of the page: elements, `ignore(...)`, assignments.
- `scripts { ... }` — local Wynxer scripts; `fn` declares a function, `export name;` exports it so elements can bind to it (`local.main`).

## Elements

An element is written `element.<kind>("text", handler){style};`. The text and
handler are optional depending on the kind; the style binding is optional and
references an imported stylesheet (`styles.blue-text`) or a local class.

See [elements.md](elements.md) for the complete list of kinds.

## Styling

Styles are plain CSS files imported with `import styles <name>;` and referenced
as `<name>.<class>` in the `{...}` binding of an element.

## See also

- [elements.md](elements.md) — the element API
- [language.md](language.md) — the Wynxer language
- [CLI.md](CLI.md) — the command line
