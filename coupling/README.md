# OF2W coupling core

The solver-agnostic layer between OpenFOAM, OpenFAST and MoorDyn-C.
Design rules: OpenFOAM-specific code stays thin; OpenFAST and MoorDyn
sit behind adapter APIs; core math and load transfer are unit-testable
without a cluster, an OpenFOAM install, or the real external libraries.

## How the production path works

OpenFOAM owns the platform: the two-phase hull hydrodynamics, the wave
field, the 6-DOF motion and (through foamMooring/MoorDyn) the mooring.
Each outer corrector of each CFD step, the `of2Restraint` plugin:

1. extracts the rigid-body state and converts it to the shared
   `PlatformState` (frames per `docs/coordinate_conventions.md`);
2. imposes it on OpenFAST's ElastoDyn through the OF2 C-ABI bridge
   (`OF2_SetImposedPlatformState` / PMI injection — custom symbols
   carried by the pinned `famiuer/openfast` fork, not stock
   actuator-node exchange);
3. receives one resultant tower-base reaction wrench
   (`OF2_GetTowerBaseReaction`: tower + RNA inertia, gravity, rotor
   aerodynamics) and returns it to the rigid-body solver, with Aitken
   dynamic under-relaxation for strong per-corrector coupling.

Coupled OpenFAST decks therefore run `CompHydro=0`, `CompMooring=0`
(the CFD owns both) and `ModCoupling=1`.

## Layout

- `common/`: shared state types, frame transforms, load shifting
- `moordyn/`: MoorDyn adapter (C API, runtime-loadable — one resultant
  global-frame wrench at the platform reference point)
- `openfast/`: OpenFAST adapter + the OF2 bridge client
- `restraints/`: load providers and the OpenFOAM-facing restraints
  (`restraints/openfoam/of2/` builds `libOF2.so`; see INSTALL.md §6)
- `config/examples/`: example coupling dictionary snippets

## Build & test

```bash
cmake -S coupling -B build/coupling -DOPENFAST_BUILD_DIR=$PWD/build/openfast
cmake --build build/coupling -j
cd build/coupling && ctest --output-on-failure   # 9 tests; run from inside
```

The tests exercise the adapters against in-process fakes (a stub
OpenFAST backend and a fake MoorDyn), so they pass on a plain Linux
toolchain with neither library installed — that is what makes them
usable as a quick build-sanity check everywhere.
