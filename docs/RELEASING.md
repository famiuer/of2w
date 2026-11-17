# Releasing OF2W

How a version `vX.Y.Z` is produced, and the rules that keep releases
reproducible. A release consists of four things that must agree:

| Artefact | Where | Identified by |
|---|---|---|
| Source | `github.com/famiuer/of2w` | git tag `vX.Y.Z` |
| The five forks | `github.com/famiuer/{openfast,MoorDyn,foamMooring,stabRAS,waves2Foam}` | git tag `of2w-vX.Y.Z` on the commit the submodule points at |
| Container image | `ghcr.io/famiuer/of2w` | image tag `X.Y.Z` |
| Archive | Zenodo record [10.5281/zenodo.22744317](https://doi.org/10.5281/zenodo.22744317) | new version with the source tarball and the `.sif` |

## Rules

1. **Tags are never moved.** A git tag, a fork tag and an image tag
   `X.Y.Z` always mean the same bytes. A mistake is fixed by a new
   version, not by re-tagging.
2. **`latest` moves only after the tests pass** on the image it will
   point at.
3. **Build knowledge lives in `bootstrap.sh`** (and `env.sh`,
   `INSTALL.md`). The `Containerfile` and the CI workflows only call
   it; `scripts/truth_guard.sh` enforces this in CI.
4. **Nothing is published before it has passed** case `00_ci_smoke`
   inside the image.
5. **No credentials in the repository.** Publishing uses your own
   GitHub login locally, or the workflow's temporary `GITHUB_TOKEN`.

## Checklist

### 1. Prepare

- [ ] `main` is green in CI (the `core-ci` badge).
- [ ] Every fork change is committed and pushed in its fork, and the
      submodule pointers in `of2w` are bumped to those commits.
- [ ] `git submodule status` shows no `+` or `-` prefixes.
- [ ] `CITATION.cff` has the new version and date.
- [ ] `docs/manual_build.md` pin table lists the new fork tag.

### 2. Verify natively

```sh
git clone --recurse-submodules https://github.com/famiuer/of2w.git && cd of2w
./bootstrap.sh && source env.sh
cd cases/01_minimal_coupling && ./run.sh      # must print PASS
```

A build on your everyday machine is not a clean test: an existing
`~/OpenFOAM` directory and an older compiler can hide problems. The
container build in step 4 is the clean one.

### 3. Tag the source and the forks

```sh
V=X.Y.Z
git tag -a v$V -m "OF2W v$V" && git push origin v$V
git submodule foreach "git tag -a of2w-v$V -m 'pinned by OF2W v$V' && git push origin of2w-v$V"
```

### 4. Build and test the image

```sh
docker build -t ghcr.io/famiuer/of2w:$V \
    --build-arg OF2W_REF=v$V \
    --build-arg OF2W_SHA=$(git rev-parse --short v$V) \
    -f Containerfile .

docker run --rm -e OMPI_ALLOW_RUN_AS_ROOT=1 -e OMPI_ALLOW_RUN_AS_ROOT_CONFIRM=1 \
    ghcr.io/famiuer/of2w:$V bash -c \
    'source /opt/of2w/env.sh && cd /opt/of2w/cases/00_ci_smoke && NP=4 ./run.sh'
```

- [ ] The build log shows the expected commit in the clone step.
- [ ] Case 00 prints `PASS` in Docker.
- [ ] Convert and test under Apptainer as a normal user:

```sh
apptainer build of2w-$V.sif docker-daemon://ghcr.io/famiuer/of2w:$V
apptainer exec of2w-$V.sif bash -c \
    'source /opt/of2w/env.sh && cp -r /opt/of2w/cases/00_ci_smoke . && cd 00_ci_smoke && NP=4 ./run.sh'
```

- [ ] Case 01 prints `PASS` inside the image at least once per release
      (same command with `01_minimal_coupling` and `NP=10`, about 1.5-2 h on a 10-core desktop).

### 5. Publish the image

```sh
gh auth refresh -s write:packages          # once per machine
gh auth token | docker login ghcr.io -u famiuer --password-stdin
docker push ghcr.io/famiuer/of2w:$V
docker tag  ghcr.io/famiuer/of2w:$V ghcr.io/famiuer/of2w:latest
docker push ghcr.io/famiuer/of2w:latest
```

- [ ] The package is set to **public** (GitHub → your profile →
      Packages → `of2w` → Package settings → Change visibility). New
      packages are private by default.
- [ ] From a machine that has never seen the image:
      `apptainer pull of2w.sif docker://ghcr.io/famiuer/of2w:$V` works
      without logging in.

### 6. Publish the release

- [ ] Create the GitHub release from tag `v$V` with the changelog.
- [ ] Upload to the Zenodo record as a new version: the source tarball
      including submodules, and `of2w-$V.sif`.
- [ ] Update the DOI badge only if Zenodo issued a new concept DOI
      (normally it does not).

### 7. Final check on a cluster

- [ ] On the HPC system, pull the published image and run case 00 in a
      single-node job ([container.md](container.md), sections 4 and 8).

## What breaks a release, from experience

These were all found by the first container build and were invisible on
the development machine:

| Problem | Why it was hidden | Guard now in place |
|---|---|---|
| A fork build script fetched upstream branch tips instead of the pinned commits | a full clone tolerated it | forks check out pinned commits only |
| A fork compiled against headers under `~/OpenFOAM` | the developer's home already contained them | forks include their own headers |
| Legacy Fortran in waves2Foam fails on gfortran ≥ 10 | the development machine had gfortran 9 | `bootstrap.sh` adds the compatibility flag when needed and checks the libraries exist |
| A dependency was cloned from upstream `master` at build time | it happened to work that day | pinned to a fixed commit |
| A build step failed but the script reported success | upstream scripts ignore errors | `bootstrap.sh` checks for each product explicitly |
