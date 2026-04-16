// frontend for search engine
// submits queries to /search?q=..., renders raw result data

const form = document.getElementById("search-form");
const input = document.getElementById("query");
const resultsDiv = document.getElementById("results");

form.addEventListener("submit", async (e) => {
    e.preventDefault();

    const query = input.value.trim();
    if (!query) return;

    resultsDiv.textContent = "searching...";

    try {
        const resp = await fetch("/search?q=" + encodeURIComponent(query));
        const data = await resp.json();
        render(data);
    } catch (err) {
        resultsDiv.textContent = "error: " + err.message;
    }
});

function render(data) {
    // clear previous results
    resultsDiv.innerHTML = "";

    const summary = document.createElement("p");
    summary.textContent = data.total + " result(s) for \"" + data.query + "\"";
    resultsDiv.appendChild(summary);

    if (data.results.length === 0) return;

    // render each result as a block showing all the data we have
    for (const r of data.results) {
        const item = document.createElement("div");
        item.style.marginBottom = "1em";

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
