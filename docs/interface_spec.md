# Interface specification

## OpenFAST C API

OF² calls OpenFAST through the **`FAST_*` C ABI** (`FAST_Library.f90`'s
`FAST_Sizes` / `FAST_Start` / `FAST_Update` / `FAST_End`) combined with
the **`OF2_*` Fortran bridge** that we add to OpenFAST in the
`famiuer/openfast @ of2-coupling-v0.1` patch series (commits
`1000592b8` through `45eea9808`). The bridge exposes three new
C-callable symbols that inject CFD-computed platform state into ED
and read out the tower-base reaction. The adapter that wraps both
APIs is in `coupling/openfast/`. Key entry points:

| Function | Purpose | Called from |
|---|---|---|
| `FAST_Sizes`                       | Read .fst, return DT, TMax, NumOuts, AbortErrLev. Trailing Fortran OPTIONAL args must be NULL-defaulted in C/C++ declarations (see §19.10). | `of2Restraint` constructor |
| `FAST_Start`                       | Solve initial conditions at t=0, populate first row of OutputAry. NumInputs_c MUST be 51 (the FAST_OpFM input vector size) or `FAST_Update` silently fails later. | `of2Restraint` constructor |
| **`OF2_SetImposedPlatformState`**  | (OF² patch) Push the body's `disp[6]` and `vel[6]` into ED's m%QT and m%QD2T arrays before stepping. | Once per CFD step, beginning of `of2Restraint::restrain()` |
| `FAST_Update`                      | Advance ED by one DT, evaluating tower dynamics with the imposed platform kinematics. | Once per CFD step, after `OF2_SetImposedPlatformState` |
| **`OF2_GetTowerBaseReaction`**     | (OF² patch) Read the tower-base reaction wrench (`F[3]`, `M[3]`) that the tower exerts on the platform after the step. | Once per CFD step, after `FAST_Update` |
| `OF2_IsActive`                     | (OF² patch) Sentinel: returns true if the OpenFAST binary has the OF² patches linked in. Used to fail fast if a colleague accidentally builds against stock NREL OpenFAST. | of2Restraint constructor (post-`FAST_Start`) |
| `FAST_End`                         | Clean shutdown, write `.sum` files. | of2Restraint destructor |

### Wrench convention

`OF2_GetTowerBaseReaction` returns the wrench in the **OpenFAST
inertial frame** (world frame, z up, g = −9.81 z) at the **tower-base
node** (ED's `TowerBsHt` height above PtfmRefzt). Layout:

```
F[0..2] = (Fx, Fy, Fz)        [N]   force the tower exerts on the platform at tower base
M[0..2] = (Mx, My, Mz)        [N·m] moment the tower exerts on the platform about tower base
```

This represents the load that the *tower* transmits to the *platform*
through the structural interface. It includes:

- tower static weight (m_tower · g, downward)
- inertial reaction (m_tower · a_z) from floater acceleration
- elastic restoring moments from tower bending (zero in v6 because
  TwFADOF/SSDOF=False makes the tower rigid relative to the platform)
- yaw-bearing friction (zero in v6, YawDOF=False)
- rotor gyroscopic / aero contributions (zero in v6: CompAero=0,
  RotSpeed=0)

The OpenFOAM-side `of2Restraint` adds the wrench **directly** (no sign
flip) to the rigid body at `towerBasePoint` (body-frame). The sign
convention is "load on platform from the tower" — ED's positive Fz
is upward on the platform, so for a stationary tower under gravity
`Fz ≈ −m_tower · g` (negative = tower pulling platform down).

### Fortran OPTIONAL args (gotcha)

OpenFAST's C-bound subroutines have several Fortran `OPTIONAL` arguments.
When linking C++ to them via `extern "C"` declarations, **all optional
args must be declared with `= nullptr` defaults** — never omit them.
Omitting causes undefined behaviour at the Fortran-C ABI boundary on
some compiler combinations (gfortran 8 + glibc 2.28 was the culprit on
our test cluster).

This is encoded in `coupling/openfast/include/coupling/ForcedOpenFASTCApi.h`.

## MoorDyn C API

The OpenFOAM-side mooring uses **foamMooring**'s `moorDynR2` restraint
(library `librigidBodyMooring.so`), which is *separate* from this repo
but is a build-time dependency. foamMooring talks to MoorDyn through
the same C API that `coupling/moordyn/MoorDynCApiAdapter.cpp` wraps.
Both use the same MoorDyn binary; they don't share state.

If you need OF²-internal MoorDyn for non-OpenFOAM tests, use the
`coupling_moordyn` adapter directly (see `coupling/moordyn/tests/`).

### MoorDyn IC sequencing (gotcha — v2.6.x)

For MoorDyn v2.6 bundled with foamMooring, the static IC convergence
(`icStationary`) is broken (floating-point exception). The legacy
dynamic IC (`ICgenDynamic 1`) works and is what the reference case
uses. Sequential Point IDs in the mooring input are also required —
non-sequential IDs cause silent failures.

## OpenFOAM-side restraints

### `of2Restraint` — selectable name `OpenFast`

```cpp
restraints
{
    aero
    {
        type             OpenFast;
        body             platformBody;
        fstFile          "openfast/derisk.fst";
        platformReferencePoint  (0 0 0);     // optional, body-frame; default Zero
        towerBasePoint   (0 0 24);           // body-frame; where wrench is applied
        verbose          true;
    }
}
```

Behaviour:
- On first `restrain()` call (after the body has been positioned by its
  `transform`): latches `prpInitialWorld_` from the current world
  position of the body-frame PRP coordinate.
- Each CFD step: computes `dispWorld = current_PRP_world -
  prpInitialWorld_` and sends to OpenFAST. ED reports this as
  PtfmHeave/Surge/Sway (relative-from-initial convention).
- Receives the wrench, applies it at `towerBasePoint`.

### `moorDynRestraint` (from foamMooring, used together) — selectable name `moorDynR2`

```cpp
restraints
{
    mooring
    {
        type             moorDynR2;
        body             platformBody;
        couplingMode     "BODY";              // body kinematics → MoorDyn
        inputFile        "Mooring/lines_oc4.txt";
        bodies           ( platformBody );
        writeMooringVTK  false;
        outerCorrector   3;                   // PIMPLE outer corrector
    }
}
```

Differs from `of2Restraint`: communicates **absolute** body world pose
to MoorDyn (not relative), because catenary tensions depend on actual
anchor-fairlead geometry.

## CFD-side ↔ structural-side step gating

`rigidBodyMotion`'s `restrain()` is called multiple times per CFD step
(PIMPLE outer correctors × RBD sub-iterations). OpenFAST and MoorDyn
must advance **exactly once per CFD step**. Both restraints implement
the same gating pattern:

```cpp
if ((!hasStepped_ || t > lastStepTime_ + 0.5*dt) && dt > SMALL)
{
    // ... do the once-per-step work
    lastStepTime_ = t;
    hasStepped_   = true;
}
```

Within a sub-iteration, subsequent `restrain()` calls re-use the cached
wrench from the last advance.
