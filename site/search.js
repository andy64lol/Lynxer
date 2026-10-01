"use strict";

document.querySelectorAll(".search-bar").forEach((searchBar) => {
    const input = searchBar.querySelector("input");
    const results = searchBar.querySelector(".search-results");
    const indexUrl = searchBar.dataset.searchIndex;
    const siteRoot = searchBar.dataset.siteRoot;
    let documents;
    let searchUnavailable = false;

    function showMessage(message) {
        results.replaceChildren();
        const status = document.createElement("p");
        status.className = "search-status";
        status.textContent = message;
        results.append(status);
    }

    function renderResults() {
        const terms = input.value.trim().toLocaleLowerCase().split(/\s+/).filter(Boolean);
        results.replaceChildren();
        if (!terms.length) return;

        if (searchUnavailable) {
            showMessage("Search is currently unavailable.");
            return;
        }

        if (!documents) {
            showMessage("Loading search index…");
            return;
        }

        const matches = documents.filter((document) => {
            const searchable = `${document.title} ${document.text}`.toLocaleLowerCase();
            return terms.every((term) => searchable.includes(term));
        }).slice(0, 10);

        if (!matches.length) {
            showMessage("No matching documentation found.");
            return;
        }

        const list = document.createElement("ul");
        list.className = "search-result-list";
        for (const match of matches) {
            const item = document.createElement("li");
            const link = document.createElement("a");
            link.href = `${siteRoot}${match.path}`;
            link.textContent = match.title;
            item.append(link);

            const excerpt = document.createElement("p");
            excerpt.textContent = match.text.slice(0, 180);
            item.append(excerpt);
            list.append(item);
        }
        results.append(list);
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

    input.addEventListener("input", renderResults);
    input.addEventListener("keydown", (event) => {
        if (event.key === "Escape") {
            input.value = "";
            renderResults();
        } else if (event.key === "Enter") {
            results.querySelector("a")?.click();
        }
    });
});
