#!/bin/sh
# run_tests.sh — build + run the OF2 coupling-frame unit tests (NO CFD run).
# Tier 1: standalone g++ test of the dependency-free core (frameAlgebra.hpp).
# Tier 2: Foam-linked test of the production CouplingFrame (real Foam types).
set -e
HERE=$(cd "$(dirname "$0")" && pwd)
cd "$HERE"

echo "===================== Tier 1: standalone core (g++) ====================="
g++ -std=c++17 -O2 -I.. frameAlgebra_test.cpp -o frameAlgebra_test
./frameAlgebra_test
T1=$?
g++ -std=c++17 -O2 -I.. aitken_test.cpp -o aitken_test
./aitken_test
[ $? -ne 0 ] && T1=1

echo
echo "================= Tier 2: production CouplingFrame (Foam) ================"
if [ -z "$WM_PROJECT_DIR" ]; then
    echo "  (OpenFOAM not sourced; skipping Tier 2 — 'source .../etc/bashrc' to enable)"
    T2=0
else
    wmake >/dev/null 2>&1
    "$FOAM_USER_APPBIN/testCouplingFrame"
    T2=$?
fi

echo
echo "============ Tier 3: full restrain() integration (FAST stubbed) =========="
if [ -z "$WM_PROJECT_DIR" ]; then
    echo "  (OpenFOAM not sourced; skipping Tier 3)"
    T3=0
elif [ -z "$OPENFAST_SRC_DIR" ]; then
    echo "  (set OPENFAST_SRC_DIR to enable Tier 3 — needs FAST_Library.h for constants)"
    T3=0
else
    ( cd harness && wmake >/dev/null 2>&1 && "$FOAM_USER_APPBIN/testRestrainHarness" )
    T3=$?
fi

echo
if [ "$T1" -eq 0 ] && [ "$T2" -eq 0 ] && [ "$T3" -eq 0 ]; then
    echo "##### OF2 COUPLING TESTS: ALL GREEN (3 tiers) #####"; exit 0
else
    echo "##### OF2 COUPLING TESTS: FAILURES (T1=$T1 T2=$T2 T3=$T3) #####"; exit 1
fi
