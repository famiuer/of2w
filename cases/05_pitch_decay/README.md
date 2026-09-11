# 05 — pitch free decay (overset) vs OpenFAST

OC4 released from +5° pitch, still water, overset mesh (322k cells),
120 s. OpenFAST baseline (same release, potential flow) ships precomputed
in `openfast_baseline/minimal.outb`.

```sh
./run.sh              # NP=<n> ./run.sh to change core count
```

Expected result (CFD decays faster than the baseline — that is viscous
physics, not error):

