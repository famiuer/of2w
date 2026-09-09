# OpenFAST Smoke Case Layout

This directory defines the **stub backend layout** for the local
`libForcedOpenFAST.so` smoke tests used by the OC4 OpenFOAM coupling.

The repo now also ships a **stub runtime scaffold** that builds as
`libforcedopenfast.so` and uses this layout for local testing. That scaffold is
not a real OpenFAST solver backend yet, but it is useful for verifying:

- manifest parsing
- runtime library loading through `dlopen`
- imposed-motion mapping
- one returned tower-base wrench
- diagnostic plumbing

The wrapper entry point currently receives only:

- `casePath`

The intended meaning of `casePath` is:

- path to a directory containing `forced_openfast_case.toml`
- plus the OpenFAST input files referenced by that manifest

## Expected layout

```text
openfast_smoke/
  forced_openfast_case.toml
  openfast/
    Main.fst
    ElastoDyn.dat
    AeroDyn.dat
    ServoDyn.dat
    InflowWind.dat
    ExtPtfm.dat
    optional other module files...
  outputs/
```

The placeholder files under `openfast/` are intentional. The current stub
backend validates that the referenced `Main.fst` exists, while the future real
backend will replace this directory with a genuine OpenFAST input set.

## Purpose of the smoke case

This is not the final production turbine setup. The first wrapper-ready case
should only prove:

- the wrapper can create and initialize an OpenFAST instance
- imposed platform motion reaches the turbine-side model
- one tower-base/interface wrench can be returned
- basic diagnostics such as rotor speed and generator power are accessible

## Required wrapper behavior

The wrapper must interpret the manifest and expose a single platform-coupling
interface with:

- imposed platform displacement `[x, y, z, roll, pitch, yaw]`
- imposed platform velocity `[vx, vy, vz, omega_x, omega_y, omega_z]`
- imposed platform acceleration `[ax, ay, az, alpha_x, alpha_y, alpha_z]`
- returned tower-base/interface reaction load

## Recommended OpenFAST-side settings

For the CFD-coupled production path:

- OpenFAST should own aero-servo-elastic turbine physics
- OpenFAST should **not** own platform hydrodynamics
- OpenFAST-side MoorDyn should be off in the production coupled path
- platform motion should come from the external/OpenFOAM side

In practice this means the first smoke case should be built around:

- an imposed platform-motion path
- a tower-base or equivalent structural interface load extraction path
- no duplicate mooring ownership inside OpenFAST

## Notes on reference points

The wrapper manifest distinguishes:

- `platform_reference_point`
- `tower_base_point`
- `openfoam_to_openfast_offset`

The points in the manifest are expressed in the wrapper/OpenFAST frame, not the
OpenFOAM frame. With the current example offset:

- OpenFOAM still water level is `z = 15`
- wrapper/OpenFAST still water level is `z = 0`
- an OpenFOAM tower-base point at `z = 10` becomes `z = -5` in the manifest

The offset exists for the same reason as the MoorDyn path:

- OpenFOAM cases in this repo use still water at `z = 15`
- the turbine-side wrapper may use a still-water or MSL convention centered at `z = 0`

The wrapper should apply the offset internally and always return the final
reaction load in the OpenFOAM/global coupling frame.

For the production-target backend, see:

- [openfast_library_smoke/README.md](coupling/config/examples/openfast_library_smoke/README.md#L1)
