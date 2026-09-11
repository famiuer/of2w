# of2W — OpenFOAM ↔ OpenFAST coupling for floating offshore wind turbines

**of2W** couples two-phase VOF CFD to an aero-servo-elastic wind turbine
model, replacing the linear potential-flow hydrodynamics (WAMIT/HydroDyn)
with first-principles CFD, and adding wave generation on the CFD side.

OpenFOAM is the master: it resolves the hull hydrodynamics and integrates
the platform 6-DOF motion together with the mooring (MoorDyn). OpenFAST is
the slave: every CFD step the platform state is imposed on ElastoDyn, and
the tower-base reaction wrench (tower + RNA inertia, gravity and rotor
aerodynamics) is applied back on the CFD body.

## Capabilities

- **Platform ↔ turbine coupling** — strong (per-corrector) coupling with
  snapshot/rollback and Aitken Δ² wrench relaxation (`libOF2.so`)
- **Mooring** — MoorDyn via foamMooring, on the CFD side
- **Waves** — waves2Foam generation/absorption compiled into an overset
  6-DOF solver (`overWaveDyMFoam`); regular waves and wave–current
  (uniform or power-law sheared current)
- **Aerodynamics** — AeroDyn + InflowWind + ServoDyn through OpenFAST
- **Validation** — OC4-DeepCwind semi-submersible free decay and load
  cases, benchmarked against standalone OpenFAST/HydroDyn baselines

## Layout

| Path | Contents |
|---|---|
| `coupling/` | solver-agnostic core + OpenFOAM restraint plugins (`of2Restraint`, mooring), unit tests |
| `solvers/overWaveDyMFoam/` | overset VOF 6-DOF solver with waves2Foam relaxation zones |
| `cases/` | test battery: minimal coupled+restart, wave 2D/3D, wave+current, pitch decay vs OpenFAST, waves+turbine showcase |
| `external/` | pinned submodules: OpenFAST, waves2Foam, MoorDyn, foamMooring |
| `tools/`, `docs/` | baseline comparison scripts, architecture and interface notes |

## Quick start

```bash
git clone --recurse-submodules https://github.com/famiuer/of2.git
cd of2
# build OpenFAST, foamMooring, waves2Foam and libOF2.so against OpenFOAM v2306:
# see INSTALL.md
```

Run the test cases: `cases/` contains six self-contained cases — from a
minutes-scale minimal coupled run (with restart) through wave/wave-current
tank validation and an OC4 pitch free decay vs an OpenFAST baseline, to
the full waves+turbine showcase. Each has a `run.sh` and (01-05) a
`check.py` with documented expected results; see `cases/README.md`.

Notes for coupled OpenFAST decks: `CompHydro=0` and `CompMooring=0` (the CFD
owns both), and **`ModCoupling=1`** — the platform-state injection requires
the loose-coupling solver path.

## Status

Single-DOF free decay (surge, sway, heave, roll, pitch, yaw) validated
against OpenFAST baselines; wave-only and wave–current tanks verified;
aero-coupled load cases (steady wind, still water) running and stable.
Ongoing: full wave + wind operating conditions and the 3D wave–turbine case.

## License

GPL-3.0-or-later (required by linking against OpenFOAM). See
[`LICENSE`](LICENSE).

## Citation

Based on the OF² method: *Wind Energ. Sci. 8, 1597–1616 (2023)*.
