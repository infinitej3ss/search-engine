// reads query from url params, fetches results from backend, renders them

const params = new URLSearchParams(window.location.search);
const query = params.get("q") || "";

// populate the search box with the current query
const input = document.getElementById("query");
if (input) input.value = query;

const resultsDiv = document.getElementById("results");

if (query) {
    resultsDiv.textContent = "searching...";
    fetch("/search?q=" + encodeURIComponent(query))
        .then(resp => resp.json())
        .then(data => render(data))
        .catch(err => { resultsDiv.textContent = "error: " + err.message; });
}

// TODO turn these into cards with snippets and doc info
function render(data) {
    resultsDiv.innerHTML = "";

    const summary = document.createElement("p");
    summary.textContent = data.total + " result(s) for \"" + data.query + "\"";
    resultsDiv.appendChild(summary);

    if (data.results.length === 0) return;

    for (const r of data.results) {
        const item = document.createElement("div");

        const link = document.createElement("a");
        link.href = r.url;
        link.textContent = r.url;
        item.appendChild(link);

        const scores = document.createElement("pre");
        scores.textContent =
            "doc_id: " + r.doc_id +
            "  static: " + r.static_score.toFixed(4) +
            "  dynamic: " + r.dynamic_score.toFixed(4) +
            "  combined: " + r.combined_score.toFixed(4);
        item.appendChild(scores);

        resultsDiv.appendChild(item);
    }
}
