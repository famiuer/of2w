# 01 — minimal coupled run (+ restart)

Minimal full OF2W test: OC4 platform (overset, 323k cells) + NREL 5-MW
turbine (AeroDyn/ServoDyn) + MoorDyn, 1 s simulation on 10 cores.
Also tests restart: reference run vs stop-at-0.5 s/restart twin.

```sh
./run.sh              # NP=<n> ./run.sh to change core count
```

Expected result:

```
PASS — stop/restart run overlays the uninterrupted reference on all 8 channels
```

plus `bench_overlay.png` (the two runs indistinguishable).
