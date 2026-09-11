#!/usr/bin/env bash
# Run the OF2W test battery sequentially. Continues past failures and
# prints a summary. Usage: ./run_all.sh [01 02 ...]  (default: all)
cd "$(dirname "$0")"
sel=("$@")
[ ${#sel[@]} -eq 0 ] && sel=(01 02 03 04 05 06)
declare -A verdict
for s in "${sel[@]}"; do
    d=$(ls -d ${s}_*/ 2>/dev/null | head -1)
    [ -n "$d" ] || { echo "== $s: no such case"; verdict[$s]=MISSING; continue; }
    echo "=============================================================="
    echo "== $d  ($(date -Is))"
    echo "=============================================================="
    if ( cd "$d" && ./run.sh ); then verdict[$s]=PASS; else verdict[$s]=FAIL; fi
done
echo
echo "================= summary ================="
rc=0
for s in "${sel[@]}"; do
    printf "  %-4s %s\n" "$s" "${verdict[$s]}"
    [ "${verdict[$s]}" = PASS ] || rc=1
done
exit $rc
