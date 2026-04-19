// reads query from url params, fetches results from backend, renders them

const params = new URLSearchParams(window.location.search);
const query = params.get("q") || "";
const offset = parseInt(params.get("offset") || "0", 10);
const limit = 10;

const input = document.getElementById("query");
if (input) input.value = query;

const resultsDiv = document.getElementById("results");

if (query) {
    resultsDiv.textContent = "searching...";
    fetch("/search?q=" + encodeURIComponent(query)
        + "&offset=" + offset + "&limit=" + limit)
        .then(resp => resp.json())
        .then(data => render(data))
        .catch(err => { resultsDiv.textContent = "error: " + err.message; });
}

// TODO turn these into cards with snippets and doc info
function render(data) {
    resultsDiv.innerHTML = "";

    const summary = document.createElement("p");
    const start = data.offset + 1;
    const end = data.offset + data.results.length;
    let text = "showing " + start + "-" + end
        + " of " + data.total + " result(s) for \"" + data.query + "\"";
    if (typeof data.took_ms === "number") {
        text += "  (" + data.took_ms.toFixed(0) + " ms)";
    }
    summary.textContent = text;
    resultsDiv.appendChild(summary);

    if (data.results.length === 0) return;

    for (const r of data.results) {
        const item = document.createElement("div");

        const title = document.createElement("div");
        title.style.fontWeight = "bold";
        title.textContent = r.title || r.url;
        item.appendChild(title);

        const link = document.createElement("a");
        link.href = r.url;
        link.textContent = r.url;
        item.appendChild(link);

        if (r.snippet) {
            const snip = document.createElement("p");
            snip.textContent = r.snippet;
            item.appendChild(snip);
        }

        const scores = document.createElement("pre");
        scores.textContent =
            "doc_id: " + r.doc_id +
            "  static: " + r.static_score.toFixed(4) +
            "  dynamic: " + r.dynamic_score.toFixed(4) +
            "  combined: " + r.combined_score.toFixed(4);
        item.appendChild(scores);

        resultsDiv.appendChild(item);
    }

    // pagination
    const nav = document.createElement("div");
    nav.style.marginTop = "1em";

    if (data.offset > 0) {
        const prev = document.createElement("a");
        prev.href = "results.html?q=" + encodeURIComponent(data.query)
            + "&offset=" + Math.max(0, data.offset - limit);
        prev.textContent = "< prev";
        nav.appendChild(prev);
        nav.appendChild(document.createTextNode("  "));
    }

    if (data.offset + data.results.length < data.total) {
        const next = document.createElement("a");
        next.href = "results.html?q=" + encodeURIComponent(data.query)
            + "&offset=" + (data.offset + limit);
        next.textContent = "next >";
        nav.appendChild(next);
    }

    resultsDiv.appendChild(nav);
}
