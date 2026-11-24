# Elements

The complete element API of Wynxer's `.wpf` page format. Every element is
written as `element.<kind>("text", handler){style};`. The text and handler
are optional depending on the kind; the style binding is optional and
references an imported stylesheet (`styles.blue-text`) or a local class.

Use `wynxer elements` to print this list (56 kinds).

## Headings

| Kind | Meaning |
| --- | --- |
| `header1` | Top-level heading (`<h1>`) |
| `header2` | Second-level heading (`<h2>`) |
| `header3` | Third-level heading (`<h3>`) |
| `header4` | Fourth-level heading (`<h4>`) |
| `header5` | Fifth-level heading (`<h5>`) |
| `header6` | Sixth-level heading (`<h6>`) |
| `header` | A generic header element |

## Text

| Kind | Meaning |
| --- | --- |
| `text` | Plain text |
| `paragraph` | A paragraph |
| `span` | An inline span |
| `label` | A label |
| `caption` | A caption |
| `quote` | A blockquote |
| `code` | Inline code |

## Interactive

| Kind | Meaning |
| --- | --- |
| `button` | A clickable button |
| `link` | A hyperlink |
| `input` | A text input |
| `textarea` | A multi-line text input |
| `checkbox` | A checkbox |
| `radio` | A radio button |
| `select` | A dropdown |
| `option` | An option in a select |
| `submit` | A submit button |

## Layout

| Kind | Meaning |
| --- | --- |
| `div` | A generic container |
| `section` | A section |
| `article` | An article |
| `header` | A page header |
| `footer` | A page footer |
| `nav` | A navigation bar |
| `aside` | A sidebar |
| `main` | The main content area |
| `column` | A vertical column |
| `row` | A horizontal row |
| `grid` | A grid layout |
| `stack` | A stacked layout |
| `container` | A generic container |
| `card` | A card |
| `panel` | A panel |

## Media

| Kind | Meaning |
| --- | --- |
| `image` | An image |
| `video` | A video |
| `audio` | An audio clip |
| `icon` | An icon |
| `avatar` | An avatar image |

## Lists and tables

| Kind | Meaning |
| --- | --- |
| `list` | An unordered list |
| `orderedList` | An ordered list |
| `listItem` | A list item |
| `table` | A table |
| `thead` | A table head |
| `tbody` | A table body |
| `tr` | A table row |
| `th` | A table header cell |
| `td` | A table data cell |

## Misc

| Kind | Meaning |
| --- | --- |
| `newline` | A line break |
| `spacer` | Empty space |
| `divider` | A divider line |
| `hr` | A horizontal rule |
| `br` | A line break |

## See also

- [wpf.md](wpf.md) — the page format
- [language.md](language.md) — the Wynxer language
- [CLI.md](CLI.md) — the command line
