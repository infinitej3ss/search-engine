#!/usr/bin/env python3
"""
generate blacklist.txt from ut1 adult expression patterns.
source: https://github.com/olbat/ut1-blacklists/blob/master/blacklists/adult/

pattern 1 (expressions): compound words, match anywhere
pattern 2 (very_restrictive_expression line 1): more compound words
pattern 2 (very_restrictive_expression line 2): short stems with boundary matching

the second line of very_restrictive_expression looks like:
(^|[-...])(<prefixes>)?(<stems>)s?([-...]|$)

we want the stems group, not the prefixes.

usage:
  python3 gen_blacklist.py              # writes blacklist.txt in same dir
  python3 gen_blacklist.py -o out.txt   # writes to custom path
"""

import argparse
import os
import re
import urllib.request

P1_URL = "https://raw.githubusercontent.com/olbat/ut1-blacklists/master/blacklists/adult/expressions"
P2_URL = "https://raw.githubusercontent.com/olbat/ut1-blacklists/master/blacklists/adult/very_restrictive_expression"


def fetch(url):
    return urllib.request.urlopen(url).read().decode().strip()


def extract_alternations(pattern):
    terms = []
    for match in re.finditer(r'\(([^)]+)\)', pattern):
        group = match.group(1)
        for term in group.split('|'):
            cleaned = term.strip()
            if cleaned and not any(c in cleaned for c in r'^$[]\\{}') and len(cleaned) > 2:
                terms.append(cleaned.lower())
    return terms


def main():
    parser = argparse.ArgumentParser()
    default_out = os.path.join(os.path.dirname(__file__), "blacklist.txt")
    parser.add_argument("-o", "--output", default=default_out)
    args = parser.parse_args()

    p1_raw = fetch(P1_URL)
    p2_raw = fetch(P2_URL)

    # pattern 1: compound words
    p1_terms = extract_alternations(p1_raw) if p1_raw else []

    # pattern 2 line 1: more compound words
    p2_lines = p2_raw.split('\n')
    p2_compounds = extract_alternations(p2_lines[0]) if len(p2_lines) > 0 else []

    # pattern 2 line 2: find all parenthesized groups
    # the stems group is the longest one (most alternations)
    p2_stems = []
    if len(p2_lines) > 1:
        groups = re.findall(r'\(([^)]+)\)', p2_lines[1])
        if groups:
            stem_group = max(groups, key=lambda g: g.count('|'))
            for term in stem_group.split('|'):
                cleaned = term.strip().lower()
                cleaned = re.sub(r'\?', '', cleaned)  # x? -> x
                cleaned = re.sub(r'\+$', '', cleaned)
                if cleaned and not any(c in cleaned for c in r'^$[]\\{}') and len(cleaned) > 2:
                    p2_stems.append(cleaned)

    with open(args.output, "w") as f:
        f.write("# url blacklist loaded by static ranker at startup\n")
        f.write("# source: ut1 blacklists, https://github.com/olbat/ut1-blacklists\n")
        f.write("# regenerate: python3 gen_blacklist.py\n")
        f.write("#\n")
        f.write("# *term  matches anywhere in the url\n")
        f.write("#  term  matches only between boundary chars (- . / ? = + _ &)\n")
        f.write("\n")

        f.write("# compound words\n")
        seen = set()
        for term in sorted(set(p1_terms + p2_compounds)):
            if term not in seen:
                f.write(f"*{term}\n")
                seen.add(term)

        f.write("\n# keyword stems (boundary-delimited)\n")
        for term in sorted(set(p2_stems)):
            if term not in seen:
                f.write(f"{term}\n")
                seen.add(term)

    print(f"wrote {len(seen)} terms to {args.output}")


if __name__ == "__main__":
    main()
