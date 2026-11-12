# OF2W test cases

Six self-contained cases, each with a `run.sh`; cases 01-05 end with a
`check.py` that prints PASS/FAIL (06 is a showcase — success is the run
completing). Prerequisite: OpenFOAM v2306 env sourced + OF2W built.

| Case | Verifies | Scale |
|---|---|---|
| `00_ci_smoke` | coupled stack + restart mechanics (CI-sized) | ~8 min, 4 cores |
| `01_minimal_coupling` | coupled stack + bit-faithful restart | minutes, 10 cores |
| `02_wave2d` | 2D regular wave | hours, 8 cores |
| `03_wave3d` | 3D regular wave | ~1 day, 48 cores |
| `04_wavecurrent2d` | wave + current, Doppler | hours, 16 cores |
| `05_pitch_decay` | free decay vs OpenFAST | ~1 day, 10 cores |
| `06_wave_turbine` | waves + running turbine | days, 48 cores |

```sh
cd 01_minimal_coupling && ./run.sh     # one case  (NP=<n> to override)
./run_all.sh                           # all, with summary
./run_all.sh 01 02 04                  # subset
```

Each case prints `PASS — ...` (exit 0) or `FAIL — ...` (exit 1); see the
case README for the expected numbers. On a cluster every case ships a
`slurm.example` with sensible core/memory/time defaults — adapt the
SBATCH + module lines to your cluster and `sbatch` it. All templates
source the repo's `env.sh`, which sets up OpenFOAM, the OF2W variables
and waves2Foam in one line.
