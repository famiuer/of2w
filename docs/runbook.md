# Run notes: gotchas & diagnostics

Build instructions live in [`INSTALL.md`](../INSTALL.md) (or just
`./bootstrap.sh`); the test battery in [`cases/`](../cases/README.md);
regenerating OpenFAST baselines (incl. where to fetch the OC4 WAMIT
data) in `cases/05_pitch_decay/README.md`. What remains here are the
hard-won runtime gotchas and diagnostics that apply to any OF2W case.

## Known gotchas

| Symptom | Cause | Fix |
|---|---|---|
| `surfaceMeshTriangulate: command not found` after `source bashrc` in non-interactive shell | OpenFOAM bashrc partially loaded | Source it directly in a login shell or via `source env.sh`; do **not** pipe `module load` |
| `Sub-cycling is not supported with the CrankNicolson ddt scheme` | `nAlphaSubCycles > 1` in fvSolution with `ddtSchemes { default CrankNicolson 0.9; }` | Keep `nAlphaSubCycles 1`, or switch ddt scheme to `Euler` |
| Body's `transform` translation seems not to move the body | OpenFOAM rigid-body `transform` sets the kinematic state, but `snappyHexMesh` carves the cavity at the literal STL world coords. To physically move the body, edit the STL or use `transformPoints`, not `transform` | See `docs/architecture.md` |
| MoorDyn IC convergence FPE | `icStationary` is broken in MoorDyn v2.6.1 | Set `ICgenDynamic 1` in the moor input file; ensure Point IDs are sequential |
| ED reports PtfmHeave = 0 at t=0 even though the `.fst` IC says +1 | In OF2W mode (CompHydro=0), the of2Restraint overrides ED's IC with the body's *relative-from-initial* world position. §setFields trick: to get a +1 m heave IC, lift the body via STL/`transformPoints`, or lower the water level via `setFieldsDict` (water z=−1 m gives a +1 m relative offset without re-meshing) | See `docs/coordinate_conventions.md` |

## Diagnostic: where does the hull actually sit at t=0?

Resolves the transform-vs-STL ambiguity (run from any meshed case with a
`float` patch):

```bash
source ../../env.sh
foamToVTK -case . -constant -ascii -fields '()' -patches '(float)'
python3 - <<'EOF'
import re, glob, numpy as np
vtp = sorted(glob.glob('VTK/*/boundary/float.vtp'))[0]
txt = open(vtp).read()
data = re.search(r'<Points>\s*<DataArray[^>]+>(.*?)</DataArray>', txt, re.S).group(1).split()
arr = np.array(list(map(float, data))).reshape(-1, 3)
print('float bbox:', *(f'{a}=[{arr[:,i].min():.3f},{arr[:,i].max():.3f}]'
                       for i, a in enumerate('xyz')))
EOF
```

The float patch z-extent tells you where the hull *actually* sits in
the world, independent of the `transform` translation.
