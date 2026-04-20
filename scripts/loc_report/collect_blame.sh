#!/bin/bash
# builds blame.tsv with per-file, per-author line counts for every tracked source
set -euo pipefail

here="$(cd "$(dirname "$0")" && pwd)"
out="$here/blame.tsv"
sources="$here/sources.txt"

git ls-files '*.cpp' '*.hpp' '*.h' '*.html' '*.css' '*.js' \
  | grep -Ev '^(build/|crawler_test_files/|data/)' > "$sources"

: > "$out"
while read -r f; do "$here/blame_file.sh" "$f" >> "$out"; done < "$sources"

for f in CMakeLists.txt CMakePresets.json continue.sh init.sh rebuild_and_continue.sh \
         src/crawler/Makefile src/crawler/init.config; do
  [ -f "$f" ] && "$here/blame_file.sh" "$f" >> "$out"
done
