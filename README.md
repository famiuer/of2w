# OF2W

OpenFOAM and OpenFAST coupling for floating offshore wind turbines.
OpenFOAM is the master: it resolves the two-phase hull hydrodynamics,
the wave field and the platform 6-DOF motion together with the mooring
(MoorDyn). OpenFAST is driven: each CFD step the platform state is
imposed on ElastoDyn and the tower-base reaction (tower and RNA inertia,
gravity, rotor aerodynamics) is applied back on the CFD body, with
strong per-corrector coupling.

## Layout

| Path | Contents |
|---|---|
| `coupling/` | core library + OpenFOAM plugins (`of2Restraint`, mooring), unit tests |
| `solvers/overWaveDyMFoam/` | overset VOF 6-DOF solver with waves2Foam relaxation zones |
| `cases/` | test battery, from a minimal coupled run to a waves+turbine showcase |
| `external/` | pinned submodules: OpenFAST, waves2Foam, MoorDyn, foamMooring, stabRAS |
| `docs/` | architecture and interface notes |

## Quick start

```bash
git clone --recurse-submodules https://github.com/famiuer/of2w.git
cd of2w                  # build against OpenFOAM v2306: see INSTALL.md
cd cases/01_minimal_coupling && ./run.sh   # minutes-scale coupled + restart check
```

See `cases/README.md` for the six test cases and their expected results.
Coupled OpenFAST decks use `CompHydro=0`, `CompMooring=0` (the CFD owns
both) and `ModCoupling=1`.

## Validation

OC4-DeepCwind free decay in all six DOFs against OpenFAST baselines;
wave-only and wave-current tanks; coupled wave + turbine load cases.

## License

GPL-3.0-or-later (required by linking against OpenFOAM). See
[`LICENSE`](LICENSE).

## Citation

Based on the OF2 method: Wind Energ. Sci. 8, 1597-1616 (2023).
