# of2W — OpenFOAM ↔ OpenFAST coupling for floating offshore wind turbines

**of2W** couples two-phase VOF CFD to an aero-servo-elastic wind-turbine
model: OpenFOAM is the master — it resolves the hull hydrodynamics, wave
field and the platform 6-DOF motion together with the mooring (MoorDyn) —
and OpenFAST is driven: each CFD step the platform state is imposed on
ElastoDyn and the tower-base reaction wrench (tower + RNA inertia,
gravity, rotor aerodynamics) is applied back on the CFD body, with
strong per-corrector coupling (snapshot/rollback + Aitken relaxation).

## Layout

| Path | Contents |
|---|---|
| `coupling/` | solver-agnostic core + OpenFOAM plugins (`of2Restraint`, mooring), unit tests |
| `solvers/overWaveDyMFoam/` | overset VOF 6-DOF solver with waves2Foam relaxation zones |
| `cases/` | test battery: minimal coupled+restart, wave 2D/3D, wave+current, pitch decay vs OpenFAST, waves+turbine showcase |
| `external/` | pinned submodules: OpenFAST, waves2Foam, MoorDyn, foamMooring, stabRAS |
| `tools/`, `docs/` | comparison scripts, architecture and interface notes |

## Quick start

```bash
git clone --recurse-submodules https://github.com/famiuer/of2w.git
cd of2w                  # build against OpenFOAM v2306: see INSTALL.md
cd cases/01_minimal_coupling && ./run.sh   # minutes-scale coupled + restart check
```

The six cases in `cases/` go from that minimal check through wave and
wave–current tank validation and an OC4 pitch free decay vs an OpenFAST
baseline to the full waves + running-turbine showcase; see
`cases/README.md` for expected results.

Coupled OpenFAST decks use `CompHydro=0`, `CompMooring=0` (the CFD owns
both) and `ModCoupling=1` (the platform-state injection requires the
loose-coupling solver path).

## Status

OC4-DeepCwind free decay (all six DOFs) validated against OpenFAST
baselines; wave-only and wave–current tanks verified; coupled
wave + turbine load cases running and stable.

## License

GPL-3.0-or-later (required by linking against OpenFOAM). See
[`LICENSE`](LICENSE).

## Citation

Based on the OF² method: *Wind Energ. Sci. 8, 1597–1616 (2023)*.
