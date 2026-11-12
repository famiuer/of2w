#!/usr/bin/env bash
# truth_guard.sh — QC-3: build knowledge lives ONLY in bootstrap.sh (and
# the manual steps it mirrors in INSTALL.md). The container recipe and CI
# workflows may INVOKE bootstrap.sh and the case scripts, but must never
# carry build commands of their own — that is how recipes silently drift
# from the documented path.
#
# Scope (deliberately narrow, to stay maintainable):
#   Containerfile*     : no wmake/Allwmake/'cmake -S'/make calls at all
#   .github/workflows/ : no wmake/Allwmake ('cmake -S coupling' is allowed
#                        there once: the fast lane's own test subject)
set -u
fail=0
root=$(cd "$(dirname "$0")/.." && pwd)

check() {  # $1 file, $2 extended-regex of forbidden patterns
    local hits
    hits=$(grep -nE "$2" "$1" | grep -vE '^\s*[0-9]+:\s*#' || true)
    if [ -n "$hits" ]; then
        echo "TRUTH-GUARD FAIL: build logic outside bootstrap.sh in $1:"
        echo "$hits" | sed 's/^/    /'
        fail=1
    fi
}

for f in "$root"/Containerfile*; do
    [ -f "$f" ] && check "$f" '(^|[^A-Za-z])(wmake|Allwmake|cmake[[:space:]]+-S|make[[:space:]]+-j)'
done
for f in "$root"/.github/workflows/*.yml; do
    [ -f "$f" ] && check "$f" '(^|[^A-Za-z])(wmake|Allwmake)'
done

[ $fail -eq 0 ] && echo "truth-guard OK: no build logic has leaked out of bootstrap.sh"
exit $fail
