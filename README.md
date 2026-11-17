# OF2W

[![container-ci](https://github.com/famiuer/of2w/actions/workflows/container-ci.yml/badge.svg)](https://github.com/famiuer/of2w/actions/workflows/container-ci.yml)
[![core-ci](https://github.com/famiuer/of2w/actions/workflows/core-ci.yml/badge.svg?branch=main)](https://github.com/famiuer/of2w/actions/workflows/core-ci.yml)
[![DOI](https://zenodo.org/badge/DOI/10.5281/zenodo.22744317.svg)](https://doi.org/10.5281/zenodo.22744317)

OpenFOAM and OpenFAST coupling for floating offshore wind turbines.
OpenFOAM resolves the hull hydrodynamics, waves, platform motion and
mooring (MoorDyn); OpenFAST supplies the tower, rotor and controller
loads at every CFD step.

## Getting started

Two routes to the same code. Each test case prints `PASS` or `FAIL`.

### Container: nothing to build

You need Docker, or Apptainer/Singularity on a cluster (no root), and
to know how to run an OpenFOAM case. No compilers, no OpenFOAM install.

```bash
# cluster
apptainer pull of2w.sif docker://ghcr.io/famiuer/of2w:0.1.2
apptainer exec of2w.sif bash -c 'source /opt/of2w/env.sh &&
    cp -r /opt/of2w/cases/00_ci_smoke . && cd 00_ci_smoke && NP=4 ./run.sh'

# workstation
docker run --rm -e OMPI_ALLOW_RUN_AS_ROOT=1 -e OMPI_ALLOW_RUN_AS_ROOT_CONFIRM=1 \
    ghcr.io/famiuer/of2w:0.1.2 bash -c 'source /opt/of2w/env.sh &&
    cd /opt/of2w/cases/00_ci_smoke && NP=4 ./run.sh'
```

Details: [container guide](docs/container.md).

### Native build: to modify the code or run on several nodes

You need OpenFOAM v2306 already installed, gcc/gfortran, CMake, MPI,
BLAS/LAPACK and GSL, and to be comfortable compiling on Linux.

```bash
git clone --recurse-submodules https://github.com/famiuer/of2w.git && cd of2w
./bootstrap.sh && source env.sh
cd cases/01_minimal_coupling && ./run.sh
```

Details: [installation](INSTALL.md).

## Layout

| Path | Contents |
|---|---|
| `coupling/` | core library, OpenFOAM plugins, unit tests |
| `solvers/overWaveDyMFoam/` | overset VOF 6-DOF solver with waves2Foam relaxation zones |
| `cases/` | [test battery](cases/README.md), from a smoke test to waves + turbine |
| `external/` | pinned submodules: OpenFAST, waves2Foam, MoorDyn, foamMooring, stabRAS |
| `docs/` | container guide, manual build, release procedure, design notes |

## Validation

OC4-DeepCwind free decay in all six DOFs against OpenFAST baselines;
wave-only and wave-current tanks; coupled wave + turbine load cases.

## License

GPL-3.0-or-later . See [`LICENSE`](LICENSE).

## References

1. **Campaña-Alonso, G., Martín-San-Román, R., Méndez-López, B., Benito-Cia, P., & Azcona-Armendáriz, J. (2023).** *OF 2 : coupling OpenFAST and OpenFOAM for high-fidelity aero-hydro-servo-elastic FOWT simulations*. _Wind Energy Science_, **8**(10), 1597–1611. [https://doi.org/10.5194/wes-8-1597-2023](https://doi.org/10.5194/wes-8-1597-2023)

2. **Chen, H., & Hall, M. (2022).** *CFD simulation of floating body motion with mooring dynamics: Coupling MoorDyn with OpenFOAM.* *Applied Ocean Research*, 124(June), 103210. [https://doi.org/10.1016/j.apor.2022.103210](https://doi.org/10.1016/j.apor.2022.103210)
3. **Chen, H., Medina, T. A., & Cercos-Pita, J. L. (2024).** *CFD simulation of multiple moored floating structures using OpenFOAM: An open-access mooring restraints library.* *Ocean Engineering*, 303(March), 117697. [https://doi.org/10.1016/j.oceaneng.2024.117697](https://doi.org/10.1016/j.oceaneng.2024.117697)
