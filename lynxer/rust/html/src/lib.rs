//! HTML parsing and CSS querying for the Lynxer standard library.
use lynxer_abi::{export_string, lynxer_module};
use scraper::{Html, Selector};
use serde_json::{json, Map, Value};

const MAX_INPUT: usize = 16 << 20;

fn parse(input: &str) -> Option<Html> {
    (input.len() <= MAX_INPUT).then(|| Html::parse_document(input))
}

fn selected(input: &str, selector: &str) -> Option<Vec<Value>> {
    let document = parse(input)?;
    let selector = Selector::parse(selector).ok()?;
    Some(document.select(&selector).map(|element| {
        let mut attrs = Map::new();
        for (key, value) in element.value().attrs() {
            attrs.insert(key.to_string(), Value::String(value.to_string()));
        }
        json!({"tag": element.value().name(), "attributes": attrs,
               "text": element.text().collect::<String>(),
               "html": element.inner_html()})
    }).collect())
}

fn tree(input: &str) -> Option<String> {
    let document = parse(input)?;
    fn node(element: scraper::ElementRef<'_>) -> Value {
        let mut attrs = Map::new();
        for (key, value) in element.value().attrs() {
            attrs.insert(key.to_string(), Value::String(value.to_string()));
        }
        let children = element.children().filter_map(scraper::ElementRef::wrap).map(node).collect::<Vec<_>>();
        json!({"tag": element.value().name(), "attributes": attrs,
               "text": element.text().collect::<String>(), "children": children})
    }
    let root = document.root_element();
    serde_json::to_string(&node(root)).ok()
}

export_string!(html_parse, args, { tree(args.string(0)).unwrap_or_default() });
export_string!(html_select, args, {
    selected(args.string(0), args.string(1)).and_then(|v| serde_json::to_string(&v).ok()).unwrap_or_default()
});
export_string!(html_text, args, {
    selected(args.string(0), args.string(1)).and_then(|items| items.first().map(|v| v["text"].as_str().unwrap_or("").to_string())).unwrap_or_default()
});
export_string!(html_attr, args, {
    selected(args.string(0), args.string(1)).and_then(|items| items.first().and_then(|v| v["attributes"].get(args.string(2))).and_then(Value::as_str).map(str::to_owned)).unwrap_or_default()
});

const OPS: &[(&str, &str, &str)] = &[
    ("htmlParse", "html_parse", "cdecl:cstring(...)"),
    ("htmlSelect", "html_select", "cdecl:cstring(...)"),
    ("htmlText", "html_text", "cdecl:cstring(...)"),
    ("htmlAttr", "html_attr", "cdecl:cstring(...)"),
];
lynxer_module!(OPS);
