# OC4 Coupling Core

This subtree contains the solver-agnostic coupling code that will sit between:

- OpenFOAM
- MoorDyn-C
- OpenFAST

The design intent is:

- keep OpenFOAM-specific code thin
- keep MoorDyn and OpenFAST integration behind wrapper APIs
- make core math and load-transfer logic testable without a cluster or OpenFOAM install

## Layout

- `common/`: shared state types, frame transforms, load shifting, formatting
- `moordyn/`: MoorDyn wrapper interfaces and concrete C-API adapter
- `openfast/`: OpenFAST wrapper interfaces and future implementation
- `restraints/`: load-provider and future OpenFOAM-facing restraint layers
- `config/examples/`: example coupling configuration files

The OpenFAST subtree now also includes a buildable shared-library scaffold:

- `forcedopenfast_runtime` -> `libforcedopenfast.so`

This now covers two roles:

- a `stub` backend for local and CI-style coupling tests
- an `openfast_library` backend path aligned with the OF2/WES architecture

The important design decision is that the production path is **not** based on
stock actuator-node `openfast-cpp` exchange. It targets a custom
OpenFAST-library wrapper that takes imposed platform motion and returns one
platform/interface wrench.

That wrapper path is now scaffolded through the runtime:

- stock `FAST_ExtLoads_Init` and `FAST_CFD_*` lifecycle calls
- custom OF2-style bridge symbols for platform motion and returned wrench
- local fake-library coverage for end-to-end tests
- a cluster-target OpenFAST bridge scaffold under
  [openfast/bridge](coupling/openfast/bridge#L1) that
  is designed to patch into `FAST_Library.f90`

## Build

```bash
cmake -S coupling -B build/coupling
cmake --build build/coupling
ctest --test-dir build/coupling --output-on-failure
```

The current code is intentionally lightweight and should build with a standard Linux cluster toolchain.

## Current MoorDyn scope

The first concrete adapter targets the version-1 OC4 coupling path described in the WES reference workflow:

- one coupled 6-DOF platform body
- OpenFOAM-owned platform motion
- MoorDyn returning one resultant global-frame wrench at the platform reference point
- optional external wave-kinematics callback for future OpenFOAM-to-MoorDyn wave transfer

The adapter is runtime-loadable, so the coupling code can be compiled and unit-tested even when the MoorDyn shared library is not installed locally.

## Current restraint-side scope

The first restraint-side implementation is a `MoorDynLoadProvider` layer:

- initializes and owns the `MoorDynAdapter`
- evaluates mooring loads from a `PlatformState`
- shifts the returned wrench onto the active platform reference point
- optionally writes a CSV debug trace for time-step-by-time-step load auditing

This is intentionally one step below a real OpenFOAM restraint so we can test the load path locally before binding it to `sixDoFRigidBodyMotion`.

The same pattern is now started for OpenFAST:

- `OpenFASTAdapter` remains the only component allowed to talk to the turbine-side runtime
- `OpenFASTLoadProvider` turns adapter results into one OpenFOAM-ready interface wrench
- the intended returned load is a **single resultant reaction force/moment at a defined interface point**, not raw distributed AeroDyn loads

## OpenFOAM shim

The actual OpenFOAM-facing restraint scaffold is now in:

- [coupling/restraints/openfoam/moorDynRestraint.H](coupling/restraints/openfoam/moorDynRestraint.H#L1)
- [coupling/restraints/openfoam/moorDynRestraint.C](coupling/restraints/openfoam/moorDynRestraint.C#L1)
- [coupling/restraints/openfoam/Make/files](coupling/restraints/openfoam/Make/files#L1)
- [coupling/restraints/openfoam/Make/options](coupling/restraints/openfoam/Make/options#L1)

The OpenFOAM shim is intentionally thin:

- it reads restraint-level dictionary settings
- it extracts the current `sixDoFRigidBodyMotion` state
- it converts that state into the shared `PlatformState`
- it delegates load evaluation to the tested bridge/provider/adapter stack
- it returns one global-frame wrench to the rigid-body solver

An example dictionary snippet is included in:

- [coupling/config/examples/dynamicMeshDict.moorDynRestraint](coupling/config/examples/dynamicMeshDict.moorDynRestraint#L1)
