# OpenFAST Library Smoke Layout

This example is the **production-target layout** for the `openfast_library`
backend of `libforcedopenfast`.

It follows the OF2/WES ownership split used in this repo:

- OpenFOAM owns platform motion
- the wrapper imposes that motion into OpenFAST
- OpenFAST returns one platform/interface wrench
- MoorDyn remains separate on the OpenFOAM side

## Expected layout

```text
openfast_library_smoke/
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

## Important note

The current runtime now drives an OF2-style bridge skeleton on top of the
OpenFAST-library lifecycle, but the real cluster-side library still needs to
provide these custom bridge hooks:

- `FAST_OC4_Platform_SetMotion`
- `FAST_OC4_Platform_GetReactionLoad`
- optional `FAST_OC4_Platform_GetDiagnostics`

The local test suite uses a fake library that exports those symbols so the
wrapper path can be exercised end to end.

For the real cluster build, the recommended path is to compile those custom
symbols into the same `openfastlib` shared library that already exports the
stock `FAST_*` routines. The bridge scaffold for that patch-in path is under:

- [coupling/openfast/bridge/README.md](coupling/openfast/bridge/README.md#L1)

## Why this backend

The stock `openfast-cpp` API is oriented around actuator-node exchange.
For the floating-platform coupling in this project, the WES article points us
instead toward a custom OpenFAST-library wrapper that exchanges:

- imposed platform kinematics in
- one returned tower-base/platform-interface wrench out
