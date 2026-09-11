# Build & run runbook

Step-by-step instructions to go from a freshly cloned repo to a running
OC4-DeepCwind free-decay simulation.

> **Note:** this runbook predates the shipped test battery and refers to a
> `cases/oc4-decay-v6` layout; the curated cases now live in `cases/`
> (overset pitch decay: `cases/05_pitch_decay`). The build steps
> (sections 0–6) apply as-is.

## 0. Cluster prep

You need on the run node:
- OpenFOAM v2306 (module or install tree)
- OpenMPI matching OpenFOAM's build
- gcc/gfortran ≥ 8 (for OpenFAST)
- CMake ≥ 3.16
- BLAS/LAPACK (`openblas` is fine)
- Python 3 with numpy/matplotlib (for the case check scripts)

```bash
git clone <repo-url> of2-coupling
cd of2-coupling
git submodule update --init --recursive    # downloads OpenFAST + MoorDyn + foamMooring at pinned commits
```

## 1. OpenFAST

```bash
cmake -S external/openfast -B build/openfast \
      -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_INSTALL_PREFIX=$(pwd)/build/openfast/install
cmake --build build/openfast -j8
cmake --install build/openfast
```

`libopenfastlib.so` (which `libOF2.so` dynamically loads) is built by
default. The C++ driver is off by default; add
`-DBUILD_OPENFAST_CPP_API=ON` if you need it.

Sanity check:
```bash
./build/openfast/glue-codes/openfast/openfast --version
```

## 2. MoorDyn

```bash
cmake -S external/moordyn -B build/moordyn \
      -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_INSTALL_PREFIX=$(pwd)/build/moordyn/install
cmake --build build/moordyn -j8
cmake --install build/moordyn
```

## 3. foamMooring (depends on OpenFOAM + MoorDyn)

```bash
source $OPENFOAM_HOME/OpenFOAM/etc/bashrc
export MOORDYN_INSTALL_DIR=$(pwd)/build/moordyn/install
cd external/foamMooring
./Allwmake
cd ../..
```

`librigidBodyMooring.so` will land in `$FOAM_USER_LIBBIN`.

## 4. OF² core library + unit tests

```bash
cmake -S coupling -B build/coupling \
      -DOPENFAST_BUILD_DIR=$(pwd)/build/openfast \
      -DCMAKE_BUILD_TYPE=Release
cmake --build build/coupling -j8
ctest --test-dir build/coupling --output-on-failure
```

All tests should pass before proceeding. If any fail, the issue is
almost always one of:
- OpenFAST `libopenfastlib.so` not in `LD_LIBRARY_PATH` at test time
- gfortran vs C++ ABI mismatch (Fortran OPTIONAL args — see
  `docs/interface_spec.md`)

## 5. OpenFOAM-side plugin `libOF2.so`

```bash
source $OPENFOAM_HOME/OpenFOAM/etc/bashrc
cd coupling/restraints/openfoam/of2
wmake libso
```

Plugin lands at `$FOAM_USER_LIBBIN/libOF2.so`.

## 6. Run the reference case

```bash
cd cases/oc4-decay-v6
./mesh                                              # ~30 min on 64 ranks
mpirun -np 64 interFoam -parallel | tee log.solver  # ~5 hours
```

On a SLURM cluster, copy `slurm.example`, edit the env block, and
`sbatch` it.

## 7. Post-processing

`cases/05_pitch_decay/check.py` overlays the CFD decay on a standalone
OpenFAST + HydroDyn baseline (shipped precomputed in that case). To
generate the baseline, set up an OpenFAST-only case with `CompHydro=1`
and `CompMooring=3` and `PtfmHeave=1` IC; run it locally:

```bash
./build/openfast/glue-codes/openfast/openfast baseline.fst
```

The OC4-DeepCwind WAMIT data (`marin_semi.{1,3,hst}`) needed by HydroDyn
is part of the NREL r-test repository:

- https://github.com/OpenFAST/r-test/tree/main/glue-codes/openfast/5MW_OC4Semi_WSt_WavesWN

Clone or download the `HydroData/marin_semi*` files from there and
point `HydroDyn.dat`'s `PotFile` at them.

## Known gotchas

| Symptom | Cause | Fix |
|---|---|---|
| `surfaceMeshTriangulate: command not found` after `source bashrc` in non-interactive shell | OpenFOAM bashrc partially loaded | Source it directly in a login shell or in a bash wrapper that has been `set +u`'d; do **not** pipe `module load` |
| `Sub-cycling is not supported with the CrankNicolson ddt scheme` | `nAlphaSubCycles > 1` in fvSolution with `ddtSchemes { default CrankNicolson 0.9; }` | Keep `nAlphaSubCycles 1`, or switch ddt scheme to `Euler` |
| Body's `transform` translation seems not to move the body | Convention: OpenFOAM rigid-body `transform` sets the kinematic state, but `snappyHexMesh` carves the cavity at the literal STL world coords. To physically move the body, edit the STL or use `transformPoints`, not `transform` | See `cases/oc4-decay-v6/README.md` and `docs/architecture.md` |
| MoorDyn IC convergence FPE | `icStationary` is broken in MoorDyn v2.6.1 | Set `ICgenDynamic 1` in the moor input file; ensure Point IDs are sequential |
| ED reports PtfmHeave = 0 at t=0 even though I set PtfmHeave=+1 in `.fst` | In OF² mode (CompHydro=0), the of2Restraint overrides ED's IC with the body's *relative-from-initial* world position. To get a +1 m IC: lift the body via STL/transform, or lower the water level via `setFieldsDict` (water z=−1 m gives a +1 m relative offset without re-meshing) | This is the v6 setup; see `cases/oc4-decay-v6/README.md` |

## Diagnostic queries

To inspect the body's actual world position at t=0 (resolves the
transform-vs-STL ambiguity):

```bash
source $OPENFOAM_HOME/OpenFOAM/etc/bashrc
cd cases/oc4-decay-v6
foamToVTK -case . -constant -ascii -fields '()' -patches '(float)'
python3 -c "
import re, numpy as np
with open('VTK/oc4-decay-v6_1/boundary/float.vtp') as f: txt = f.read()
data = re.search(r'<Points>\s*<DataArray[^>]+>(.*?)</DataArray>', txt, re.S).group(1).split()
arr = np.array(list(map(float, data))).reshape(-1, 3)
print(f'float bbox: x=[{arr[:,0].min():.3f},{arr[:,0].max():.3f}], y=[{arr[:,1].min():.3f},{arr[:,1].max():.3f}], z=[{arr[:,2].min():.3f},{arr[:,2].max():.3f}]')
"
```

The float patch z-extent tells you where the hull *actually* sits in
the world, independent of the `transform` translation.
