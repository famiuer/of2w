#!/usr/bin/env bash
# 01_minimal_coupling: mesh -> runA (0-1 s, uninterrupted) -> runB (0-0.5 s,
# stop, restart to 1 s) -> check.py verifies runB overlays runA.
# Usage: ./run.sh   |   NP=<n> ./run.sh
command -v overInterDyMFoam >/dev/null || {
    echo "ERROR: OpenFOAM env not sourced"; exit 1; }
set -e
B=$(cd "$(dirname "$0")" && pwd)
NP=${NP:-10}
SPLICE=0.5
export FOAM_SIGFPE=false

if [ ! -d "$B/background/constant/polyMesh" ]; then
    echo "== mesh (NP=$NP)"
    ( cd "$B" && NP=$NP ./mesh > log.mesh 2>&1 )
fi

for leg in runA runB; do
    rm -rf "$B/$leg" && cp -a "$B/background" "$B/$leg"
done

echo "== runA: 0 -> 1.0 s"
( cd "$B/runA" && mpirun -np $NP overInterDyMFoam -parallel > log.solver 2>&1 )

echo "== runB: 0 -> $SPLICE s, stop"
( cd "$B/runB" && foamDictionary -entry endTime -set $SPLICE system/controlDict > /dev/null
  mpirun -np $NP overInterDyMFoam -parallel > log.solver.seg1 2>&1 )

# restart point must be complete: RBD + coupling + mooring states + FAST chkp
u="$B/runB/processor0/$SPLICE/uniform"
for f in rigidBodyMotionState of2RestraintState moorDynState; do
    [ -f "$u/$f" ] || { echo "FAIL: missing $f at t=$SPLICE"; exit 1; }
done
root=$(sed -n 's/^ *fastCheckpointRoot *"\{0,1\}\([^";]*\).*/\1/p' "$u/of2RestraintState" | head -1)
[ -n "$root" ] && [ -f "$B/runB/$root.chkp" ] || { echo "FAIL: missing FAST checkpoint"; exit 1; }

echo "== runB: restart $SPLICE -> 1.0 s"
( cd "$B/runB" && foamDictionary -entry endTime -set 1.0 system/controlDict > /dev/null
  mpirun -np $NP overInterDyMFoam -parallel > log.solver.seg2 2>&1 )

echo "== check"
python3 "$B/check.py"
