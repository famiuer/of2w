# 00 — CI smoke (coupled stack + restart mechanics, minutes-scale)

The CI-sized sibling of `01_minimal_coupling`: same physics setup (OC4
hull released from a +5 m surge offset, overset mesh, OF2-coupled
OpenFAST + MoorDyn), shrunk to ~64k cells, 0.08 s at a FIXED
`deltaT 0.005`, splice at 0.04 s. Runs in ~8 min on 4 cores.

```sh
./run.sh              # NP=<n> ./run.sh; CI uses NP=4 (free-runner vCPUs)
```

**What PASS means here**: the full coupled stack meshes, runs, writes a
complete restart state (RBD + coupling + MoorDyn + FAST checkpoint) at
the splice, restarts from it, and finishes — with finite, physically
bounded output on channels from all three subsystems. That is the
regression class CI exists to catch (build/ABI breakage, missing state
files, crash-on-restart).

**What PASS does NOT mean**: bit-faithful restart (runB overlaying runA
to rms < 1%). This mesh is deliberately too coarse for that check: the
velocity-limited overset fringe makes the solution chaotic, so two runs
of *identical* inputs already diverge before the splice (MPI summation
order seeds ~1e-16 differences that grow exponentially). The overlay
rms is still printed and plotted (`bench_overlay.png`) as a diagnostic.
Bit-fidelity is verified by `01_minimal_coupling`, whose finer mesh is
deterministic — run that one before trusting restart physics.

Differences from case 01, all deliberate:
- base meshes halved per direction (~64k vs 323k cells);
- `fvOptions limitVelocity 10 m/s` (clips spurious coarse-fringe
  spikes; physical velocities are cm/s);
- fixed time step (`adjustTimeStep no`) so both legs step the same grid;
- `writeInterval` aligned to the splice time.
