from typing import List
from utils import normalize_url

# https://en.wikipedia.org/wiki/Evaluation_measures_(information_retrieval)

def average_precision(our_results: List[str], reference_results: List[str], k: int = 10) -> float:
    our_norm = [ normalize_url(url) for url in our_results[:k] ]
    ref_set = set(normalize_url(url) for url in reference_results)

    num_relevant_found = 0
    precision_sum = 0.0
    
    for i, url in enumerate(our_norm):
        if url in ref_set:
            num_relevant_found += 1
            precision_at_i = num_relevant_found / (i + 1)
            precision_sum += precision_at_i

    return precision_sum / k


def score(our_results: List[str], reference_results: List[str]) -> float:
    return average_precision(our_results, reference_results)

