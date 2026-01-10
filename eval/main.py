# gives us a score for automated heuristic eval
# using ddgs cuz official apis are pricy

from ddgs import DDGS

from interface import get_results
from scoring import score

from statistics import mean

ddgs_cli = DDGS()

queries = [
    "elephant",
    "world health organization",
    "mary poppins"
]

def eval_query(query: str):
    # bing seemed to be the most reliable in terms of skipping unimportant stuff (e.g. images)
    # should probably only use one source to maintain a good relevancy ranking
    ddgs_results = ddgs_cli.text(query, max_results = 10, backend = "bing")

    reference_results = [ ddgs_res['href'] for ddgs_res in ddgs_results ]
    our_results = get_results(query, top_k = 10)

    comparison_score = score(our_results, reference_results)

    return comparison_score

def main():
    scores = [ eval_query(query) for query in queries ]

    print(mean(scores))

if __name__ == "__main__":
    main()
