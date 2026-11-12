#!/usr/bin/env bash
# 00_ci_smoke: CI-sized clone of 01_minimal_coupling (~40k cells, 2 ranks,
# 0.08 s, FIXED dt). mesh -> runA (0-0.08 s) -> runB (0-0.04 s, stop, restart)
# -> check.py verifies runB overlays runA (self-referential: no golden
# numbers, machine-independent by construction).
# Usage: ./run.sh   |   NP=<n> ./run.sh    (CI sets --oversubscribe itself)
command -v overInterDyMFoam >/dev/null || {
    echo "ERROR: OpenFOAM env not sourced"; exit 1; }
set -e
B=$(cd "$(dirname "$0")" && pwd)
NP=${NP:-2}
MPIFLAGS=${CI:+--oversubscribe}   # CI runners advertise fewer slots than ranks
SPLICE=0.04
export FOAM_SIGFPE=false

# re-mesh when absent OR decomposed for a different rank count (a stale
# decomposition makes mpirun fail with a cryptic processor-dir error)
NPROC=$(ls -d "$B"/background/processor* 2>/dev/null | wc -l)
if [ ! -d "$B/background/constant/polyMesh" ] || [ "$NPROC" -ne "$NP" ]; then
    echo "== mesh (NP=$NP)"
    ( cd "$B" && NP=$NP ./mesh > log.mesh 2>&1 ) || { echo "MESH FAILED — see log.mesh"; exit 1; }
fi

for leg in runA runB; do
    rm -rf "$B/$leg" && cp -a "$B/background" "$B/$leg"
done

echo "== runA: 0 -> 0.08 s"
( cd "$B/runA" && mpirun ${MPIFLAGS:-} -np $NP overInterDyMFoam -parallel > log.solver 2>&1 )

echo "== runB: 0 -> $SPLICE s, stop"
( cd "$B/runB" && foamDictionary -entry endTime -set $SPLICE system/controlDict > /dev/null
  mpirun ${MPIFLAGS:-} -np $NP overInterDyMFoam -parallel > log.solver.seg1 2>&1 )

# restart point must be complete: RBD + coupling + mooring states + FAST chkp
u="$B/runB/processor0/$SPLICE/uniform"
for f in rigidBodyMotionState of2RestraintState moorDynState; do
    [ -f "$u/$f" ] || { echo "FAIL: missing $f at t=$SPLICE"; exit 1; }
done
root=$(sed -n 's/^ *fastCheckpointRoot *"\{0,1\}\([^";]*\).*/\1/p' "$u/of2RestraintState" | head -1)
[ -n "$root" ] && [ -f "$B/runB/$root.chkp" ] || { echo "FAIL: missing FAST checkpoint"; exit 1; }

echo "== runB: restart $SPLICE -> 0.08 s"
( cd "$B/runB" && foamDictionary -entry endTime -set 0.08 system/controlDict > /dev/null
  mpirun ${MPIFLAGS:-} -np $NP overInterDyMFoam -parallel > log.solver.seg2 2>&1 )

echo "== check"
python3 "$B/check.py"
