#!/usr/bin/env bash
# 04_wavecurrent2d — 2D combined wave+current tank.
# Stokes-II H=6 m, T=10 s (absolute) on a 1/7-power-law current U_s=0.5 m/s.
# endTime 300 s on 16 cores (~3-4 h).
#
# NOTE: constant/waveProperties and 0/ ship PRE-GENERATED — the wave period
# in the dict is Doppler-compensated (10.303 s internal so the absolute
# period under current is 10 s) and 0/U carries the initial current profile.
# Do NOT rerun setWaveParameters/setWaveField (they would revert this).
# Usage: ./run.sh    |    NP=<n> ./run.sh
command -v waveFoam >/dev/null || {
    echo "ERROR: waves2Foam not in PATH (source env / build first)"; exit 1; }
set -e
B=$(cd "$(dirname "$0")" && pwd)
cd "$B"
NPD=$(awk '/^numberOfSubdomains/ {gsub(";","",$2); print $2; exit}' system/decomposeParDict)
NP=${NP:-$NPD}
sed -i -e "s/^numberOfSubdomains.*/numberOfSubdomains  $NP;/" \
       -e "s/n ([0-9]\+ 1 1)/n ($NP 1 1)/" system/decomposeParDict

blockMesh > log.blockMesh 2>&1
decomposePar -force > log.decomposePar 2>&1
echo "[$(date -Is)] waveFoam on $NP ranks (endTime 300 s)"
mpirun -np $NP waveFoam -parallel > log.waveFoam 2>&1
echo "[$(date -Is)] solver done: $(grep -oE '^Time = [0-9.]+' log.waveFoam | tail -1)"
python3 check.py
