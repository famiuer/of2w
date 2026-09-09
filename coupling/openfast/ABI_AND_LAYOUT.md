# Forced OpenFAST ABI

This note defines the exact exported C symbols expected from the future
`libForcedOpenFAST.so` wrapper and the minimal case layout that the current
`ForcedOpenFASTCApiAdapter` is built around.

The repo now includes a buildable scaffold runtime:

- `forcedopenfast_runtime` CMake target
- output shared library name: `libforcedopenfast.so`
- implementation file:
  [ForcedOpenFASTLibrary.cpp](coupling/openfast/runtime/src/ForcedOpenFASTLibrary.cpp#L1)

That runtime now supports two backend modes:

- `backend = "stub"` for local end-to-end tests
- `backend = "openfast_library"` for the production-target architecture

The `openfast_library` backend now implements the outer bridge skeleton:

- stock OpenFAST-library initialization and CFD stepping
- OF2-style custom platform-motion and reaction-load hooks
- diagnostics retrieval

The remaining cluster-side task is to provide the real custom bridge library on
top of OpenFAST rather than the local fake test implementation.

The new bridge scaffold for that work is now in:

- [coupling/openfast/bridge/README.md](coupling/openfast/bridge/README.md#L1)
- [OpenFASTBridgeCApi.h](coupling/openfast/bridge/include/coupling/OpenFASTBridgeCApi.h#L1)
- [FAST_OC4_Platform_Bridge_Data.inc.f90](coupling/openfast/bridge/src/FAST_OC4_Platform_Bridge_Data.inc.f90#L1)
- [FAST_OC4_Platform_Bridge_Procedures.inc.f90](coupling/openfast/bridge/src/FAST_OC4_Platform_Bridge_Procedures.inc.f90#L1)

Public header:

- [ForcedOpenFASTCApi.h](coupling/openfast/include/coupling/ForcedOpenFASTCApi.h#L1)

## Exported C symbols

Required:

- `ForcedOpenFAST_Create`
- `ForcedOpenFAST_Initialize`
- `ForcedOpenFAST_Step`
- `ForcedOpenFAST_GetTowerBaseLoad`
- `ForcedOpenFAST_GetRotorSpeed`
- `ForcedOpenFAST_GetGeneratorPower`
- `ForcedOpenFAST_GetBladePitch`
- `ForcedOpenFAST_GetTowerTopDisplacement`
- `ForcedOpenFAST_GetControllerStatus`
- `ForcedOpenFAST_Close`

Recommended:

- `ForcedOpenFAST_GetLastError`
- `ForcedOpenFAST_GetVersion`

## Motion input contract

The wrapper receives one imposed platform state with:

- displacement `[x, y, z, roll, pitch, yaw]`
- velocity `[vx, vy, vz, omega_x, omega_y, omega_z]`
- acceleration `[ax, ay, az, alpha_x, alpha_y, alpha_z]`

Units:

- positions in meters
- angles in radians
- translational velocity in m/s
- angular velocity in rad/s
- translational acceleration in m/s^2
- angular acceleration in rad/s^2

This contract intentionally matches the OF2-style loose-coupling path:

- OpenFOAM owns platform motion
- OpenFAST consumes imposed motion
- OpenFAST returns one interface reaction wrench

## Load output contract

For version 1, the wrapper returns:

- `applicationPoint[3]`
- `force[3]`
- `moment[3]`

All are in the global coupling frame.

Interpretation:

- `force` is the turbine reaction force
- `moment` is the turbine reaction moment about `applicationPoint`
- `applicationPoint` should correspond to the tower base or another explicitly documented structural interface point

## Minimal case layout

The adapter’s `casePath` should point to a directory containing:

- [forced_openfast_case.toml](coupling/config/examples/openfast_smoke/forced_openfast_case.toml#L1)
- an `openfast/` subtree with the referenced OpenFAST files

Reference examples:

- [openfast_smoke/README.md](coupling/config/examples/openfast_smoke/README.md#L1)
- [openfast_library_smoke/README.md](coupling/config/examples/openfast_library_smoke/README.md#L1)

## First real implementation target

The first real cluster-side `libForcedOpenFAST.so` does not need to solve every possible OpenFAST coupling mode.
It only needs to do these well:

1. create one OpenFAST turbine instance from a manifest-backed case directory
2. accept imposed platform motion each coupling step
3. advance OpenFAST
4. return one tower-base/interface wrench
5. expose a few diagnostics for debugging

## Current scaffold behavior

Today’s scaffold runtime does these practical things:

1. parses `forced_openfast_case.toml`
2. validates that the referenced `openfast_input` file exists
3. for `backend = "stub"`, returns deterministic synthetic tower-base loads and
   diagnostics from imposed platform motion
4. for `backend = "openfast_library"`, validates the OF2-relevant OpenFAST
   library symbols:
   - `FAST_AllocateTurbines`
   - `FAST_DeallocateTurbines`
   - `FAST_ExtLoads_Init`
   - `FAST_CFD_Solution0`
   - `FAST_CFD_Prework`
   - `FAST_CFD_UpdateStates`
   - `FAST_CFD_AdvanceToNextTimeStep`
   - `FAST_CFD_WriteOutput`
   - `FAST_End`
   and drives the custom bridge symbols:
   - `FAST_OC4_Platform_SetMotion`
   - `FAST_OC4_Platform_GetReactionLoad`
   - optional `FAST_OC4_Platform_GetDiagnostics`

That makes it suitable for:

- adapter testing
- OpenFOAM-side restraint plumbing
- cluster build validation

It is not yet the final cluster-ready bridge implementation, but the runtime
path, symbol contract, and the correct OpenFAST patch seam are now in place.

That is enough to plug into the current OpenFOAM-side coupling stack.
