#!/usr/bin/env bash
# 03_wave3d: setWaveParameters -> blockMesh (4.7M cells) -> setWaveField
# -> waveFoam (300 s) -> check.py verifies H, k, R at the gauges.
# HPC-scale: intended for ~48 ranks. SMOKE=1 runs 3 s on 8 ranks and only
# sanity-checks the outputs (the full 300 s is needed for check.py).
# Usage: ./run.sh   |   NP=<n> ./run.sh   |   SMOKE=1 ./run.sh
command -v waveFoam >/dev/null || {
    echo "ERROR: waves2Foam not in PATH (source env / build first)"; exit 1; }
set -e
cd "$(dirname "$0")"
if [ "${SMOKE:-0}" = "1" ]; then
    cp system/decomposeParDict.smoke system/decomposeParDict
    foamDictionary -entry endTime -set 3 system/controlDict > /dev/null
fi
NPD=$(awk '/^numberOfSubdomains/ {gsub(";","",$2); print $2; exit}' system/decomposeParDict)
NP=${NP:-$NPD}
[ "$NP" = "$NPD" ] || sed -i "s/^numberOfSubdomains.*/numberOfSubdomains  $NP;/" system/decomposeParDict

echo "== mesh + wave init"
setWaveParameters > log.setWaveParameters 2>&1
blockMesh > log.blockMesh 2>&1
setWaveField > log.setWaveField 2>&1
decomposePar -force > log.decomposePar 2>&1

echo "== waveFoam (NP=$NP, endTime $(foamDictionary -entry endTime -value system/controlDict) s)"
mpirun -np $NP waveFoam -parallel > log.waveFoam 2>&1

if [ "${SMOKE:-0}" = "1" ]; then
    echo "== smoke check (solver ran; gauges written and finite)"
    python3 - << 'PY'
import glob, sys, numpy as np
fs = sorted(glob.glob("postProcessing/surfaceElevation/*/surfaceElevation.dat"))
if not fs:
    sys.exit("FAIL — no gauge output")
d = np.loadtxt(fs[-1], skiprows=1)[3:]
if not np.isfinite(d).all():
    sys.exit("FAIL — non-finite gauge values")
print(f"gauges: {d.shape[1]-1}, {len(d)} instants to t={d[-1,0]:.1f} s, "
      f"eta range {d[:,1:].min():+.3f}..{d[:,1:].max():+.3f} m")
print("PASS — 3D tank runs and the wave is entering (full check needs 300 s)")
PY
    # restore the full-run settings the smoke mode overrode
    foamDictionary -entry endTime -set 300 system/controlDict > /dev/null
    sed -i "s/^numberOfSubdomains.*/numberOfSubdomains  48;/" system/decomposeParDict
else
    echo "== check"
    python3 check.py
fi
