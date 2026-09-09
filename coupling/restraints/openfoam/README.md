# OpenFOAM MoorDyn Restraint (RBD)

This directory contains the OpenFOAM-facing `liboc4MoorDynRestraint` shim for the
OC4 coupling stack. It is the **vendor-independent fallback** mooring path; the
**primary** mooring path is FOAMmooring's `moorDynR2`.

The restraint is a **rigid-body-dynamics (RBD) restraint**
(`Foam::RBD::restraints::moorDynRestraint`, run-time type **`moorDynCoupling`**),
matching the OF2 method (Martín-San-Román et al., WES 8, 1597, 2023, Listing A1),
which drives the platform with `motionSolver rigidBodyMotion`. It is **not** a
`sixDoFRigidBodyMotion` restraint.

Design split (unchanged solver-agnostic core, new thin RBD shim):

- `moorDynRestraint`: OpenFOAM `Foam::RBD::restraints::restraint` class
- `MoorDynRestraintBridge`: converts a rigid-body-motion snapshot into `PlatformState`
- `MoorDynLoadProvider`: owns load shifting and optional CSV tracing
- `MoorDynCApiAdapter`: owns the raw MoorDyn C API

OF2 separation of concerns:

- OpenFOAM owns platform rigid-body motion
- the restraint reads the current body kinematics from the `rigidBodyModel`
- MoorDyn is stepped behind a dedicated wrapper (once per CFD time step)
- one resultant global-frame wrench is returned to the rigid body

## Why RBD (and how time/dt is obtained)

`restrain(scalarField& tau, Field<spatialVector>& fx, const rigidBodyModelState& state)`
receives `state.t()` and `state.deltaT()`. MoorDyn is advanced exactly **once per
CFD time step**: the shim steps only when the solution time advances and caches
the wrench across the RBD `nIter` / PIMPLE outer-corrector re-calls within a step
(loose/staggered coupling). The `sixDoFRigidBodyMotionRestraint` interface does
not expose time/dt, which is why coupling uses RBD.

## Body kinematics → MoorDyn

Per step, for the platform reference point `fromJtoPtfmReferencePoint` (body frame):

- position: `bodyPoint(refPtBody)` (global)
- orientation: `model_.X0(bodyID).E().T()` = R(global←body)
- linear velocity: `bodyPointVelocity(refPtBody).l()`
- angular velocity: `bodyPointVelocity(refPtBody).w()`

The returned wrench `(F, M_ref)` is applied about the global origin as the RBD
convention requires: `fx[bodyIndex] += spatialVector(appPt × F + M_ref, F)`.

## Cluster build

From this directory on the cluster, with the OpenFOAM v2306 environment loaded:

```bash
wmake libso .
```

Produces `$(FOAM_USER_LIBBIN)/liboc4MoorDynRestraint.so` (distinct name from
FOAMmooring's `libmoordynRestraint.so`, so both can coexist). Requires
`libmoordyn.so` (MoorDyn v2) discoverable at run time, or set `libraryPath`.

## Notes

- The shim uses RBD APIs verified against OpenFOAM-v2306: `rigidBodyModel::X0/v/a`,
  `restraint::bodyPoint/bodyPointVelocity`, `rigidBodyModelState::t/deltaT`,
  `spatialTransform::E`, `spatialVector(w, l)`.
- MoorDyn's coupled-body velocity rotational slot is fed the **angular velocity**
  (its quaternion-rate / fairlead-velocity convention), not Euler-angle rates.
- `debugCsvPath` records the motion/load exchange at every step — use it for
  cluster-side sign and tension checks during Stage-1 validation.
- It cannot be compiled off-cluster (needs the OpenFOAM build); the solver-agnostic
  core is covered by the `ctest` suite in `build/coupling`.
