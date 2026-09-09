# Relay restart failure — diagnosis (2026-08-10)

All three `_long` relay chains completed their first 72 h leg (TIMEOUT, as
designed) and then **failed on every restart leg**. This is the read-only
investigation; **no code was changed**. Fix plan is §7.

Failing example analysed end-to-end:
`/nobackup/nnrq22/coupling/scripts/slurm-bwc_relay-18255069.out`
(case `oc4_of2_wh6t10c0p5_v4p1_long`, restart from checkpoint t=160).

---

## 1 · The resume script is NOT the problem

The relay leg did everything correctly:

| check | result |
|---|---|
| found newest complete checkpoint | `RESTART from complete checkpoint t=160` ✅ |
| trimmed partial time dirs | processor0 now ends exactly at 160 ✅ |
| five-artifact set present | fields + `rigidBodyMotionState` + `of2RestraintState` + `moorDynState` + `chkpt_160.chkp` ✅ |
| `of2RestraintState` sane | `prpInitialWorld (8.5791 0 0.0329)`, `accCfdTime 2.27e-12`, plausible `cachedWrench` ✅ |
| MoorDyn | `MoorDyn module state RESTORED` ✅ |
| deck coupling mode | `ModCoupling = 1` (loose) — correct for OF2 ✅ |
| chained follow-up | submitted ✅ |

**One real (separate) flaw:** `afterany` chaining resubmits even when a leg
FAILS, so one failure became a runaway — **1,489 failed relay jobs**, cycling
every ~3 min until the chain burned out. Needs a guard (§7.3).

---

## 2 · Failure chain (proven from the log)

`FAST_Restart` reports success with fully consistent metadata:

```
[OF2] FAST_Restart -> ok. checkpoint="openfast/chkpt_160".chkp
      DT=0.0125 NumOuts=50 n_t_global=6400 (OpenFAST clock t=80 s)
```

(n_t_global 6400 x 0.0125 = 80 s = exactly the coupled elapsed time. Cold
start reported the identical `DT=0.0125 TMax=2000 NumOuts=50 AbortErrLev=4`.)

Then, in order:

1. **step 9367** (first step after restart): `subSteps=0 fired=0` — dt < fastDT,
   so no `FAST_Update`; the restored `cachedWrench` is held. Fine.
2. **step 9368** — the *first actual* `FAST_Update` after the restore:
   ```
   ED disp=(9.3352942,-0.73854382,0.10561077, 0.0512deg,0.7084deg,0.2257deg)
   Lacc=(-0.465,-0.0047,-0.0767)m/s2        <- inputs all FINITE
   TwrBs F=(-nan -nan -nan) N  Mraw=(nan -nan nan) N-m   <- output NaN
   ```
3. Our PMI bridge caches `m%AllOuts(TwrBsFxt..TwrBsMzt)` inside
   `ED_CalcOutput` (ElastoDyn.f90:1531-1536), so **ElastoDyn itself produced
   the NaN** from restored state — the imposed inputs were healthy.
4. NaN wrench -> body state NaN (`Centre of rotation: (nan nan nan)`)
   -> overset mapping collapses (`interpolated : 0`, `hole : 198751`)
   -> alpha/U/p_rgh all NaN
   -> 64-rank `FOAM FATAL IO ERROR ... 'nan' ... data.solverPerformance.p_rgh`.

**The IO error is a symptom three layers downstream, not the cause.**

---

## 3 · The concrete defect: an init-only routine is run after restore

Our restart path (`of2Restraint.C`, `initOpenFAST()` restart branch):

```
FAST_AllocateTurbines -> FAST_Restart -> FAST_CFD_InitIOarrays_SubStep
```

OpenFAST's own CFD-coupling reference,
`glue-codes/openfast-cpp/src/OpenFAST.cpp`, splits the two cases cleanly:

```cpp
case fast::init:                       // cold start
    ... FAST_CFD_Solution0(...);
        FAST_CFD_InitIOarrays_SubStep(...);   // line 910

case fast::trueRestart:                // restart
    FAST_ExtInfw_Restart(...) / FAST_Restart(...);
    allocateMemory_postInit(iTurb);
    get_ref_positions_from_openfast(iTurb);
    readRestartFile(iTurb, nt_global);
    checkAndSetSubsteps();
    // NO FAST_CFD_InitIOarrays_SubStep, NO Solution0
```

Why it matters — `FAST_InitIOarrays_SubStep_T` (FAST_Subs.f90:4834) creates
the sub-step save slots with **`MESH_NEWCOPY`**:

```fortran
do i = 1, size(Turbine%m_Glue%ModData)
   do j = 1, Turbine%p_FAST%InterpOrder + 1
      call FAST_CopyInput(ModData(i), Turbine, INPUT_CURR, -j, MESH_NEWCOPY, ...)
   end do
   call FAST_CopyStates(ModData(i), Turbine, STATE_CURR, STATE_SAVED_CURR, MESH_NEWCOPY, ...)
   call FAST_CopyStates(ModData(i), Turbine, STATE_PRED, STATE_SAVED_PRED, MESH_NEWCOPY, ...)
end do
```

On a **cold start** those slots do not exist yet -> NEWCOPY is correct.
After a **restart** the checkpoint has already restored them -> we are doing
a "new copy" onto already-allocated mesh/state targets. This is the leading
suspect for the corrupted turbine state.

*Status: strong, evidence-backed hypothesis (a verified divergence from the
reference implementation), not yet proven to be the sole cause.*

---

## 4 · Why the Phase-5 bench missed it (validation gap)

| | bench `oc4_overset_surge_v12` (PASSED) | production `_long` (FAILS) |
|---|---|---|
| CompElast | 1 | 1 |
| CompInflow | **0** | **1** (InflowWind, steady 8 m/s) |
| CompAero | **0** | **2** (AeroDyn) |
| CompServo | **0** | **1** (Bladed DISCON, PCMode=5 / VSContrl=5) |
| TMax | 120 | 2000 |

The restart was validated against an **ElastoDyn-only** deck. Production
checkpoints a far larger module set (many more meshes/states), which is
exactly where the NEWCOPY-onto-existing problem has surface area. **The bench
must be re-run with an aero+servo deck.**

---

## 5 · Ruled out (do not re-investigate)

- **Checkpoint corruption / truncation** — every `chkpt_*.chkp` is exactly
  65,500,165 B; `chkpt_160` was written 13:18, the TIMEOUT was 13:32 (14 min
  later), so the file was complete and closed.
- **Missing or stub `.dll.chkp`** — present; 52 B = 13 IEEE floats decoding to
  `80.0, 80.0, 80.0` (DISCON `LastTime/LastTimePC/LastTimeVS`) plus ~922 rpm
  filtered generator speed. The DISCON *does* implement the OpenFAST
  checkpoint extension (status -8/-9) and saved valid state.
- **Stale DLL procedure pointer** — `DLLTypeUnPack` (NWTC_IO.f90:1667) calls
  `LoadDynamicLib` on unpack when the handle was associated.
- **AeroDyn <-> InflowWind `FlowField` pointer** — aliasing preserved via
  `RegPackPointer` / `RegUnpackPointer`.
- **`m_Glue` not checkpointed** — it is (`Glue_PackMisc` in
  `FAST_PackTurbineType`).
- **Sizing mismatch** — NumOuts 50 == 50, DT and AbortErrLev identical cold
  vs restart; `FAST_Update` would have raised a fatal otherwise.
- **Our un-restored `fastStepIdx_`** — the `t_global` it feeds is computed but
  never used in `FAST_Store_SubStep_T`; `FAST_Reset_SubStep` uses the
  correctly-restored module-level `n_t_global`.
- **ModCoupling** — 1 (loose), as OF2 requires.
- **The relay/resume script** — see §1.

---

## 6 · Secondary gaps found

1. **OpenFAST output file is never re-opened on restart.** The reference
   restart branch calls `findOutputFile()`; we do not. Consequence: the failed
   legs wrote **zero** rows to `derisk.out` (the rows past t=80 in that file
   are leg-1's tail, not the restart's) — worth knowing so the file is not
   misread as evidence.
2. **`.dll.chkp` companions are not pruned** by `checkpointKeep` (fixed in
   commit `116e236`, **not yet deployed to ham8** — deliberately deferred).
3. **`afterany` runaway** (§1).

---

## 7 · Fix plan
> **Status 2026-08-11 (superseded — see §10):** §7.1 and §7.3 implemented
> locally (plus the §9 −g first-call fix). The aero+servo bench then STILL
> failed, §7.2's isolation test was run, and the true root cause found —
> §3's InitIOarrays hypothesis is WRONG (kept only as reference alignment).
> Read §10 before touching anything.

1. **Remove `FAST_CFD_InitIOarrays_SubStep` from the restart branch** of
   `of2Restraint::initOpenFAST()`, mirroring `OpenFAST.cpp`'s `trueRestart`
   case. (Keep it on the cold path.)
2. **Re-validate with an aero+servo deck** — copy a production deck
   (`CompInflow=1, CompAero=2, CompServo=1`) into the restart bench. This is
   the essential change; the ElastoDyn-only bench cannot catch this class of
   bug. Decisive isolation test if needed: restart the **standalone**
   `openfast` binary from the same `chkpt_160` (no OpenFOAM, no PMI bridge) —
   if that also NaNs, the problem is OpenFAST-level; if it is clean, it is our
   call sequence.
3. **Chain guard** in `coupled_restart.slurm.example`: stop the chain after
   N consecutive non-advancing legs (compare the leg's last `Time =` against
   its start time), or switch to `afterok` + an explicit TIMEOUT-detect
   resubmit, so a failure can never spawn 1,489 jobs again.
4. Optional: re-open the OpenFAST output file on restart (§6.1); deploy
   `116e236` (§6.2).

---

## 8 · State of the data (nothing lost)

Leg-1 results are intact and all checkpoints remain on disk:

| case | reached | last checkpoint |
|---|---|---|
| `oc4_of2_c0p5_long` | t = 121 | `chkpt_121` |
| `oc4_of2_wh6t10_v4p1_long` | t = 157 | `chkpt_157` |
| `oc4_of2_wh6t10c0p5_v4p1_long` | t = 160 | `chkpt_160` |

`derisk.out` for the WC case was backed up to `derisk.out.leg1backup` before
any testing. Once the fix is in, each relay can resume from exactly these
points — no rerun of leg 1 is needed. Queue is currently empty.

---

## 9 · C-case chain verified (18220055 → 18220841) — same defect, plus one new finding (2026-08-11)

Cross-check of the `oc4_of2_c0p5_long` pair confirms the §2/§3 diagnosis is
chain-independent:

- **Leg 1 (18220055)**: healthy to t=121.28, killed by the 72 h wall limit as
  designed. Final state clean (Co≈0.5, continuity ~1e-10, body velocities
  ~0.01 m/s).
- **Leg 2 (18220841)**: every resume stage succeeded — checkpoint t=121 found,
  time dirs trimmed, MoorDyn `RESTORED`, `of2RestraintState` restored
  (`PRP_initial=(5.9172 0 -0.0194)`, `|cachedWrench|=85.2 MN`), `FAST_Restart
  -> ok` with fully consistent metadata (`n_t_global=8079` × 0.0125 =
  100.9875 s = coupled elapsed time). Then the **first** `FAST_Update`
  (step 10159, t=121.009) returned `TwrBs F=(-nan -nan -nan)` from finite
  inputs → body `Centre of rotation: (nan nan nan)` → overset collapse
  (`interpolated : 0`, `hole : 198665`) → all-field NaN → 64-rank FOAM FATAL
  IO ERROR. Identical cascade to §2.
- The c0p5 deck is **also aero+servo** (`CompInflow=1, CompAero=2,
  CompServo=1`, Bladed DISCON) — consistent with §4: all three production
  chains exercise the module set the ElastoDyn-only bench never covered.

**All three chains verified (2026-08-10 retry legs).** The three retries fail
identically — NaN out of ElastoDyn on the **first actual `FAST_Update`** after
restore, never earlier, never later:

| job | case | restart t | first call | first fire → NaN | Lacc sent on that fire |
|---|---|---|---|---|---|
| 18255077 | `wh6t10_v4p1_long` (W) | 157 | step 9176, `fired=0`, held wrench OK | step 9177 | `(0.128, 0.003, −0.179)` — clean |
| 18255078 | `wh6t10c0p5_v4p1_long` (WC) | 160 | step 9367, `fired=0`, held wrench OK | step 9368 | `(−0.465, −0.005, −0.077)` — clean |
| 18255079 | `c0p5_long` (C) | 121 | step 10159, fires immediately (`accCfdTime=0.0125=fastDT`) | step 10159 | `(0.001, −0.002, −9.812)` — the −g glitch |

W and WC held the restored `cachedWrench` for one step (body motion stayed
sane — the CFD/RBD/mooring side of the restart is fully functional) and then
NaN'd on a *clean, finite* imposed state. This isolates the root cause to
OpenFAST-internal state corrupted during our restart call sequence (§3), not
to the inputs.

**New (secondary) finding — spurious free-fall acceleration on a leg's first
fired step.** The step-10159 log line shows
`Lacc=(0.0008, -0.0019, -9.8124632)` m/s² — exactly −g — versus ~−0.002 at
leg-1's end. Cause: `of2Restraint.C:702-711` recovers the classical
acceleration as `aSpat.l() + model_.g()` (Featherstone bakes gravity into the
root link's spatial accel, so a body at rest returns `+g` which the correction
cancels). On the *first* `restrain()` call of any leg, `model_.a(bodyID_)` is
still zero — no forward-dynamics solve has run yet — so the correction emits a
bare −9.81 m/s². The artifact is visible in all three retry logs' first call
(`Lacc_z` = −9.81 / −9.89 / −9.99), but only the C case actually *sent* it to
OpenFAST: its restored `accCfdTime=0.0125` equals `fastDT`, firing a sub-step
on the very first CFD step, whereas W and WC (`accCfdTime≈2e-12`) held that
step, fired on step 2 with clean inputs — and NaN'd anyway. So this is **not** the NaN
cause — the §3 state-corruption hypothesis stands — but it is a real one-step
input error on every leg start (cold starts included) and should be fixed
alongside §7 (e.g. suppress the `+ g` correction, or hold the cached wrench,
until the first forward-dynamics solve has populated `model_.a()`).

---

## 10 · TRUE ROOT CAUSE (2026-08-11): `FAST_Restart` never calls `NWTC_Init()`

The local aero+servo bench (production deck: `CompInflow=1, CompAero=2,
CompServo=1`, Bladed DISCON, `RotSpeed=9.05`) reproduced the production NaN
**even with §7.1 applied** — so §3 was not the cause. Systematic elimination
with a minimal C driver against `libopenfastlib` alone
(`scratchpad/isotest/mini_restart.c`: AllocateTurbines → FAST_Restart →
FAST_Update, no OpenFOAM):

| experiment | result |
|---|---|
| standalone `openfast -restart chkpt_0.5` (correct cwd) | clean — 13 s finite physics ⇒ checkpoint healthy |
| driver, no Store / no InitIOarrays / no PMI arming | **NaN** on first update (22/50 channels) |
| driver, with Store; with Store+InitIOarrays | identical NaN ⇒ all three innocent |

The NaN channels are exactly the geometry-dependent loads (blade root, LSS,
yaw bearing, tower base); restored *states* (PtfmSurge, wind) come through
finite. Mechanism: the NWTC library's global constants (Pi, TwoPi, R2D, mesh
module init) are **zero until `NWTC_Init()` runs**. Every path into the
library initialises them except ours:

- cold start: `FAST_Sizes → FAST_InitializeAll → NWTC_Init` ✓
- standalone restart: `FAST_RestoreFromCheckpoint_Tary → FAST_ProgStart → NWTC_Init` ✓
- `FAST_ExtInfw_Restart` / `FAST_ExtLoads_Restart`: explicit `CALL NWTC_Init()` ✓
- **`FAST_Restart` (our entry): nothing** ✗ — its historical caller
  (Simulink) always calls FAST_Sizes first, so upstream never noticed.

With Pi=0 the whole restarted run computes garbage trigonometry: ElastoDyn
limps (why the ED-only §4 bench PASSED and misled us), AeroDyn's BEMT NaNs
immediately → NaN airloads → NaN tower-base wrench → §2's cascade.

**Fix:** one line, mirroring the sibling routines — `CALL NWTC_Init()` at the
top of `FAST_Restart` (FAST_Library.f90; patched in both local clones of the
fork at pin `45eea9808`; idempotent, cold path unaffected).

**Validation (bench, 2026-08-11):** minimal driver → 0/50 NaN in all modes,
azimuth advancing at exactly 9 rpm. Full bench: runB restart leg clean to
t=1.0; runA/runB overlay over 0.5–1.0 s agrees across ALL subsystems —
surge rms 3.7e-7 m, TwrBs Fx rms 11 N (std 165 kN), FairTen2 rms 0.016 kN,
**RotSpeed rms exactly 0**, TwrBsMyt rms 0.63 kN·m (signal 12.5 MN·m).
`compare_bench.py` extended to include the OpenFAST channels
(RotSpeed/BldPitch1/TwrBsMyt) alongside motion + mooring.

Corrections to earlier sections: §3 INNOCENT (removal kept as reference
alignment); §5's "checkpoint corruption ruled out" CONFIRMED by standalone
test; §6.1 WRONG — `FAST_RestoreFromCheckpoint_T` *does* reopen the .out for
append (failed legs wrote no rows only because they died before first flush).

**Deploy to ham8 before resubmitting relays:** rebuild the OpenFAST fork
library with the NWTC_Init patch, rebuild libOF2 (of2Restraint.C changes),
install the updated relay script (chain guard + non-clobber archive), then
manually sbatch each chain — restart points chkpt_121/157/160 are verified
intact (§8, re-checked 2026-08-11).
