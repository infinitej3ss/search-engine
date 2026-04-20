#!/bin/bash
# emits tab-separated: <file>\t<author>\t<lines>
f="$1"
git blame --line-porcelain "$f" 2>/dev/null | \
  awk -v file="$f" '
    /^author / { sub(/^author /,""); a=$0 }
    /^\t/ { count[a]++ }
    END { for (k in count) printf "%s\t%s\t%d\n", file, k, count[k] }'
