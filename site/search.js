"use strict";

function normalize(value) {
    return value
        .normalize("NFD")
        .replace(/[\u0300-\u036f]/g, "")
        .replace(/([a-z])([A-Z])/g, "$1 $2")
        .toLocaleLowerCase()
        .replace(/[^a-z0-9]+/g, " ")
        .trim();
}

const STOP_WORDS = new Set([
    "a", "an", "and", "are", "as", "at", "be", "by", "can", "could", "did",
    "do", "does", "for", "from", "how", "in", "is", "it", "of", "on", "or",
    "please", "should", "the", "to", "what", "when", "where", "which", "with",
]);

function editDistanceAtMostOne(left, right) {
    if (Math.abs(left.length - right.length) > 1) return false;
    let i = 0;
    let j = 0;
    let edits = 0;
    while (i < left.length && j < right.length) {
        if (left[i] === right[j]) {
            i++;
            j++;
            continue;
        }
        if (++edits > 1) return false;
        if (left.length > right.length) i++;
        else if (right.length > left.length) j++;
        else {
            i++;
            j++;
        }
    }
    return edits + (i < left.length || j < right.length ? 1 : 0) <= 1;
}

function scoreTerm(term, fields, words) {
    let score = 0;
    for (const [field, weight] of fields) {
        if (field === term) score = Math.max(score, weight + 20);
        else if (field.includes(term)) score = Math.max(score, weight + 10);
        else if (words.some((word) => word.startsWith(term))) {
            score = Math.max(score, weight + 6);
        }
    }
    if (!score && term.length >= 5
        && words.some((word) => editDistanceAtMostOne(term, word))) {
        score = 2;
    }
    return score;
}

function rankDocument(document, terms) {
    const title = normalize(document.title);
    const path = normalize(document.path);
    const headings = (document.headings || []).map((heading) => ({
        ...heading,
        normalized: normalize(heading.title),
    }));
    const text = normalize(document.text);
    const words = `${title} ${path} ${headings.map((h) => h.normalized).join(" ")} ${text}`
        .split(/\s+/).filter(Boolean);
    const fields = [
        [title, 100],
        [path, 60],
        ...headings.map((heading) => [heading.normalized, 45]),
        [text, 0],
    ];
    let score = 0;
    let bestHeading = null;
    for (const term of terms) {
        const termScore = scoreTerm(term, fields, words);
        if (!termScore) return null;
        score += termScore;
        for (const heading of headings) {
            if (scoreTerm(term, [[heading.normalized, 45]], heading.normalized.split(" "))
                && (!bestHeading || heading.normalized.length < bestHeading.normalized.length)) {
                bestHeading = heading;
            }
        }
    }
    if (terms.length > 1 && terms.join(" ") === title) score += 100;
    return { document, score, heading: bestHeading };
}

function excerptFor(text, terms, heading) {
    const sentence = heading?.title;
    if (sentence) return sentence;
    const lower = text.toLocaleLowerCase();
    const offsets = terms.map((term) => {
        const directOffset = lower.indexOf(term);
        if (directOffset >= 0) return directOffset;
        for (const word of text.matchAll(/[A-Za-z0-9]+/g)) {
            if (normalize(word[0]).split(/\s+/)
                .some((part) => term.length >= 5 && editDistanceAtMostOne(term, part))) {
                return word.index;
            }
        }
        return -1;
    }).filter((offset) => offset >= 0);
    if (!offsets.length) return text.slice(0, 180);
    const first = Math.min(...offsets);
    const start = Math.max(0, first - 70);
    const end = Math.min(text.length, start + 180);
    return `${start ? "…" : ""}${text.slice(start, end)}${end < text.length ? "…" : ""}`;
}

function appendHighlightedText(element, text, terms) {
    const expression = new RegExp(
        `(${terms.map((term) => term.replace(/[.*+?^${}()|[\]\\]/g, "\\$&"))
            .sort((a, b) => b.length - a.length).join("|")})`,
        "ig",
    );
    let cursor = 0;
    for (const match of text.matchAll(expression)) {
        if (match.index > cursor) {
            element.append(document.createTextNode(text.slice(cursor, match.index)));
        }
        const highlight = document.createElement("mark");
        highlight.textContent = match[0];
        element.append(highlight);
        cursor = match.index + match[0].length;
    }
    if (cursor < text.length) element.append(document.createTextNode(text.slice(cursor)));
}

document.querySelectorAll(".search-bar").forEach((searchBar) => {
    const input = searchBar.querySelector("input");
    const results = searchBar.querySelector(".search-results");
    const indexUrl = searchBar.dataset.searchIndex;
    const siteRoot = searchBar.dataset.siteRoot;
    let documents;
    let searchUnavailable = false;
    let activeIndex = -1;
    let debounce;

    function showMessage(message) {
        results.replaceChildren();
        const status = document.createElement("p");
        status.className = "search-status";
        status.textContent = message;
        results.append(status);
        input.setAttribute("aria-expanded", "true");
        input.removeAttribute("aria-activedescendant");
        activeIndex = -1;
    }

    function renderResults() {
        const query = normalize(input.value);
        const terms = query.split(/\s+/).filter((term) => term && !STOP_WORDS.has(term));
        results.replaceChildren();
        input.removeAttribute("aria-activedescendant");
        activeIndex = -1;
        if (!terms.length) {
            input.setAttribute("aria-expanded", "false");
            return;
        }

        if (searchUnavailable) {
            showMessage("Search is currently unavailable.");
            return;
        }
        if (!documents) {
            showMessage("Loading search index…");
            return;
        }

        const ranked = documents
            .map((document) => rankDocument(document, terms))
            .filter(Boolean)
            .sort((a, b) => b.score - a.score || a.document.title.localeCompare(b.document.title));
        const matches = ranked.slice(0, 8);

        if (!matches.length) {
            showMessage("No results. Try fewer or different keywords.");
            return;
        }

        const summary = document.createElement("p");
        summary.className = "search-status";
        summary.textContent = ranked.length > matches.length
            ? `Top ${matches.length} of ${ranked.length} results · use ↑ ↓ to navigate`
            : `${matches.length} result${matches.length === 1 ? "" : "s"} · use ↑ ↓ to navigate`;
        results.append(summary);

        const list = document.createElement("ul");
        list.id = "docs-search-results";
        list.className = "search-result-list";
        list.setAttribute("role", "listbox");
        matches.forEach(({ document: match, heading }, index) => {
            const item = document.createElement("li");
            item.id = `docs-search-option-${index}`;
            item.setAttribute("role", "option");
            item.setAttribute("aria-selected", "false");
            const link = document.createElement("a");
            link.href = `${siteRoot}${match.path}${heading ? `#${heading.id}` : ""}`;
            appendHighlightedText(link, heading?.title || match.title, terms);
            item.append(link);

            const excerpt = document.createElement("p");
            appendHighlightedText(excerpt, excerptFor(match.text, terms, heading), terms);
            item.append(excerpt);
            list.append(item);
        });
        results.append(list);
        input.setAttribute("aria-expanded", "true");
        input.setAttribute("aria-controls", list.id);
    }

    function setActive(index) {
        const options = [...results.querySelectorAll('[role="option"]')];
        if (!options.length) return;
        activeIndex = (index + options.length) % options.length;
        options.forEach((option, optionIndex) => {
            const active = optionIndex === activeIndex;
            option.setAttribute("aria-selected", String(active));
            option.classList.toggle("active", active);
        });
        input.setAttribute("aria-activedescendant", options[activeIndex].id);
        options[activeIndex].scrollIntoView({ block: "nearest" });
    }

    fetch(indexUrl)
        .then((response) => {
            if (!response.ok) throw new Error(`HTTP ${response.status}`);
            return response.json();
        })
        .then((index) => {
            if (!Array.isArray(index)) throw new Error("Invalid search index");
            documents = index;
            renderResults();
        })
        .catch((error) => {
            console.error("Unable to load the documentation search index:", error);
            searchUnavailable = true;
            renderResults();
        });

    input.setAttribute("role", "combobox");
    input.setAttribute("aria-autocomplete", "list");
    input.setAttribute("aria-expanded", "false");
    input.addEventListener("input", () => {
        clearTimeout(debounce);
        debounce = setTimeout(renderResults, 80);
    });
    input.addEventListener("keydown", (event) => {
        if (event.key === "Escape") {
            input.value = "";
            renderResults();
        } else if (event.key === "ArrowDown") {
            event.preventDefault();
            setActive(activeIndex + 1);
        } else if (event.key === "ArrowUp") {
            event.preventDefault();
            setActive(activeIndex < 0 ? 0 : activeIndex - 1);
        } else if (event.key === "Enter") {
            const active = results.querySelector('[aria-selected="true"] a')
                || results.querySelector('[role="option"] a');
            if (active) {
                event.preventDefault();
                active.click();
            }
        }
    });
});
