# Installation

## Prerequisites

- **OpenFOAM v2306** (ESI/OpenCFD). Other v2*** releases probably work; only
  v2306 is tested. Install via your package manager, your HPC's module
  system, or build from source at
  `https://gitlab.com/openfoam/core/openfoam`.
- **C++17 compiler** (gcc ≥ 8, clang ≥ 7).
- **CMake ≥ 3.16**.
- **OpenMPI** matching the one OpenFOAM was built against.
- **BLAS + LAPACK** (OpenFAST requires these). On HPC clusters that ship a
  bare gcc, install `openblas` or load the cluster's BLAS module.
- **gfortran** (OpenFAST is mostly Fortran).
- **Python 3** with `numpy`, `scipy`, `matplotlib`, `openfast_io` (for the
  comparison tools).
- **Python `build`** (PyPA) only if you want MoorDyn's Python wrapper —
  see §3 for how to disable it.

### Locating OpenFOAM's bashrc

You need to `source` OpenFOAM's `bashrc` in every shell that builds
foamMooring or `libOF2`, or runs the reference case. The path depends on
how OpenFOAM was installed:

| Install method | Typical path |
|---|---|
| Ubuntu `apt` (ESI repo) | `/usr/lib/openfoam/openfoam2306/etc/bashrc` |
| HPC module + system install | `$OPENFOAM_HOME/OpenFOAM/etc/bashrc` after `module load openfoam/v2306` |
| Personal install | `$HOME/OpenFOAM/OpenFOAM-v2306/etc/bashrc` or wherever you unpacked it |
| Source build | `<build-prefix>/etc/bashrc` |

Test it works:

```bash
source /usr/lib/openfoam/openfoam2306/etc/bashrc    # adjust path
echo "$WM_PROJECT_DIR"                              # non-empty if sourced OK
which wmake interFoam blockMesh                     # should resolve
```

## 1 · Pull submodules

```bash
git submodule update --init --recursive
```

This populates `external/openfast/`, `external/moordyn/`,
`external/foamMooring/` at the pinned commits the coupling was tested
against:

| Submodule     | Upstream                                              | Pinned commit |
|---------------|-------------------------------------------------------|---------------|
| openfast      | `https://github.com/famiuer/openfast.git` (fork)      | `f54fe019` (branch `of2-coupling-v0.1`) |
| moordyn       | `https://github.com/famiuer/MoorDyn.git` (fork)  | `405dd8cc`    |
| foamMooring   | `https://github.com/famiuer/foamMooring.git` (fork)   | `d2f70c3` (branch `of2-restart`) |
| stabRAS       | `https://github.com/famiuer/stabRAS_v1712.git` (fork) | `f066370` (branch `of2306-port`) |

You can bump these later by `cd external/<name> && git checkout <newer>`,
then committing the submodule pointer.

## 2 · Build OpenFAST (out of tree)

```bash
cmake -S external/openfast -B build/openfast \
      -DCMAKE_BUILD_TYPE=Release
cmake --build build/openfast -j$(nproc)
```

This produces `build/openfast/glue-codes/openfast/openfast` (standalone
driver, used to run baselines) and `build/openfast/modules/openfast-library/libopenfastlib.so`
(used by the coupling). The C++ driver is not required and is off by
default; if you need it, add `-DBUILD_OPENFAST_CPP_API=ON`.

> **Note**: the submodule pin (f54fe0192) is on famiuer/openfast, branch
> `of2-coupling-v0.1`. It carries the OF² Fortran patches
> (`PMI_Bridge` module + `OF2_SetImposedPlatformState`,
> `OF2_GetTowerBaseReaction`, `OF2_IsActive` C-ABI symbols) that
> `libOF2.so` calls into. These patches are not in upstream
> NREL/OpenFAST.

## 3 · Build MoorDyn

```bash
cmake -S external/moordyn -B build/moordyn \
      -DCMAKE_BUILD_TYPE=Release \
      -DPYTHON_WRAPPER=OFF \
      -DFORTRAN_WRAPPER=OFF
cmake --build build/moordyn -j$(nproc)
cmake --install build/moordyn --prefix $(pwd)/build/moordyn/install
```

> `-DPYTHON_WRAPPER=OFF` avoids a configure-time hard dependency on the
> PyPA `build` package. If you actually want the Python wrapper,
> `pip install --user build` first, then drop the flag.
> `-DFORTRAN_WRAPPER=OFF` avoids the Fortran bindings (not needed by
> foamMooring).

## 4 · Build foamMooring (depends on OpenFOAM + MoorDyn)

```bash
source /usr/lib/openfoam/openfoam2306/etc/bashrc    # see Prerequisites
export MOORDYN_INSTALL_DIR=$(pwd)/build/moordyn/install
cd external/foamMooring && ./Allwmake
cd ../..
```

`librigidBodyMooring.so` (used by the reference case's `moorDynR2`
restraint) lands in `$FOAM_USER_LIBBIN`.

## 4b · Build the stabilised turbulence models (stabRAS)

```bash
cd external/stabRAS && wmake libso && cd ../..
```

`libstabRASModels.so` lands in `$FOAM_USER_LIBBIN`. This is the
Larsen & Fuhrman (2018) stabilised closure set (`kOmegaSSTStab` is the
production model); the fork's `of2306-port` branch carries the OpenFOAM
v2306 dialect. Cases load it via `libs ("libstabRASModels.so");` in
`system/controlDict` and select it in `constant/turbulenceProperties`.

## 5 · Build the coupling library

```bash
cmake -S coupling -B build/coupling \
      -DOPENFAST_BUILD_DIR=$(pwd)/build/openfast \
      -DCMAKE_BUILD_TYPE=Release
cmake --build build/coupling -j$(nproc)
(cd build/coupling && ctest --output-on-failure)
```

Run `ctest` from inside `build/coupling/` (not via `--test-dir` from
outside — that doesn't always discover them with this CMake layout).
9 tests should pass; they exercise the OpenFAST and MoorDyn adapters
against in-process fakes, so no actual coupling run is invoked here.

## 6 · Build `libOF2.so` (the OpenFOAM-side plugin)

```bash
source /usr/lib/openfoam/openfoam2306/etc/bashrc           # see Prerequisites
export OPENFAST_BUILD_DIR=$(pwd)/build/openfast
export OPENFAST_SRC_DIR=$(pwd)/external/openfast
(cd coupling/restraints/openfoam/of2 && wmake libso)
```

`libOF2.so` lands in `$FOAM_USER_LIBBIN`.

The two env vars are **required** — they're consumed by the plugin's
`Make/options`, which has no defaults (intentionally — to fail fast
rather than silently build against the wrong OpenFAST).

## 7 · Run the reference case

The test cases live in `cases/` (see `cases/README.md`; start with
`cases/01_minimal_coupling`, a minutes-scale full-coupling check with
restart). The unit tests of the coupling core can be run without any
case:

```bash
cmake -S coupling -B build/coupling
cmake --build build/coupling -j
cd build/coupling && ctest --output-on-failure    # 9 tests
```

## HPC notes

For HPC clusters that load OpenFOAM via `module`, the bashrc has to be
sourced AFTER the module load — in a non-interactive (slurm batch)
shell, putting `module load openfoam/v2306; source
$OPENFOAM_HOME/OpenFOAM/etc/bashrc` in the script is the working
pattern (see `cases/06_wave_turbine/slurm.example`).

If your node has more than 10 cores, bump `numberOfSubdomains` in
`system/decomposeParDict` — the `mesh` script and the run line above
will pick up the new value automatically.

## Troubleshooting (first-run mistakes)

| Symptom | Cause | Fix |
|---|---|---|
| `source: command not found` for OpenFOAM bashrc | Wrong path | Find with `dpkg -L openfoam2306-default \| grep bashrc` or `module show openfoam` |
| `wmake: not found` after sourcing bashrc | OpenFOAM bashrc was sourced via pipe / subshell | Source in the *current* shell, not via `module load … | tail` |
| MoorDyn cmake configure fails on Python `build` | `-DPYTHON_WRAPPER=ON` (default) | Add `-DPYTHON_WRAPPER=OFF -DFORTRAN_WRAPPER=OFF` |
| `libOF2.so` build fails: missing `FAST_Library.h` | `OPENFAST_SRC_DIR` unset | `export OPENFAST_SRC_DIR=$(pwd)/external/openfast` |
| `libOF2.so` build links a different OpenFAST | `OPENFAST_BUILD_DIR` pointing at a stale build | re-set it; verify with `nm -D` on the built `libOF2.so` |
| `FAST_Sizes failed (ErrStat=4)` at solver startup | Missing or corrupt `openfast/derisk.fst` | Verify the file exists and parses; common header bug: header must be exactly 2 lines before `Echo` |
| `mpirun: not enough slots` | `numberOfSubdomains` > physical cores | Reduce in `system/decomposeParDict` AND re-run `./mesh` |
| ctest says "No tests were found" | called from wrong cwd | `cd build/coupling && ctest --output-on-failure` |
