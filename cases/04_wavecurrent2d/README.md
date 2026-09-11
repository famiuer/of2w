# 04 — 2D combined wave + current

Stokes-II H=6 m, T=10 s (absolute) on a 1/7-power-law current
U_s=0.5 m/s. `constant/waveProperties` and `0/` ship PRE-GENERATED
(Doppler-compensated period 10.303 s + initial current profile) — run.sh
does not regenerate them. ~3-4 h on 16 cores.

```sh
./run.sh              # NP=<n> ./run.sh to change core count
```

Expected result:

```
[wave]    incident H ≈ 6.4 m (+7%), R ≈ 3.5%
[doppler] k = 0.0380  vs Kirby-Chen 0.03808 (±1%)  vs still-water (-6%)
[current] profile at x=0 (z<=-15): rms vs 1/7 law ≈ 5% of U_s
PASS — wave height, Doppler-shifted dispersion and current profile all within tolerance.
```

The near-surface (z > -15 m) mean flow deviates from the 1/7 law by design:
the wave-induced Eulerian return flow balances the Stokes drift there; the
checker reports it as information and judges the law below the wave zone.
