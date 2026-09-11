# 02 — 2D regular-wave tank

waves2Foam check: Stokes-II H=6 m, T=10 s, depth 80 m, 2D tank (294k
cells). Wave gauges via the `surfaceElevation` function object.

```sh
./run.sh              # NP=<n> ./run.sh to change core count
```

Expected result:

```
incident H = 5.899 m  (target 6.0, -1.7%)
wavenumber k = 0.04015  (theory 0.04037, -0.54%)
reflection R = 5.8%
PASS — regular wave generated within tolerance
```
