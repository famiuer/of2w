# 05 — pitch free decay (overset) vs OpenFAST

OC4 released from +5° pitch, still water, overset mesh (322k cells),
120 s. OpenFAST baseline (same release, potential flow) ships precomputed
in `openfast_baseline/minimal.outb`.

```sh
./run.sh              # NP=<n> ./run.sh to change core count
```

Expected result (CFD decays faster than the baseline — that is viscous
physics, not error): `check.py` extracts the pitch natural period from
the peak spacing and prints `PASS` when it lies within ±12% of the
OpenFAST baseline period and both records hold ≥2 peaks. Reference run:
CFD period ~7-8% above the baseline.

## Regenerating the OpenFAST baseline (optional)

`openfast_baseline/minimal.outb` ships precomputed; rebuild it only for
new DOFs/ICs. HydroDyn's `PotFile` points at the OC4-DeepCwind WAMIT
data (`marin_semi.{1,3,hst}`), which is **not** distributed here — fetch
it from the NREL r-test repository and drop it next to the deck:

- https://github.com/OpenFAST/r-test/tree/main/glue-codes/openfast/5MW_OC4Semi_WSt_WavesWN
  (`HydroData/marin_semi*`)

Then run `openfast minimal.fst` with the driver from
`build/openfast/glue-codes/openfast/` (keep the baseline constrained to
the same single DOF as the CFD — a free baseline shows phantom
cross-coupled tower-base moments).
