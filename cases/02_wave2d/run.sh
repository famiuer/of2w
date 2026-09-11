#!/usr/bin/env bash
# 02_wave2d: setWaveParameters -> blockMesh -> setWaveField -> waveFoam
# (300 s, ~2-3 h on 8 cores) -> check.py verifies H, k, R at the gauges.
# Usage: ./run.sh   |   NP=<n> ./run.sh
command -v waveFoam >/dev/null || {
    echo "ERROR: waves2Foam not in PATH (source env / build first)"; exit 1; }
set -e
cd "$(dirname "$0")"
NPD=$(awk '/^numberOfSubdomains/ {gsub(";","",$2); print $2; exit}' system/decomposeParDict)
NP=${NP:-$NPD}
[ "$NP" = "$NPD" ] || sed -i "s/^numberOfSubdomains.*/numberOfSubdomains  $NP;/" system/decomposeParDict

echo "== mesh + wave init"
setWaveParameters > log.setWaveParameters 2>&1   # waveProperties from .input master
blockMesh > log.blockMesh 2>&1
cp 0.org/alpha.water.template 0/alpha.water      # reset before setWaveField
setWaveField > log.setWaveField 2>&1
decomposePar -force > log.decomposePar 2>&1

echo "== waveFoam (NP=$NP, endTime 300 s)"
mpirun -np $NP waveFoam -parallel > log.waveFoam 2>&1

echo "== check"
python3 check.py
