# Running OF2W from the container image

The container image is the whole OF2W stack already compiled: OpenFOAM
v2306, OpenFAST, MoorDyn, foamMooring, the stabilised turbulence models,
the coupling library, `libOF2.so`, waves2Foam and all wave solvers, plus
the test cases. You download one file and run; nothing is built on your
machine and no administrator rights are needed on a cluster.

Contents of this page:

1. [Which route should I use?](#1-which-route-should-i-use)
2. [The five terms you need](#2-the-five-terms-you-need)
3. [What is in the image](#3-what-is-in-the-image)
4. [Quick start with Apptainer (clusters, no root)](#4-quick-start-with-apptainer-clusters-no-root)
5. [Quick start with Docker (workstation)](#5-quick-start-with-docker-workstation)
6. [Running your own case](#6-running-your-own-case)
7. [How the image treats your host environment](#7-how-the-image-treats-your-host-environment)
8. [On a cluster with Slurm](#8-on-a-cluster-with-slurm)
9. [Building the image yourself](#9-building-the-image-yourself)
10. [What has been tested](#10-what-has-been-tested)
11. [Troubleshooting](#11-troubleshooting)
12. [Further reading](#12-further-reading)

---

## 1. Which route should I use?

| Route | You need | Time to first run | Choose it when |
|---|---|---|---|
| **Container** (this page) | Apptainer, Singularity or Docker | minutes (one download) | you want to run OF2W, on a cluster or a laptop, without building anything |
| **`./bootstrap.sh`** ([INSTALL.md](../INSTALL.md)) | OpenFOAM v2306 + compilers already installed | ~25 min build | you will modify the code, or need the cluster's own MPI for multi-node jobs |
| **Manual build** ([manual_build.md](manual_build.md)) | as above | longer | a bootstrap step fails, or your system is unusual |

The container and the native build run exactly the same code: the image
is produced by running `bootstrap.sh` inside a clean Ubuntu 22.04.

## 2. The five terms you need

| Term | Meaning here |
|---|---|
| **Image** | A read-only snapshot of a complete Linux system with OF2W installed. |
| **Container** | A running instance of an image. It shares your machine's kernel and CPUs, so there is no virtual-machine overhead. |
| **Docker** | The usual container tool on workstations. It needs administrator rights to install, which is why clusters rarely offer it. |
| **Apptainer / Singularity** | The container tool for HPC. It runs as your normal user, without root, and sees your files. Apptainer and SingularityCE are two descendants of the same project and use the same commands and file format; where this page says `apptainer`, `singularity` works the same way. |
| **`.sif` file** | Apptainer's image format: the whole image as one ordinary file (about 1.4 GB for OF2W) that you can copy with `scp` like any other file. |

The image is published on the GitHub Container Registry (GHCR) as
`ghcr.io/famiuer/of2w`. Apptainer can download a Docker-format image
from there and convert it to a `.sif` in one command.

## 3. What is in the image

| Location in the image | Contents |
|---|---|
| `/usr/lib/openfoam/openfoam2306` | OpenFOAM v2306 (ESI package), with the OF2W libraries and solvers installed into its system `lib` and `bin` directories |
| `/opt/of2w` | this repository at the release tag, built in place |
| `/opt/of2w/env.sh` | sets up the whole environment; source it first in every session |
| `/opt/of2w/cases` | the test cases (`00_ci_smoke` … `06_wave_turbine`) |

Base system: Ubuntu 22.04, GCC/gfortran 11, OpenMPI 4.1.

## 4. Quick start with Apptainer (clusters, no root)

### Step 1 — check that Apptainer or Singularity is available

```sh
which apptainer singularity
```

Many clusters provide one of them without a module. If neither exists,
ask your cluster support, or see the
[Apptainer installation guide](https://apptainer.org/docs/admin/latest/installation.html)
for your own machine.

### Step 2 — get the image as a `.sif` file

Either download and convert it from the registry:

```sh
apptainer pull of2w.sif docker://ghcr.io/famiuer/of2w:0.1.2
```

or download the ready-made `of2w-<version>.sif` attached to the
[Zenodo record](https://doi.org/10.5281/zenodo.22744317) and copy it to
the cluster with `scp`. The second way is useful when compute or login
nodes have no internet access.

> On a cluster, put the `.sif` on the scratch or project file system,
> not in a small home quota. `apptainer pull` also needs a few GB of
> temporary space; if it fails for lack of space, set
> `APPTAINER_TMPDIR` and `APPTAINER_CACHEDIR` to a scratch directory.

### Step 3 — run the smoke test

```sh
apptainer exec of2w.sif bash -c '
    source /opt/of2w/env.sh &&
    cp -r /opt/of2w/cases/00_ci_smoke . &&
    cd 00_ci_smoke &&
    NP=4 ./run.sh'
```

The image is read-only, so the case is first copied into your current
directory and run there. Expected output, after about 9 minutes on 4
cores:

```
OF2W env ready — all checks passed.
== mesh (NP=4)
== runA: 0 -> 0.08 s
== runB: 0 -> 0.04 s, stop
== runB: restart 0.04 -> 0.08 s
== check
...
PASS — coupled stack ran, wrote a restart at t=0.04, restarted, and finished at t=0.08 ...
```

`PASS` means OpenFOAM, OpenFAST and MoorDyn ran coupled, wrote a restart
state, and restarted from it. See [`cases/README.md`](../cases/README.md)
for the other cases.

### Step 4 — work interactively (optional)

```sh
apptainer shell of2w.sif
Apptainer> source /opt/of2w/env.sh
Apptainer> overInterDyMFoam -help
```

## 5. Quick start with Docker (workstation)

Install Docker first if needed
([Docker Engine](https://docs.docker.com/engine/install/) on Linux,
[Docker Desktop](https://docs.docker.com/desktop/) on Windows/macOS).

```sh
docker pull ghcr.io/famiuer/of2w:0.1.2

docker run --rm \
    -e OMPI_ALLOW_RUN_AS_ROOT=1 -e OMPI_ALLOW_RUN_AS_ROOT_CONFIRM=1 \
    ghcr.io/famiuer/of2w:0.1.2 bash -c '
        source /opt/of2w/env.sh &&
        cd /opt/of2w/cases/00_ci_smoke &&
        NP=4 ./run.sh'
```

Two differences from Apptainer:

- Docker runs as `root` inside the container, and OpenMPI refuses to
  start as root unless the two `OMPI_ALLOW_RUN_AS_ROOT…` variables are
  set. They are safe here because the container is isolated.
- The container's files disappear when it exits (`--rm`). To keep
  results, mount a host directory, as shown in the next section.

## 6. Running your own case

Keep your case on the host file system and run the image's solvers on it.

**Apptainer.** Your home directory, the current directory and `/tmp` are
visible inside the container automatically. Other locations (for
example a scratch file system) must be named with `--bind`:

```sh
cd /scratch/$USER/my_case
apptainer exec --bind /scratch/$USER of2w.sif bash -c '
    source /opt/of2w/env.sh &&
    ./run.sh'
```

**Docker.** Mount the case directory explicitly and work in it:

```sh
docker run --rm -v "$PWD":/work -w /work \
    -e OMPI_ALLOW_RUN_AS_ROOT=1 -e OMPI_ALLOW_RUN_AS_ROOT_CONFIRM=1 \
    ghcr.io/famiuer/of2w:0.1.2 bash -c 'source /opt/of2w/env.sh && ./run.sh'
```

Files written this way are owned by `root` on a Linux host.

A good starting point for a new case is a copy of one of the shipped
cases: `cp -r /opt/of2w/cases/01_minimal_coupling .` from inside the
container.

## 7. How the image treats your host environment

Apptainer deliberately lets the container see your home directory and
your shell's environment variables. Two things follow, and `env.sh`
handles both:

- **OpenFOAM already loaded on the host.** If your shell (or a loaded
  module) has set OpenFOAM variables, they are passed into the
  container. `env.sh` detects this and loads the image's OpenFOAM again,
  so the result is the same as from a clean shell.
- **Your own OpenFOAM add-ons in `~/OpenFOAM/<user>-v2306`.** OpenFOAM
  normally searches that directory before the system one. Inside the
  image, `env.sh` removes it from the search paths, so the image's own
  libraries and solvers are always the ones used. The readiness report
  confirms it with the line
  `container image: using the built-in stack (~/OpenFOAM user builds ignored)`.

If you want full isolation anyway, add `--cleanenv --no-home` to
`apptainer exec` and `--bind` the directory you work in. See the
Apptainer pages on
[environment](https://apptainer.org/docs/user/latest/environment_and_metadata.html)
and [bind paths](https://apptainer.org/docs/user/latest/bind_paths_and_mounts.html).

## 8. On a cluster with Slurm

A single-node job runs MPI entirely inside the container. Template
(adapt the `#SBATCH` lines and paths to your cluster):

```sh
#!/bin/bash
#SBATCH --job-name=of2w
#SBATCH --nodes=1
#SBATCH --ntasks=16
#SBATCH --time=02:00:00
#SBATCH --output=/scratch/%u/my_case/slurm-%j.out

cd /scratch/$USER/my_case
apptainer exec --bind /scratch/$USER /scratch/$USER/of2w.sif bash -c '
    source /opt/of2w/env.sh &&
    NP=16 ./run.sh'
```

Keep `NP` equal to the number of tasks requested and to the
decomposition of the case.

**Multi-node jobs are not supported by the image yet.** Spreading one
simulation over several nodes requires the cluster's MPI to launch the
container, and the host MPI must be compatible with the OpenMPI inside
the image (the "hybrid model" in the
[Apptainer MPI guide](https://apptainer.org/docs/user/latest/mpi.html)).
This has not been tested for OF2W. For multi-node production runs, use
the native build ([INSTALL.md](../INSTALL.md)), which links against your
cluster's MPI.

## 9. Building the image yourself

You only need this to change the recipe or to build from a development
branch. From the repository root:

```sh
docker build -t of2w:local \
    --build-arg OF2W_REF=main \
    --build-arg OF2W_SHA=$(git rev-parse --short HEAD) \
    -f Containerfile .
```

| Build argument | Meaning |
|---|---|
| `OF2W_REF` | branch or tag of this repository to clone into the image (default `main`; use the release tag for a release) |
| `OF2W_SHA` | the commit you expect. It is printed in the build log, and changing it forces Docker to clone again instead of reusing a cached, older clone |

How the recipe works:

- It installs OpenFOAM v2306 from the ESI package repository plus the
  compilers, clones this repository with its submodules, and runs
  `./bootstrap.sh --only <step>` once per component.
- Each component is therefore its own cached layer: if a step fails,
  fixing it and rebuilding re-runs only that step and those after it.
- The recipe contains no build logic of its own. If something is
  missing, the fix belongs in `bootstrap.sh`, so that native and
  container builds cannot drift apart.

A full build takes about 25 minutes on a 20-core machine and needs
roughly 20 GB of free disk. To convert a locally built image to a
`.sif` without publishing it:

```sh
apptainer build of2w.sif docker-daemon://of2w:local
```

## 10. What has been tested

All runs are case `00_ci_smoke` on 4 cores unless noted. Every run gave
the same check values to five digits.

| Host system | Runtime | Result | Wall time |
|---|---|---|---|
| Ubuntu 20.04 (WSL2 desktop) | Docker 28.1 | PASS | 9 min |
| Ubuntu 20.04 (WSL2 desktop), OpenFOAM also installed natively | Apptainer 1.4.5 | PASS | 9 min |
| AlmaLinux 9 (WSL2), no OpenFOAM, fresh user | Apptainer (EPEL) | PASS | — |
| Rocky Linux 8.10 cluster, Slurm job (template of Section 8) | SingularityCE 4.5.1 | PASS | 14-20 min |
| GitHub-hosted runner (Ubuntu 22.04) | Docker | PASS | 19 min |
| Desktop and the same cluster, case `01_minimal_coupling`, 10 cores | Apptainer / SingularityCE | PASS | 104 / 157 min |

Sizes: about 3.7 GB as a Docker image, 0.75 GB as a `.sif`.

## 11. Troubleshooting

| Symptom | Cause | Fix |
|---|---|---|
| `[FAIL] OpenFOAM env not loaded` inside the container | image older than v0.1.2 combined with OpenFOAM variables from the host | use a current image, or add `--cleanenv` |
| A solver or library resolves to a path under your home directory | image older than v0.1.2 | use a current image, or add `--cleanenv --no-home` |
| `Read-only file system` when running a case | the case is being run inside `/opt/of2w` under Apptainer | copy the case to a host directory first (Section 4, step 3) |
| `mpirun has detected an attempt to run as root` | Docker runs as root | add the two `OMPI_ALLOW_RUN_AS_ROOT…` variables (Section 5) |
| `mkdir ... /local: Read-only file system` followed by `orte_init failed` | image older than v0.1.2: the batch system's `TMPDIR` does not exist inside the container | use a current image, or `export TMPDIR=/tmp` inside the container before running |
| `There are not enough slots available` | more ranks requested than cores visible | lower `NP`, or request more tasks in the job |
| Case directory not found inside the container | the path is outside your home and current directory | add `--bind /that/path` |
| `apptainer pull` fails with "no space left on device" | temporary or cache directory too small | set `APPTAINER_TMPDIR` and `APPTAINER_CACHEDIR` to scratch |
| Matplotlib warns that its config directory is not writable | home not mounted (`--no-home`) | harmless; the plot is still written |

## 12. Further reading

- Apptainer: [quick start](https://apptainer.org/docs/user/latest/quick_start.html),
  [user guide](https://apptainer.org/docs/user/latest/),
  [bind paths and mounts](https://apptainer.org/docs/user/latest/bind_paths_and_mounts.html),
  [MPI applications](https://apptainer.org/docs/user/latest/mpi.html)
- SingularityCE: [user guide](https://docs.sylabs.io/guides/latest/user-guide/)
- Docker: [get started](https://docs.docker.com/get-started/),
  [`docker run` reference](https://docs.docker.com/reference/cli/docker/container/run/)
- GitHub Container Registry:
  [working with the registry](https://docs.github.com/en/packages/working-with-a-github-packages-registry/working-with-the-container-registry)
- How releases and images are produced: [RELEASING.md](RELEASING.md)
