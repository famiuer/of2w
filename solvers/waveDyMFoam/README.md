# waveDyMFoam

**Morphing-mesh** (deforming, body-fitted) VOF solver **with waves2Foam
relaxation-zone wave generation/absorption** — the single-mesh sibling of
`overWaveDyMFoam`. Used by the `*_dm` coupled OC4 cases (waves/current +
rigidBodyMeshMotion + OF2 + MoorDyn); the OF2 + MoorDyn coupling rides along
unchanged as runtime motion/restraint libraries.

Built/validated on OpenFOAM **v2306** (ESI).

## Provenance — why this is (almost) just waveFoam

waves2Foam's own `solvers2306_PLUS/waveFoam` port is based on stock v2306
`interFoam`, which already merged the dynamic-mesh machinery:
`createDynamicFvMesh.H`, per-outer-corrector `mesh.update()`, the
`gh/ghf = g & (C - referencePoint)` recompute on mesh change (waves2Foam's
referencePoint form), and `correctPhi`. **waveDyMFoam is that same source
compiled under an explicit name** so morphing cases state their intent in
`controlDict` (`application waveDyMFoam;`). No functional diff vs waveFoam.

## Build

Local:
    source /usr/lib/openfoam/openfoam2306/etc/bashrc
    source <waves2Foam>/bin/bashrc
    cd <waves2Foam>/applications/solvers/solvers2306_PLUS/waveDyMFoam && wmake
## Verification

1. **Dynamic path**: validated 2D wave tank (`lc3p1wave2d_v6`) with an ACTIVE
   `velocityLaplacian` motion solver (zero boundary motion): 0.6 s, motion
   equation solved every step, relaxation zones active, phase volume conserved.
2. **Full stack**: foamMooring `sixDoF_2D/deformMesh` tutorial converted to
   waves2Foam zones (StokesII H=0.12 m, T=2 s): sixDoF morphing +
   moorDynR2 (MoorDyn v2.6.1, BODY mode) + relaxation, 2 s / 1511 steps clean,
   physically sensible moored-box drift.
3. **Coupled OC4 morphing pilot**: 6.8M-cell body-fitted mesh,
   rigidBodyMeshMotion (inner 10 / outer 30) + OF2W + MoorDyn; the platform
   tracks the OpenFAST baseline orbit through the first wave periods.

## Case-setup requirements (cost time; learned the hard way)

- Laplacian-type motion solvers need a `"cellMotionU.*"` entry in
  `fvSolution.solvers` (dies after step 1 without it). rigidBody/sixDoF
  morphing (displacement-based, algebraic) does not.
- Mooring restraints: add `"libsixDoFMooring.so"` (sixDoF framework) or
  `"librigidBodyMooring.so"` (RBD framework) to `motionSolverLibs`.
- waves2Foam preprocessing needs the OLD dimensioned `transportProperties`
  entries (`rho rho [1 -3 0 0 0 0 0] 1025;`) and never use the ESI
  `waveVelocity`/`waveAlpha` BC type names (library name collision).
- `correctPhi` is active on moving meshes -> `"pcorr.*"` solver entry needed.
- Two-phase static-hold starts (kill the current-IC momentum kick):
  `staticFvMesh` hold writes NO `pointDisplacement`; backfill it from `0/`
  before the coupled release (`run_hold_release.sh` in the *_dm cases).
