# 01 — minimal coupled run (+ restart)

> CI runs the shrunk sibling `00_ci_smoke` (restart *mechanics* only);
> this case is the human-run one — its fine mesh is deterministic, so it
> verifies *bit-faithful* restart (rms < 1% overlay).

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
