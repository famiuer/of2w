# Installation

Two ways to get a working OF2W. Both give the same code.

## Option A · Container (nothing to build)

A prebuilt image with the whole stack; needs only Apptainer, Singularity
or Docker, and no root access on a cluster.

```bash
apptainer pull of2w.sif docker://ghcr.io/famiuer/of2w:0.1.2
```

Everything else (running cases, Docker, Slurm, troubleshooting) is in
[`docs/container.md`](docs/container.md).

## Option B · Native build (one command)

Choose this if you will modify the code or need your cluster's own MPI
for multi-node runs.

**Prerequisites**

- OpenFOAM v2306 (ESI/OpenCFD)
- gcc, g++ and gfortran (versions 9 and 11 tested), CMake ≥ 3.16
- OpenMPI matching the one OpenFOAM was built with
- BLAS + LAPACK, GSL
- Python 3 with `numpy`, `scipy`, `matplotlib`

**Build and test**

```bash
git clone --recurse-submodules https://github.com/famiuer/of2w.git && cd of2w
./bootstrap.sh          # ~25 min; re-run after fixing a failure, finished steps are skipped
source env.sh           # in every shell; prints what is ready and what is missing
cd cases/01_minimal_coupling && ./run.sh      # must print PASS
```

On a cluster, load the OpenFOAM module first, then `source env.sh`. If
`env.sh` cannot find OpenFOAM, pass the path once:
`source env.sh /path/to/openfoam/etc/bashrc`.

`./bootstrap.sh --list` shows the status of each step; `--only STEP`
and `--force STEP` run or rebuild a single one.

## If something fails

[`docs/manual_build.md`](docs/manual_build.md) has every build step done
by hand, the pinned versions of the five submodules, HPC notes, and a
troubleshooting table.
