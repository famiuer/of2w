# 06 — regular waves + running turbine (showcase)

The full OF2W production wave case: OC4 semi-submersible in Stokes-II
H=6 m, T=10 s waves, overset mesh (5.19M cells), NREL 5-MW turbine
running (AeroDyn + ServoDyn) + MoorDyn. Two legs: waves develop from
rest with the body held (0-80 s), then the coupled release.

```sh
sbatch slurm.example        # adapt SBATCH lines + env block first
```

Default is 60 s of coupled motion after the release (`MOVING=60`;
`NP=<n>` sets the rank count).

This is a showcase (no check script): success is the run completing, with
wave gauges, water-volume/flux monitors, platform motion (OpenFAST
`derisk.out`) and mooring tensions (`Mooring/lines_oc4.out`) written.
