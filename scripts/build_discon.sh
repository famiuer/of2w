#!/usr/bin/env bash
# build_discon.sh — rebuild the NREL 5MW Bladed-style controller DLLs that
# ServoDyn loads at runtime (DLL_FileName in *_ServoDyn.dat).
#
# The repo ships prebuilt x86-64 DISCON_OC3Hywind.so for convenience; if your
# cluster's glibc/arch rejects it (or you don't run shipped binaries), rebuild
# from the Fortran source that sits right next to it:
#
#   ./scripts/build_discon.sh          # rebuilds in every cases/*/ServoData
#
# Provenance: DISCON*.F90 are NREL's reference controllers from the OpenFAST
# r-test 5MW_Baseline (https://github.com/OpenFAST/r-test, Apache-2.0).
set -euo pipefail
cd "$(dirname "$0")/.."
command -v gfortran >/dev/null || { echo "ERROR: gfortran not found"; exit 1; }

n=0
for sd in cases/*/background/5MW_Baseline/ServoData; do
    [ -d "$sd" ] || continue
    ( cd "$sd"
      gfortran -shared -fPIC -O2 -o DISCON_OC3Hywind.so DISCON_OC3/DISCON_OC3Hywind.F90
      echo "rebuilt $sd/DISCON_OC3Hywind.so" )
    n=$((n+1))
done
[ "$n" -gt 0 ] || { echo "ERROR: no ServoData directories found"; exit 1; }
echo "done ($n controller(s) rebuilt)"
