// reads query from url params, fetches results from backend, renders them

const params = new URLSearchParams(window.location.search);
const query = params.get("q") || "";
const offset = parseInt(params.get("offset") || "0", 10);
const limit = 10;

const input = document.getElementById("query");
if (input) input.value = query;

const resultsDiv = document.getElementById("results");
const sidebarDiv = document.getElementById("sidebar");

if (query) {
    resultsDiv.textContent = "searching...";
    fetch("/search?q=" + encodeURIComponent(query)
        + "&offset=" + offset + "&limit=" + limit)
        .then(resp => resp.json())
        .then(data => { render(data); renderSidebar(data); })
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

// sidebar debug widgets

function renderSidebar(data) {
    if (!sidebarDiv) return;
    sidebarDiv.innerHTML = "";
    const stats = data.stats;
    if (!stats) return;

    // widget 1: parsed query AST (falls back to flat token chips)
    if (stats.parsed_query_ast) {
        const w = makeWidget("parsed query");
        const pre = document.createElement("pre");
        pre.className = "ast-display";
        pre.textContent = stats.parsed_query_ast;
        w.body.appendChild(pre);

        if (stats.parsed_tokens && stats.parsed_tokens.length > 0) {
            const chips = document.createElement("div");
            chips.className = "chips";
            for (const t of stats.parsed_tokens) {
                const c = document.createElement("span");
                c.className = "chip";
                c.textContent = t;
                chips.appendChild(c);
            }
            w.body.appendChild(chips);
        }
        sidebarDiv.appendChild(w.root);
    } else if (stats.parsed_tokens && stats.parsed_tokens.length > 0) {
        const w = makeWidget("searched for");
        const chips = document.createElement("div");
        chips.className = "chips";
        for (const t of stats.parsed_tokens) {
            const c = document.createElement("span");
            c.className = "chip";
            c.textContent = t;
            chips.appendChild(c);
        }
        w.body.appendChild(chips);
        sidebarDiv.appendChild(w.root);
    }

    // widget 2: query funnel
    if (stats.constraint_solved !== undefined) {
        const w = makeWidget("pipeline");
        const rows = [
            ["constraint-solved", stats.constraint_solved],
            ["passed static floor", stats.passed_static_floor],
            ["returned this page", data.results.length],
        ];
        const max = Math.max(1, stats.constraint_solved);
        for (const [label, value] of rows) {
            w.body.appendChild(makeBarRow(label, value, max));
        }
        sidebarDiv.appendChild(w.root);
    }

    // widget 3: top-result signal breakdown
    if (data.results.length > 0) {
        const top = data.results[0];
        const w = makeWidget("top result signals  (doc " + top.doc_id + ")");
        const rows = [
            ["T1 metastream", top.t1],
            ["T2 span",       top.t2],
            ["T3 quality",    top.t3],
            ["BM25",          top.bm25],
        ];
        for (const [label, value] of rows) {
            w.body.appendChild(makeBarRow(label, value, 1.0, 3));
        }
        const sep = document.createElement("div");
        sep.className = "sep";
        w.body.appendChild(sep);
        w.body.appendChild(makeBarRow("static score",  top.static_score,  1.0, 3));
        w.body.appendChild(makeBarRow("dynamic score", top.dynamic_score, 1.0, 3));
        sidebarDiv.appendChild(w.root);
    }

    // widget 4: per-rank breakdown
    if (stats.per_rank_matched && stats.per_rank_matched.length > 0) {
        const w = makeWidget("results by rank bucket");
        const max = Math.max(1, ...stats.per_rank_matched);
        stats.per_rank_matched.forEach((n, r) => {
            w.body.appendChild(makeBarRow("rank " + r, n, max));
        });
        sidebarDiv.appendChild(w.root);
    }
}

function makeWidget(title) {
    const root = document.createElement("section");
    root.className = "widget";
    const h = document.createElement("h3");
    h.textContent = title;
    root.appendChild(h);
    const body = document.createElement("div");
    body.className = "widget-body";
    root.appendChild(body);
    return { root, body };
}

function makeBarRow(label, value, max, decimals = 0) {
    const row = document.createElement("div");
    row.className = "bar-row";

    const lab = document.createElement("span");
    lab.className = "bar-label";
    lab.textContent = label;
    row.appendChild(lab);

    const track = document.createElement("span");
    track.className = "bar-track";
    const fill = document.createElement("span");
    fill.className = "bar-fill";
    const pct = max > 0 ? Math.max(0, Math.min(100, (value / max) * 100)) : 0;
    fill.style.width = pct + "%";
    track.appendChild(fill);
    row.appendChild(track);

    const val = document.createElement("span");
    val.className = "bar-value";
    val.textContent = decimals > 0 ? value.toFixed(decimals) : value.toLocaleString();
    row.appendChild(val);

    return row;
}
