#!/bin/bash
set -euo pipefail


DIR="./out"

if [ ! -d "$DIR" ]; then
    echo "No $DIR directory found. Generate some .jgr files first, e.g.:" >&2
    echo "  ./Plot_EV basic hard lines base > $DIR/basic_hard_lines_base.jgr" >&2
    exit 1
fi

shopt -s nullglob
jgr_files=("$DIR"/*.jgr)
shopt -u nullglob

if [ "${#jgr_files[@]}" -eq 0 ]; then
    echo "No .jgr files in $DIR." >&2
    exit 1
fi

count=0
skipped=0
for jgr in "${jgr_files[@]}"; do
    base="${jgr%.jgr}"
    ps="${base}.ps"
    pdf="${base}.pdf"

    # Skip if pdf is up to date
    if [ -f "$pdf" ] && [ "$pdf" -nt "$jgr" ]; then
        skipped=$((skipped + 1))
        continue
    fi

    ./jgraph -L -P "$jgr" > "$ps"
    ps2pdf "$ps" "$pdf"
    count=$((count + 1))
done
