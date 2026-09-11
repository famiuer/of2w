# OF2W test cases

Six self-contained cases, each with a `run.sh`; cases 01-05 end with a
`check.py` that prints PASS/FAIL (06 is a showcase — success is the run
completing). Prerequisite: OpenFOAM v2306 env sourced + OF2W built.

| Case | Verifies | Scale |
|---|---|---|
| `01_minimal_coupling` | coupled stack + restart | minutes, 10 cores |
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
case README for the expected numbers. On a cluster wrap either form in
your own batch script (template: `06_wave_turbine/slurm.example`).
