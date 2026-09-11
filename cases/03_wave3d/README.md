# 03 — 3D regular-wave basin

Same Stokes-II H=6 m, T=10 s wave as 02 in a 160 m wide 3D basin
(4.7M cells). ~1 day on 48 cores.

```sh
./run.sh              # NP=<n> ./run.sh to change core count
SMOKE=1 ./run.sh      # 8 ranks, 60 s generation check only
```

Expected result: same tolerances as 02 —

```
PASS — regular wave generated within tolerance (|ΔH|<6%, |Δk|<2%, R<10%)
```
