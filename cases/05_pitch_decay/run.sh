#!/usr/bin/env bash
# 05_pitch_decay: mesh -> overInterDyMFoam (release from +5 deg pitch,
# 120 s) -> check.py overlays the decay on the OpenFAST baseline.
# Usage: ./run.sh   |   NP=<n> ./run.sh
command -v overInterDyMFoam >/dev/null || {
    echo "ERROR: OpenFOAM env not sourced"; exit 1; }
set -e
B=$(cd "$(dirname "$0")" && pwd)
NP=${NP:-10}
export FOAM_SIGFPE=false

if [ ! -d "$B/background/constant/polyMesh" ]; then
    echo "== mesh (NP=$NP)"
    ( cd "$B" && NP=$NP ./mesh > log.mesh 2>&1 )
fi

echo "== overInterDyMFoam (NP=$NP, endTime 120 s)"
( cd "$B/background" && mpirun -np $NP overInterDyMFoam -parallel > log.solver 2>&1 )

echo "== check"
python3 "$B/check.py"
