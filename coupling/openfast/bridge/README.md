# OpenFAST Bridge Scaffold

This directory contains the **cluster-target scaffold** for the custom
OpenFAST bridge that follows the WES/OF2 logic used in this repo.

The intended ownership split remains:

- OpenFOAM owns platform motion
- MoorDyn stays on the OpenFOAM side
- OpenFAST runs as a slave under imposed platform motion
- OpenFAST returns one aggregated structural/interface wrench

## Why this bridge exists

The runtime in
[ForcedOpenFASTLibrary.cpp](coupling/openfast/runtime/src/ForcedOpenFASTLibrary.cpp#L1)
already expects these custom symbols:

- `FAST_OC4_Platform_SetMotion`
- `FAST_OC4_Platform_GetReactionLoad`
- `FAST_OC4_Platform_GetDiagnostics`

That runtime also resolves the stock OpenFAST-library symbols from the **same**
shared-library handle. So the production cluster build must export both:

- stock `FAST_*` symbols
- custom `FAST_OC4_*` symbols

from one library path.

## Why this patches `FAST_Library.f90`

This is the most important implementation detail in the scaffold.

The stock OpenFAST shared library keeps `Turbine(:)` private inside:

- [FAST_Library.f90](external/openfast/modules/openfast-library/src/FAST_Library.f90#L1)

That means a useful OF2-style bridge cannot be a completely separate external
module unless we also add new accessor plumbing. The cleanest path is to patch
the bridge **inside the `FAST_Data` module** so it can see the evolving turbine
state directly.

That is why this scaffold is delivered as `include` fragments rather than as a
standalone independently compiled Fortran module.

## Files

- [OpenFASTBridgeCApi.h](coupling/openfast/bridge/include/coupling/OpenFASTBridgeCApi.h#L1)
  defines the exact custom C symbols expected by the runtime.
- [FAST_OC4_Platform_Bridge_Data.inc.f90](coupling/openfast/bridge/src/FAST_OC4_Platform_Bridge_Data.inc.f90#L1)
  adds bridge state inside `FAST_Data`.
- [FAST_OC4_Platform_Bridge_Procedures.inc.f90](coupling/openfast/bridge/src/FAST_OC4_Platform_Bridge_Procedures.inc.f90#L1)
  adds the exported bridge routines and named hook points for the real
  OpenFAST-side motion/load mapping.

## How this follows the WES article logic

The scaffold mirrors the same structural idea visible in the local OpenFAST
`WaveTank` glue code:

- [WaveTank_Struct.f90](external/openfast/glue-codes/labview/src/WaveTank_Struct.f90#L274)
  creates PRP-to-tower, PRP-to-hub, and hub-to-blade-root motion maps.
- [WaveTank_Struct.f90](external/openfast/glue-codes/labview/src/WaveTank_Struct.f90#L331)
  updates platform motion and transfers it into the internal structural meshes.
- [WaveTank_Struct.f90](external/openfast/glue-codes/labview/src/WaveTank_Struct.f90#L410)
  aggregates blade-root and hub loads back to the platform reference point.
- [WaveTank.f90](external/openfast/glue-codes/labview/src/WaveTank.f90#L613)
  returns one `FrcMom` vector to the external caller.

That is the same high-level contract we want here:

1. store OpenFOAM-imposed platform motion
2. apply it to the OpenFAST-side PRP motion state
3. map it through tower/hub/blade-root motion
4. aggregate returned turbine loads
5. expose one interface wrench back to OpenFOAM

## Cluster integration steps

1. Copy the two `.inc.f90` files into
   `external/openfast/modules/openfast-library/src/`.
2. Edit
   [FAST_Library.f90](external/openfast/modules/openfast-library/src/FAST_Library.f90#L1)
   inside `MODULE FAST_Data`:
   - insert `include 'FAST_OC4_Platform_Bridge_Data.inc.f90'` before `contains`
   - insert `include 'FAST_OC4_Platform_Bridge_Procedures.inc.f90'` before
     `end module FAST_Data`
3. Add the real OpenFAST-side hook calls:
   - call `OC4Bridge_ApplyStoredPlatformMotion` inside `FAST_CFD_Solution0`
     after `FAST_Solution0_T`
   - call `OC4Bridge_ApplyStoredPlatformMotion` inside `FAST_CFD_UpdateStates`
     before `FAST_UpdateStates_T`
   - call `OC4Bridge_RefreshReactionLoadAndDiagnostics` inside
     `FAST_CFD_AdvanceToNextTimeStep` after `FAST_AdvanceToNextTimeStep_T`
4. Build `openfastlib` so the resulting shared library exports both the stock
   `FAST_*` API and the custom `FAST_OC4_*` bridge symbols.

## Current status

This scaffold is intentionally honest:

- the exported `FAST_OC4_*` interfaces are now specified in code
- the bridge state layout is in place
- the correct OpenFAST patch location is documented
- the motion/load hook points are named and explained

What is still not implemented is the **real internal mapping** from stored
platform motion into OpenFAST meshes and from OpenFAST structural/aero loads
back into the returned interface wrench. That work must be done in the cluster
OpenFAST build environment against the real internal types.
