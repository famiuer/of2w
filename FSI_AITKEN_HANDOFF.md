# OF² FSI Instability & Aitken Dynamic Relaxation — Handoff

> **RESOLVED 2026-07-02 — root cause found (see §11 at the bottom).**
> The divergence was NOT an FSI/aero-damping issue: the aero-coupled decks had
> `ModCoupling = 3` in `derisk.fst`. OpenFAST v5's tight-coupling glue solver
> integrates ElastoDyn directly and never calls `ED_UpdateStates`, so the OF²
> `PMI_Inject` platform-state injection never ran — ED's internal platform was in
> free fall (CompHydro=0) and the tower-base wrench was computed from that state.
> **Fix: set `ModCoupling = 1` (loose) in the coupled decks.** All previously
> validated coupled cases already used 1.

**Author:** prior session (2026-06-30 → 07-02)
**Audience:** a new agent tasked specifically with the OF² aero-coupled FSI instability
**One-line status:** Aitken dynamic relaxation is implemented, unit-tested, and deployed;
it converges the *partitioned iteration* but does **not** cure the coupled instability.
The still-water no-aero model validates; **switching on the aerodynamic load, the platform
pitch grows (negative damping) to −4.5° and the blade strikes the tower at t≈13.2 s.**

---

## 0. TL;DR for the new agent

- **Symptom:** with OpenFAST aero ON, the coupled `overInterDyMFoam`+OF² run diverges as a
  **slow growing pitch oscillation** (pitch drifts +1.9° → −4.5° over 13 s), ending in an
  AeroDyn `TwrInfl` **tower strike**. Without aero (motion/mooring only) the model is stable
  and validated.
- **What was tried:** Aitken Δ² dynamic relaxation of the tower-base wrench (to kill a
  suspected partitioned added-mass instability). It works *as designed* (per-step residual
  converges, gate closes) but the **system still diverges** — so the root cause is **not** the
  partitioned-iteration/added-mass instability. It is a **negative damping introduced by the
  coupling itself** (the monolithic OpenFAST baseline is stable).
- **Where to look next:** the **platform velocity / pitch-rate imposed on OpenFAST** (aero
  pitch damping depends on it; a small persistent sign/magnitude error grows over ~13 s), and
  the moment/Euler-rate sign path. See §6.

---

## 1. System under study

- **Case:** `cases/oc4_of2_lc3p1still` (baseline, fixed relax) and `cases/oc4_of2_lc3p1still_aitken`
  (Aitken variant) on ham8; local mirrors under `baseline_models/oc4_of2_lc3p1still*`.
- **Physics:** OC4 semi-sub FOWT, LC3.1* — still water (VOF), steady 8 m/s wind, IC surge=5 m,
  pitch=1.9°. Solver `overInterDyMFoam` (overset). Coupling: CFD rigid-body ↔ OpenFAST
  (AeroDyn + InflowWind + ServoDyn/DISCON_OC3Hywind + ElastoDyn) + MoorDyn.
- **Coupling library:** `coupling/restraints/openfoam/of2/` → `libOF2.so`. It is an RBD
  `restraint`: each PIMPLE outer corrector it (1) imposes the body 6-DOF state on OpenFAST
  (`OF2_SetImposedPlatformState`), (2) advances OpenFAST one `fastDT`, (3) reads the tower-base
  reaction (`OF2_GetTowerBaseReaction`), (4) applies it as `fx[body] += spatialVector(M,F)`.
  Strong coupling: OpenFAST is snapshotted (`FAST_CFD_Store_SubStep`) on the first corrector and
  rolled back (`FAST_CFD_Reset_SubStep`) + re-advanced each repeat corrector.

## 2. THE VALIDATION STATE (critical context)

- **No-aero validation PASSES.** With the aerodynamic model OFF (motion + mooring only, and the
  free-decay / wave cases), the coupled model reproduces the OpenFAST baseline (period +
  amplitude). This is the established, trusted baseline of the whole coupling effort.
- **Aero ON → FAILS.** The moment the AeroDyn/InflowWind/ServoDyn stack is switched on, the
  pitch grows and the run dies at the tower. **This localises the bug to the aero-coupling path**
  (the wrench value/sign/timing and the platform kinematics imposed on OpenFAST), not the
  mooring or the VOF/CFD side.

## 3. The gold diagnostic signal (USE THIS)

Monolithic OpenFAST baseline (saved `baseline_models/openfast_lc3p1wave/lc3p1still_baseline_budget.{md,csv}`):

```
tower-base overturning  TwrBsMyt = +38.86 MN·m   (std 0.94, i.e. STEADY)
balance:  aero overturning (+38.9) = hydro restoring (−32.3) + mooring&gravity (−6.6)
```

**A correct coupled run must hold TwrBsMyt ≈ +39 MN·m.** In the failed aero run it instead
**oscillates and drifts negative** (mean +40 → −87 MN) as pitch runs away. Overturning moment
*decreasing / going negative* = the divergence signature.

## 4. THEORY — added-mass FSI instability & Aitken Δ²

**Partitioned added-mass instability (what Aitken targets).** In a partitioned FSI the fluid
(here: the platform dynamics) and the structure (here: tower+RNA via OpenFAST) are solved in
sequence and iterated. The interface fixed-point `w_{k+1}=H(w_k)` has gain `H'` set by the ratio
of added (structural) inertia to physical inertia. Tower+RNA added pitch inertia ≈ m·h² ≈
350 t·90² ≈ 2.8e9 vs platform pitch inertia 1.23e10 (~23%). If the *partitioned iteration* gain
|H'|>1 it expands across correctors → negative numerical damping → divergence. Fixed
under-relaxation `w += ω·r` (r = residual) with a small ω can stabilise it; the optimal ω is
problem-dependent.

**Aitken Δ² (Irons-Tuck; Küttler & Wall 2008).** Adapts ω each corrector from the residual
secant:
```
ω_k = −ω_{k−1} · ⟨r_{k−1}, Δr⟩ / ⟨Δr, Δr⟩ ,   Δr = r_k − r_{k−1}
```
For a 1-D map H(w)=a·w+b it gives the one-step-optimal ω = 1/(1−a). Clamp to [ω_min, ω_max].

**Paper alignment (Wang et al., interDyMFoam FSI; Appendix A, eqs 39-45).** Confirms the exact
approach (Küttler-Wall, Chow & Ng 2016). Differences we adopted:
- eq 41 uses the **|·| (magnitude)** form → ω stays *positive* on a monotone residual (the
  signed form returns negative → clamps to the floor → **freezes the wrench**; observed).
- ω restricted to **[0.1, 1]** (Chow & Ng), not our earlier [0.01, 1].
- eqs 43-45 **convergence criterion**: `r_force=|ΔF|/(m_s·g)`, `r_moment=|ΔM|/|M|`,
  `ϱ=max(r_force,r_moment)`; iterate until `ϱ<tol`.
- Paper drives ω on **force**; we deliberately drive on the **moment** (pitch is the divergent
  DOF — "option A").
- Paper relaxes the **acceleration**; we relax the **wrench** (natural for our OF2↔OpenFAST
  interface). NOT yet tried: relaxing the imposed acceleration instead.

## 5. IMPLEMENTATION (what exists now)

All in `coupling/restraints/openfoam/of2/`:

**`aitkenRelaxation.hpp`** — dependency-free `struct of2::AitkenRelaxation` (unit-testable with
g++). `configure(omegaInit,omin,omax)`, `resetStep()` (per CFD step, warm-starts ω, clears
history), `update(const double r[3])` returns ω. Key line (eq-41 |·| form):
```cpp
omega = std::fabs(omega * num / den);   // num=⟨rPrev,Δr⟩, den=⟨Δr,Δr⟩
omega = clampOmega(omega);              // [omegaMin=0.1, omegaMax=1]
```

**`of2Restraint.{C,H}`** — integration:
- Members: `of2::AitkenRelaxation aitken_`, `useAitken_` (Switch, default true), `omegaMin_`
  (0.1), `omegaMax_` (1.0), `fsiTol_` (1e-3), `fsiConverged_` (per-step latch).
- In `restrain()` relaxation block (drive on MOMENT, apply to full wrench):
  - `newStep`: `cachedWrench_=wrenchNew; aitken_.resetStep(); fsiConverged_=false;`
  - repeat corrector (if `!fsiConverged_`): `resid = wrenchNew − cachedWrench_`;
    `rho = max(|resid.l()|/(m_s·g), |resid.w()|/|cached.w()|)` (eqs 43-45);
    if `rho<fsiTol_` → `fsiConverged_=true` (HOLD wrench rest of loop);
    else `ω=aitken_.update(resid.w())` and `cachedWrench_ += ω·resid`.
  - mass via `model_.bodies()[bodyID_].m()`, g via `model_.g()`.
- Log line prints `omega=` , `rho=` , `(conv)`.
- Dict keys: `useAitken` / `omegaMin` / `omegaMax` / `fsiTol` (+ `relaxFactor` = warm-start ω).

**Build:** `wmake libso .` with `OPENFAST_SRC_DIR` + `OPENFAST_BUILD_DIR` exported.
- local: `OPENFAST_SRC_DIR=/home/cg/oc4_github/external/openfast`,
  `OPENFAST_BUILD_DIR=/home/cg/oc4_github/build/openfast`, OF bashrc
  `/usr/lib/openfoam/openfoam2306/etc/bashrc`.
- ham8: `source /nobackup/nnrq22/coupling/scripts/env.sh`; src at
  `/nobackup/nnrq22/coupling/src/of2_restraint/`; build via `scripts/01_build.sh` (stage 3).
  NOTE ham8 g++ is `-std=c++11` (hpp uses only C++11 features).

## 6. TESTS (all green)

`coupling/restraints/openfoam/of2/test/run_tests.sh` — 3 tiers:
- **Tier 1 (g++ standalone):** `frameAlgebra_test`, `aitken_test.cpp`. Aitken tests: stiff map
  H=−4w+5 fixed-0.5 diverges / Aitken converges to ω=1/(1−a)=0.2; vector map; clamp; **[E] |·|
  anti-flooring** (monotone residual → ω stays 0.5, not floored).
- **Tier 2 (Foam):** `testCouplingFrame` — the frame conversion (axis map, Euler-rate Jacobian,
  wrench rotation, gimbal gate, lever-arm velocity).
- **Tier 3 (Foam, FAST stubbed):** `harness/testRestrainHarness.C` + `of2_fast_stubs.C`. Runs the
  REAL `restrain()`. Tests 1-5: IC, lever-arm velocity, wrench rotation. **Test 6 (CALL 6):** a
  closed-loop added-mass stub (`M_y = M0 − I_add·a_pitch`, `g_addedMassPitch`) driven through
  `forwardDynamics` across correctors — proves **fixed-0.5 diverges, Aitken converges** on a
  stiff added-mass map.

**Interpretation:** the tests validate the Aitken machinery on an *added-mass* map. Because the
real failure is NOT an added-mass partitioned instability (see §7), these green tests do not
cover the actual bug — a **coupling test with a velocity-dependent (damping) aero stub** is
missing and would be worth adding (see §8).

## 7. RESULTS — what actually happened

**Local smoke (aero ON, Aitken, 3 correctors), t→0.062 s:** clean. Modules init physical
(FAST_Sizes DT=0.0125 TMax=600 NumOuts=50; IC exact surge5/pitch1.9/heave−0.0074; gravity→heave).
FSI **converges**: rho 0.10 (startup) → <1e-3 by step ~8, gate closes; ω adapts in [0.1,1], no
flooring; My ~20 MN tracking baseline early-time. **The relaxation scheme is working.**

**ham8 full run (job 17700046, 48 ranks, endTime 200):** ran 20 h, reached **t=13.19 s**, then
**FAILED** — AeroDyn `TwrInfl:Tower strike`. Trajectory (last corrector per step):

| t | pitch° | My(MN) | Fx(kN) |
|---|---|---|---|
| 0.01 | 1.90 | 22.8 | 183 |
| 1.0 | 1.84 | 56 | 624 |
| 2.0 | 1.76 | 8.6 | 17 |
| 5.0 | 1.17 | 6 | −62 |
| 7.5 | −0.01 | −5.7 | −189 |
| 9.5 | −1.59 | −87 | −1014 |
| 13.0 | −4.50 | +55 | 1405 → strike |

- **Pitch drifts monotonically +1.9° → −4.5°** (through eq 1.728° and zero), offset from eq
  **grew ~36×** — violent **negative damping**, not decay. Period context: OC4 pitch T≈26 s, so
  this is ~half a *growing* swing.
- **My does NOT hold +39 MN** — it oscillates and its mean drifts to −87 MN; as My goes
  negative it drives pitch negative → more negative My → runaway.
- **Same endpoint as the original wrong model** (pitch→−4.5°, strike ~13.5 s).
- Courant healthy (~0.5) throughout → **not** a CFD/mesh blow-up. Thrust Fx balloons to ~2 MN
  (~4× physical) as the platform motion grows.

**Key deductions:**
1. Aitken converges the per-step FSI (gate closes) yet the system diverges ⇒ the divergence is
   **NOT** the partitioned added-mass iteration. It is negative damping in the **coupled pitch
   dynamics** as computed.
2. The monolithic OpenFAST baseline is **stable** (pitch settles to 1.728°). So the coupling
   *injects* the negative damping — a bug in **what/how we couple**, not the physics.
3. **Baseline-cancellation was misread:** job 17697685 was stopped at t≈2 s on "My collapsing
   57→8.6". But My *oscillates* (56@1s, 8.6@2s, 41@3s…); that was a downswing, not divergence.
   The true failure is the slow 13 s pitch drift, common to fixed-relax and Aitken alike.

## 8. DIRECTIONS TO GO (ranked)

**A. Localise the negative damping (do first, cheap).**
Overlay the coupled run's **pitch(t)** and **tower-base My(t)** against the monolithic OpenFAST
baseline over 0-13 s. Determine whether damping is negative from t=0 (structural coupling error)
or only once motion grows (nonlinear). Data already in `log.solver` (OF2 step lines: disp[4]=pitch,
Mraw=tower-base moment, TwrBs F). Baseline in `baseline_models/openfast_lc3p1wave/`.

**B. Audit the platform VELOCITY / pitch-rate imposed on OpenFAST (prime suspect).**
Aerodynamic pitch damping ∝ platform pitch rate (relative wind at the rotor). A wrong **sign** or
**magnitude** of the imposed angular velocity flips damping negative — small per step, fatal over
13 s. Check in `of2Restraint::restrain()` / `CouplingFrame`:
- `bodyPointVelocity(platformReferencePointBody_)` → `angVelWorld`, `linVelWorld`.
- the Euler-rate Jacobian `dofRates` in `frameAlgebra.hpp`/`CouplingFrame` (maps world ω → ED
  Euler rates) — verify **sign** and axis mapping dynamically (not just the static pose).
- the lever-arm velocity (sample at PtfmRef, not body origin) — earlier quantified "small (~4%
  thrust)" but a *persistent* damping-sign error need not be large to diverge. Re-examine.
- Confirm the acceleration path (`model_.a(bodyID_)` + `model_.g()`) and its transform sign.

**C. Verify the wrench sign/timing.**
The tower-base moment must *oppose* pitch increase (positive stiffness/damping). Confirm
`loadToWorld` (F_W=R_WB·F_T, M_W=R_WB·M_T) and the about-origin lever term `twrBs×F` give the
right sign at a tilted pose. Check for a one-corrector **lag** (explicit-in-time coupling) that
reads as negative damping even when converged.

**D. Isolate aero-only in OpenFAST-side.**
Drive OpenFAST with a *prescribed sinusoidal pitch* (offline, no CFD) and measure the tower-base
moment phase vs pitch rate — confirms whether OpenFAST returns net-damping or net-antidamping for
the imposed-motion convention OF2 uses. This directly tests the coupling convention against a
known-stable reference.

**E. Add a damping-aware harness test.**
Tier-3 test 6 uses an added-mass stub (`M∝−a`). Add a **velocity/damping stub** (`M∝−c·ω_pitch`
with the coupling's sign convention) and assert the closed loop is *stable* — this would have
caught the current bug. If it reproduces the growth, the sign bug is in the imposed-velocity path.

**F. (fallback) Relax the acceleration, not the wrench** (paper eq 39-40) and/or add outer-loop
FSI convergence on ϱ — only if A-E show a genuine residual partitioned instability (unlikely given
the gate already closes).

## 9. Artifacts & commands

- Source: `coupling/restraints/openfoam/of2/{aitkenRelaxation.hpp,of2Restraint.C,of2Restraint.H}`
- Tests: `.../of2/test/{run_tests.sh,aitken_test.cpp,frameAlgebra_test.cpp,harness/}`
- Cases: ham8 `cases/oc4_of2_lc3p1still{,_aitken}`; local `baseline_models/oc4_of2_lc3p1still{,_aitken}`
- Baseline budget: `baseline_models/openfast_lc3p1wave/lc3p1still_baseline_budget.{md,csv}`
- Failed run log: ham8 `cases/oc4_of2_lc3p1still_aitken/background/log.solver`
  (+ `scripts/slurm-of2-lc3p1aitk-17700046.out`)
- ham8 access: `ssh ham8`; env `source /nobackup/nnrq22/coupling/scripts/env.sh`; NO parentheses
  in remote ssh echo strings; the restart slurm `[1-9]*` glob deletes `5MW_Baseline` — never use it.
- Run: `sbatch scripts/oc4_of2_lc3p1still_aitken.slurm` (NP=48, rebuilds mesh NP=10→48, endTime 200).
- Extract trajectory: `awk` over `log.solver` `[OF2] step … fired=1` lines (t, disp[4]=pitch,
  Mraw[2]=My, TwrBs F[0]=Fx).

## 10. Related memory

`[[project_of2_aitken_fsi_fix]]`, `[[project_oc4_of2_lc3p1still_aero_coupling]]`,
`[[project_of2_strong_implicit_coupling]]`, `[[feedback_consistency_validation_metric]]`,
`[[feedback_freedecay_baseline_dof_match]]`, `[[project_freedecay_cfd_vs_openfast_motion]]`.

---

## 11. RESOLUTION (2026-07-02) — root cause: ModCoupling=3 bypasses PMI_Inject

**Root cause.** `cases/oc4_of2_lc3p1still{,_aitken}` `derisk.fst` had `ModCoupling = 3`
(inherited from the monolithic baseline fst). In OpenFAST v5, ModCoupling=2/3 places
ElastoDyn under the glue tight-coupling solver (`FAST_Solver.f90`: `p%iModTC` includes
ED unless `ModCoupling == LooseCoupling`), which integrates ED's continuous states
itself and **never calls `ED_UpdateStates`** — the routine where the OF² patch hooks
`PMI_Inject` (platform QT/QDT injection). Consequence: the CFD-imposed platform motion
never reached ElastoDyn's states. With `CompHydro=0`/`CompMooring=0`, ED's internal
platform was a fully free body:

- **PtfmHeave in literal free fall**: −½gt² (−4.85 m @1 s, −122 m @5 s, −827 m @13 s).
- ED pitch drifting under unopposed thrust; rotor decelerating 9.0 → 7.7 rpm; thrust
  collapsing and reversing (TwrBsFxt −800 kN @10 s).
- The tower-base wrench returned to the CFD was computed from this fantasy state —
  its mean collapsed and went negative, removing the +39 MN·m aero overturning moment
  from the CFD pitch balance → pitch ran away → the AeroDyn "tower strike" happened in
  ED's internal free-falling world, not the CFD one.

`PMI_InjectQD2T` (hooked in `ED_CalcOutput`, which IS still called under tight
coupling) DID fire — the imposed accelerations entered the reaction-load sum, keeping
the early-time wrench plausible and letting the Aitken residual converge. That masked
the bug (Aitken worked as designed; the target was wrong).

**Why no-aero validated:** every validated coupled case (overset v11/v12, oc4_wave_equi,
hifi decks) uses `ModCoupling = 1`. Only the two aero lc3p1still decks had 3.

**Evidence (all offline, local `build/openfast`, CSV `PtfmPrescribed.csv` path):**
- Replaying the failed run's imposed trajectory offline with ModCoupling=3 reproduces
  the failed run's wrench almost exactly (My @9 s: −70.8 vs coupled −72.2 MN·m) —
  the FSI machinery was irrelevant.
- Constant pose (surge 5 m, pitch 1.9°) with ModCoupling=3: platform outputs free-fall.
  With **ModCoupling=1**: pose held exactly, RotSpeed 8.95 rpm, and
  **TwrBsMyt settles at +39.5 MN·m** (baseline: 38.87 at its 1.728° equilibrium) —
  the §3 gold diagnostic passes.
- Imposed-kinematics audit of the failed run: pitchRate ≡ d(pitch)/dt (slope 1.000)
  and aP ≡ d(pitchRate)/dt — §8.B/C suspects (velocity/wrench sign paths) are CLEAR.

**Fix:** set `ModCoupling = 1` in the coupled decks (ham8:
`/nobackup/nnrq22/coupling/cases/oc4_of2_lc3p1still{,_aitken}/background/openfast/derisk.fst`
and local mirrors), re-run. Monolithic baselines may keep 3.

**Hardening (recommended):** make PMI_Bridge count PMI_Inject calls and have
of2Restraint (or ED_Init) abort when OF2 is active but the injection never fires /
ModCoupling ≠ 1; in coupled runs, cross-check ED's PtfmHeave output channel against
the imposed heave.

**Production confirmation (2026-07-02, jobs 17729773 fixed-relax / 17729774 Aitken,
ModCoupling=1 + TMax=2000 + explicit useAitken):** at the decisive t=5-6 s window —
where the failed run's mean My had gone NEGATIVE (−5.7 MN·m, Fx −112 kN, pitch 1.02°
plunging at −0.27°/s) — both reruns hold My = +43..+46 MN·m (baseline +50), Fx +431 kN,
pitch settled at ~1.61-1.64° with rate −0.006°/s (baseline equilibrium 1.728°). The
runaway is gone; fixed-relax and Aitken are statistically identical through this window.
