# Contributing to OF2W

Thanks for your interest. OF2W is research software from a PhD project;
contributions that make it easier to build, run and trust are the most
valuable kind.

## Reporting problems

Open a GitHub issue with:
- what you ran (case, command) and what happened (paste the last ~20 log
  lines, and the `source env.sh` readiness report);
- your platform: OS/cluster, compiler, MPI, OpenFOAM version (only
  v2306 ESI is tested), and the output of `./bootstrap.sh --list`.

Build failures in the submodules (OpenFAST, waves2Foam, MoorDyn,
foamMooring, stabRAS) are in scope — the pinned forks are part of OF2W.

## Contributing changes

- Fork, branch from `main`, keep PRs focused on one thing.
- Before submitting: the 9 unit tests pass
  (`cd build/coupling && ctest`) and, for anything touching the coupling
  or a case, `cases/01_minimal_coupling/run.sh` prints PASS.
- New functionality needs either a unit test or a case-level check.
- Cluster recipes are welcome: a short note of the module loads +
  settings that made OF2W build on your machine helps the next person.

## Scope notes

- Physics/validation changes should reference the comparison they were
  checked against (OpenFAST baseline, experiment, or published result).
- The six cases in `cases/` are the regression battery — changes that
  alter their PASS numbers need a justification in the PR.
