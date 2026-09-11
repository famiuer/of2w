#!/usr/bin/env bash
# 06_wave_turbine: mesh -> leg A (wave development from rest, body HELD,
# 0-80 s) -> leg B (coupled release at 80 s, default 60 s of motion).
# HPC-scale (5.19M cells): wrap in your scheduler (see slurm.example).
# Usage: ./run.sh   |   NP=<n> ./run.sh   |   MOVING=<s> ./run.sh
command -v overWaveDyMFoam >/dev/null || {
    echo "ERROR: overWaveDyMFoam not in PATH (build OF2W first)"; exit 1; }
set -e
B=$(cd "$(dirname "$0")" && pwd)
NP=${NP:-48}
REL=80                # release time (=8T: waves fully developed at the body)
MOVING=${MOVING:-60}  # coupled seconds after release
export FOAM_SIGFPE=false

if [ ! -d "$B/background/constant/polyMesh" ]; then
    echo "== mesh + wave deck (NP=$NP)"
    ( cd "$B" && NP=$NP ./mesh > log.mesh 2>&1 )
fi
cd "$B/background"

if [ ! -d processor0/$REL ]; then
    echo "== leg A: wave development from rest, body held (0 -> $REL s)"
    cp constant/dynamicMeshDict_static constant/dynamicMeshDict
    foamDictionary -entry endTime       -set $REL system/controlDict > /dev/null
    foamDictionary -entry writeInterval -set 5    system/controlDict > /dev/null
    mpirun -np $NP overWaveDyMFoam -parallel > log.solver.hold 2>&1
    T=$(grep -oE "^Time = [0-9.]+" log.solver.hold | tail -1 | awk '{print $3}')
    awk -v t="$T" -v r="$REL" 'BEGIN{exit !(t>=r-0.01)}' || {
        echo "FAIL: hold leg stopped at t=$T (< $REL s)"; exit 1; }
    # backfill pointDisplacement (static classes never write it; mesh unmoved)
    for p in processor*; do
        [ -f $p/$REL/pointDisplacement ] || cp $p/0/pointDisplacement $p/$REL/
    done
fi

echo "== leg B: coupled release at t=$REL (-> $((REL + MOVING)) s)"
cp constant/dynamicMeshDict.coupled constant/dynamicMeshDict
foamDictionary -entry endTime       -set $((REL + MOVING)) system/controlDict > /dev/null
foamDictionary -entry writeInterval -set 5 system/controlDict > /dev/null
mpirun -np $NP overWaveDyMFoam -parallel > log.solver 2>&1
echo "== done: $(grep -oE '^Time = [0-9.]+' log.solver | tail -1)"
