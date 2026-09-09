# overWaveDyMFoam

Overset VOF + 6-DOF dynamic-mesh solver **with waves2Foam relaxation-zone wave
generation/absorption** compiled in. It is OpenFOAM's stock `overInterDyMFoam`
(`$FOAM_APP/solvers/multiphase/interFoam/overInterDyMFoam`) plus the same minimal
patch that waves2Foam applies to turn `interFoam` into `waveFoam`. This lets the
OC4 / floating-body **overset** cases run with **waves2Foam** waves; the OF2 +
MoorDyn coupling rides along unchanged (they are runtime-loaded motion/restraint
libraries, not solver code).

Built/validated on OpenFOAM **v2306** (ESI). Verified on the `rgb_overset`
floating-box-in-waves overset+mooring case (relaxation active, stable; the native
ESI wave BC FPE-crashed on the same case).

## Provenance — the exact patch (vs stock overInterDyMFoam)
Copied stock `overInterDyMFoam`, renamed the `.C`, and applied the waveFoam-vs-interFoam diff:
1. `#include "relaxationZone.H"` + `#include "externalWaveForcing.H"` (top).
2. Comment out `#include "postProcess.H"`.
3. After `createDynamicFvMesh.H`, **before** `createFields`: add
   `readGravitationalAcceleration.H`, `readWaveProperties.H`, `createExternalWaveForcing.H`
   (defines `g`, `referencePoint`, `externalWave`).
4. `externalWave->step();` right after the `Time =` print (before the PIMPLE loop).
5. In the `mesh.changing()` block: `gh = g & (mesh.C() - referencePoint);` / `ghf = ...`
   (was `(g & mesh.C()) - ghRef`).
6. `relaxing.correct();` immediately after `#include "alphaEqnSubCycle.H"`.
7. `externalWave->close();` at the end.
8. `createFields.H`: comment out `readGravitationalAcceleration.H`/`readhRef.H`/`gh.H`,
   replace with `volScalarField gh("gh", g & (mesh.C()-referencePoint));` + `ghf`; and at
   the end add `relaxationZone relaxing(mesh, U, alpha1);`.

`Make/options` = overInterDyMFoam's (its classic overset + immiscibleIncompressibleTwoPhaseMixture
stack) with the waves2Foam includes/libs/defines added; `Make/files` installs to `$(WAVES_APPBIN)`.

## Build
Requires a **compiled waves2Foam** (it links `libwaves2Foam`). On a fresh setup:
```sh
source /usr/lib/openfoam/openfoam2306/etc/bashrc      # OpenFOAM v2306
source <waves2Foam>/bin/bashrc                        # sets WAVES_SRC, WAVES_LIBBIN, WAVES_APPBIN
( cd <waves2Foam> && ./Allwmake )                     # build the waves2Foam library (once)
wmake                                                  # build this solver -> $WAVES_APPBIN/overWaveDyMFoam
```
The solver builds from this directory (paths are env-var based, not relative to the
waves2Foam tree).

## waves2Foam dependency (vendored as a submodule)
This solver links `libwaves2Foam`, so it needs a **compiled waves2Foam**. Upstream
`https://github.com/ogoe/waves2Foam.git` (`a8d38fd`) is **not** sufficient — the working build
carries local changes not in upstream:
- `applications/solvers/solvers2306_PLUS` — a **symlink → `solvers2206_PLUS`** so the v2306
  toolchain resolves the `_PLUS` solver tree.
- `applications/solvers/solvers2206_PLUS/overWaveDyMFoam/` — the solver source (mirrored here).
- `waveFoam/Make/files` `EXE -> $(WAVES_APPBIN)`; `bin/prepareCase.sh` made executable.

These are captured in the fork **`famiuer/waves2Foam` @ branch `of2-coupling-v0.1`**, vendored
at `external/waves2Foam` (see `.gitmodules`). Clone recursively to get it:
```sh
git clone --recurse-submodules <of2-coupling>      # or: git submodule update --init external/waves2Foam
```
The copy under `solvers/overWaveDyMFoam/` here is the **same source** (kept for review/provenance);
the submodule is what actually builds (`source external/waves2Foam/bin/bashrc` then `wmake`).

## Case usage
- `controlDict`: `application overWaveDyMFoam;` and **drop `libwaveModels.so`** from `libs`
  (the ESI wave BCs name-clash with waves2Foam BCs).
- `constant/waveProperties`: waves2Foam relaxation-zone format (`relaxationNames`, a generating
  zone e.g. `stokesSecond`, an absorbing outlet zone). Run `setWaveParameters`.
- `constant/transportProperties`: use the **verbose dimensioned** form
  (`rho rho [1 -3 0 0 0 0 0] 1000;`) — the compact `rho 1;` fails in waves2Foam.
- Inlet/outlet BCs become passive (`U fixedValue (0 0 0)`, `alpha.water zeroGradient`); the
  relaxation zones do generation + absorption. `setWaveField` is optional (still-water IC + the
  zones suffice). Keep the OF2/MoorDyn libs in `dynamicMeshDict`.
- The relaxation zones must sit in the **static background far-field** (inlet/outlet), clear of
  the overset hole/fringe region.

Idea/recipe: a maoyanjun GitHub post documenting `waveDyMFoam` + `overWaveDyMFoam`.
