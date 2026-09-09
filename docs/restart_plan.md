# Restart support for coupled OF²W runs — findings and plan

Goal: a wall-time-aborted coupled run (overWaveDyMFoam + OpenFast restraint +
moorDynR2) restarts from `latestTime` like a normal OpenFOAM case, with
motion/load/tension continuity within FSI jitter.

## 1 · What restarts today, and what is lost

| State | Persisted? | Mechanism / gap |
|---|---|---|
| Fluid fields (U, p_rgh, alpha, phi, turbulence) | ✅ | normal time-dir writes, `startFrom latestTime` |
| Mesh point positions | ✅ | `pointDisplacement` + `polyMesh/points` in time dirs |
| Rigid-body state (q, qDot, qDdot) | ❌ **overset only** | see §2 — `uniform/rigidBodyMotionState` is never written under `dynamicOversetFvMesh`; on restart the body snaps to the dict pose with zero velocity |
| of2Restraint runtime state | ❌ | `prpInitialWorld_` (ED Sg/Sw/Hv reference!), `prevLin/AngVelWorld_` (acc diff), `accCfdTime_` (sub-step clock), `cachedWrench_` — all lost; `prpInitialWorld_` would be *recaptured at the restart pose*, silently offsetting every ED platform DOF |
| OpenFAST module states (ED/AD/tower, rotor azimuth, its clock) | ❌ | cold `FAST_Sizes`+`FAST_Start` only; restart would replay a fresh turbine from t=0 mid-motion |
| MoorDyn line states | ❌ | `moorDynR2` always `MoorDyn_Init` (IC solve at dict pose) |
| Workflow | ❌ | B-leg slurm guard *forbids* restart (`log.solver` contains "TwrBs F=" → abort); `tee log.solver` clobbers the series source |

## 2 · Root cause of the missing body state (found, empirical + source)

Empirical: every morphing-mesh case (`decay_1M_stl_*`, `oc4_of2_*_dm`) has
`processor*/​<t>/uniform/rigidBodyMotionState`; **no overset case has it**.

Source: `dynamicOversetFvMesh` derives from `dynamicMotionSolverListFvMesh`,
which constructs its motion solver via the **two-argument**
`motionSolver::New(*this, dict)` — that path `checkOut()`s the registration
(motionSolver.C:55), so the solver is *not* in the registry with AUTO_WRITE.
The **one-argument** path used by plain `dynamicMotionSolverFvMesh` builds its
own `IOdictionary` with `AUTO_WRITE` (motionSolver.C:152-160) — which is why
morphing cases write state. Additionally neither
`dynamicMotionSolverListFvMesh` nor `dynamicOversetFvMesh::writeObject`
forwards to `motionSolvers_` (private, no accessor). `rigidBodyMeshMotion`
itself *reads* `uniform/rigidBodyMotionState` READ_IF_PRESENT at construction
(rigidBodyMeshMotion.C:97-117) — so if we write the file, native restart of
the body works with no core patch.

## 3 · Native mechanisms already available

- **OpenFAST**: `FAST_CreateCheckpoint` / `FAST_Restart` (FAST_Library.h:42,51
  in the famiuer fork) — full binary state dump incl. `n_t_global` and all
  module states; restores dt/NumOuts/AbortErrLev on load. This is the
  FAST.Farm restart machinery — proven upstream.
- **MoorDyn v2.6**: `MoorDyn_SaveState` / `MoorDyn_LoadState` +
  `MoorDyn_Init_NoIC` (MoorDyn2.h:164-183, 493-519) — documented pattern:
  re-create system from the same deck, `Init_NoIC` at current kinematics,
  `LoadState`.
- **PMI bridge module state** needs no persistence: it is re-imposed by
  `OF2_SetImposedPlatformState` before every advance.
- **Aitken / corrector state** needs no persistence: reset per CFD step.

## 4 · Plan

### Phase 1 — body state (libOF2, no core patch) — IMPLEMENTED (of2RigidBodyStateWriter functionObject)
At each `writeTime`, the of2 restraint (which receives the
`rigidBodyModelState` every `restrain()` call) writes
`<t>/uniform/rigidBodyMotionState` in `rigidBodyMeshMotion`'s exact format
(q, qDot, qDdot). Native READ_IF_PRESENT then restores the body on restart.
*(Alternative considered and rejected: patching
`dynamicMotionSolverListFvMesh::writeObject` — requires an OpenFOAM core
patch on both machines; `motionSolvers_` is private so a solver-side cast
cannot reach it.)*

### Phase 2 — of2Restraint persistence + OpenFAST checkpoint — IMPLEMENTED (2a+2b)
- New `<t>/uniform/of2RestraintState` dict: `prpInitialWorld`,
  `prevLinVelWorld`, `prevAngVelWorld`, `accCfdTime`, `cachedWrench`,
  `fastUpdateCount`, `fastCheckpointRoot`.
- At `writeTime` (end of step, correctors converged):
  `FAST_CreateCheckpoint(iTurb, "openfast/chkpt_t<t>")`; keep the last K
  checkpoints (dict key `checkpointKeep`, default 3), prune older.
- `initOpenFAST()` restart branch: state dict present at `startTime` →
  `FAST_AllocateTurbines` → `FAST_Restart(chkpt)` (verify returned dt/NumOuts
  match the deck) → `FAST_CFD_InitIOarrays_SubStep` → skip `FAST_Sizes` /
  `FAST_Start`; restore the persisted scalars; do NOT recapture
  `prpInitialWorld_`.
- Dict keys: `checkpointControl (writeTime|timeInterval)`, `checkpointKeep`.

### Phase 3 — MoorDyn restart (famiuer/foamMooring fork patch) — IMPLEMENTED (branch of2-restart, d2f70c3)
`moorDynR2`: at `writeTime` → `MoorDyn_SaveState("<t>/uniform/moorDynState")`
(master rank); restart branch → `MoorDyn_Create` from the same deck,
`MoorDyn_Init_NoIC` at the restored body kinematics, `MoorDyn_LoadState`.
Fallback if LoadState proves fragile: `MoorDyn_Init` with `TmaxIC` dynamic
relaxation at the restart pose (≈ seconds-long tension transient, no drift —
acceptable but second-best).

### Phase 4 — workflow (slurm + extraction) — IMPLEMENTED (tools/coupled_restart.slurm.example)
- Replace the "no coupled restart" guard: allow start when
  `of2RestraintState` + FAST checkpoint + moorDynState exist at `latestTime`;
  trim any partial time dirs *beyond the newest complete checkpoint triple*.
- Log rotation: `tee -a` or `log.solver.$SLURM_JOB_ID`; series extraction
  becomes `cat log.solver*` (time-sorted) — the "TwrBs F=" awk is unchanged.
- Auto-continuation: submit with `--dependency=afterany:<prev>` + a
  completion check (endTime reached → exit, else run) so a case chains
  through walls unattended.
- Constraint: same decomposition (NP=64) on restart — document in runbook.

### Phase 5 — validation (gold standard: kill/restart twin overlay)

**5.1 PASSED (2026-08-05)** — local bench (baseline_models/oc4_restart_bench,
overset surge-decay clone of cases/oc4_overset_surge_v12, 323k cells, 10
ranks, splice 0.5 s / end 1.0 s): all five artifacts written at the splice;
seg2 restored of2RestraintState + FAST_Restart (clock n_t_global=39 ✓) +
MoorDyn LoadState. rms(restart − reference) over 0.5-1.0 s: surge 1.4e-7 m
(7e-5 of signal), TwrBs Fx 3.6 N / My 217 N·m (~2% of signal = the
per-corrector FSI jitter scale), FairTen2 0.02 kN (0.7%). Overlay:
bench_overlay.png — no visible splice discontinuity in any channel.

Two findings folded back into the implementation/docs:
- Collective-IO trap (fixed): the of2RestraintState probe/read must run on
  ALL ranks — IOdictionary is a global object whose header read is a
  collective; master-gating it desynced Pstream and segfaulted the next
  collective (overset zone transforms) with a misleading stack.
- MoorDyn out-clock quirk (documented): the restarted instance truncates
  lines_oc4.out and stamps rows with its internal clock (restarts at 0)
  plus one pre-LoadState zero row; post-processing must shift by the splice
  time and drop zero rows. Line-state physics is exact (endpoint tension
  matches to 7 significant figures).
1. Bench: 20 s of the W case; checkpoint at 10 s; kill; restart; overlay vs
   an uninterrupted twin. Accept: motion/TwrBs My/T2 differences within
   per-corrector FSI jitter (|Δ| ≪ the 1e-3 fsiTol residual scale) and no
   visible discontinuity at the splice.
2. Unit: OpenFAST-only checkpoint/restart harness in ctest (adapter-level,
   in-process fake CFD driver) asserting wrench continuity.
3. Production dry-run: restart a finished kow case from its wall snapshot for
   ~5 s and diff the overlap region against the original run.

## 5 · Risks / notes
- FAST checkpoint size & cost: single-turbine ~MB and ~ms — negligible next
  to the 1-s fluid snapshots (~4 GB each across 64 ranks).
- Checkpoints must be written at end-of-step only (never mid-corrector; the
  Store/Reset sub-step arrays are re-initialized on restart instead).
- `MoorDyn_SaveState`/`LoadState` smoke-tested (tools/moordyn_restart_smoke.cpp,
  v2.6, production OC4 deck): PASS, bit-exact round-trip (max|dF|=0 over 240
  steps at |F|~2.2 MN). Phase 3 can rely on it; the TmaxIC fallback is moot.
- The restart is not bit-exact (acc diff seeds one step with stored
  velocities; Aitken warm-start differs) — continuity, not bit-identity, is
  the acceptance criterion, consistent with the consistency-vs-baseline
  validation philosophy.
