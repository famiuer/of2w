# Coordinate conventions

Single most common source of bugs in OF2W is mixing reference frames and
sign conventions across OpenFOAM, ElastoDyn, and MoorDyn. This document
states the rules and points out where v3/v6 deviated from them.

## Global inertial frame `G`

One project-wide frame, used as the lingua franca for all exchanged data:

- `+x` → surge / wave-propagation direction
- `+y` → sway / transverse direction
- `+z` → up
- gravity `g = (0, 0, -9.81)`
- still-water level (SWL) is at `z = 0` by convention. **All
  setFieldsDict water boxes should top at z=0 unless deliberately used
  as a heave IC offset (see `runbook.md` §setFields trick).**

## Platform reference point (PRP)

ElastoDyn defines `PtfmRefxt/yt/zt` as the world coordinates of the
platform's reference point at design eq. For OC4-DeepCwind:

```
PtfmRefxt = 0    PtfmRefyt = 0    PtfmRefzt = 0     (i.e. PRP at MSL)
```

The OpenFOAM rigid-body solver carries the PRP as the body's
`platformReferencePointBody_` (body-frame), default `(0, 0, 0)`. It
becomes the world position `bodyPoint(platformReferencePointBody_)`
that `of2Restraint` sends to ED.

**Rule**: the OF body's PRP in world coords at t=0 should equal ED's
`(PtfmRefxt, PtfmRefyt, PtfmRefzt)`. Set the body's `transform`
translation so that the body-frame origin lands on `(0, 0, 0)`, or
override `platformReferencePointBody_` in `dynamicMeshDict` to point
at the body-frame coord that maps to `(0, 0, 0)` world.

v6 status: `transform.r = (0, 0, -12.722)` and PRP defaults to body
origin → PRP world z = −12.722. **Mismatch with ED's expected 0**. The
`of2Restraint` relative-coordinate convention masks this for translational
DOFs but introduces a moment-arm error if pitch/roll are non-zero.

## Centre of gravity (CoG)

ED: `PtfmCMzt = -8.6588` m (world z of CoG when body at design eq).

OF: `centreOfMass` is in body-frame. For v3 with `transform.r = (0, 0,
-12.722)` and `centreOfMass = (0, 0, 0.54)`, the CoG world z =
−12.722 + 0.54 = **−12.182**.

This is 3.5 m below ED's expected CoG. The tower-base wrench from ED is
computed with the moment arm to ED's `PtfmCMzt = -8.6588`; when OF
applies the wrench at `towerBasePoint`, the body's response uses the
3.5-m-offset CoG. Pure heave is invariant. Pitch/roll decay tests will
show this as a torque/moment-arm scaling error.

## Tower-base point

ED: `TowerBsHt = +10` m (world z of tower base when body at design eq).

OF: `towerBasePoint` is body-frame in `dynamicMeshDict`. v3/v6 use
`(0, 0, 24)`. With `transform.r.z = -12.722`, world z = +11.278.

1.3 m mismatch with ED. The wrench applied at this point is what ED
computes for the tower base; if OF's tower-base point in world differs
from ED's by 1.3 m, the wrench is applied 1.3 m off in z. For a pure
vertical wrench (gravity transfer), no torque error; for moment
contributions, there is a torque error proportional to the 1.3 m offset.

## Mooring fairleads

Mooring/lines_oc4.txt body-frame fairlead z = +1. OF2W applies the body's
*current* world pose to MoorDyn → world fairlead z = `transform.r.z +
1` = −11.722 in v3/v6.

ED-side spec / OC4 reference: fairlead world z = −14 (i.e. body-frame
relative to a different "body origin" convention).

**Mismatch: 2.3 m.** This is the **leading suspect** for the residual
~1 m drift of v3/v6's CFD equilibrium below design eq — the catenary's
weight distribution depends on the suspended chain length, which is
sensitive to fairlead world z.

## STL geometry

OpenFOAM's `rigidBodyMotion` `transform` does NOT translate the body's
geometry — `snappyHexMesh` carves the cavity at the literal STL world
coords. To physically place the body at a different world location, you
must either:

1. shift the STL itself (then re-run `snappyHexMesh`), or
2. shift the entire mesh post-snappy via `transformPoints -translate`.

For v6, the STL is at the OC4 design draft world coords (z=[-19.722,
+12.278], close to the spec [-20, +10]). The lifted offset of 0.278 m
from the spec hull bottom is a small residual from STL processing,
absorbable as a few cm of heave drift.

## Rotation matrices vs Euler angles

The OF↔ED interface transmits Euler XYZ angles (R, P, Y in radians) as
part of the dispWorld payload (positions 3..5). Internally, `of2Restraint`
carries the rotation matrix `R_world<-body` and decomposes to Euler
angles only at the `OF2_SetImposedPlatformState` boundary, where the
imposed disp[6] vector packs translations into positions 0–2 and Euler
R/P/Y (radians) into positions 3–5.

**Avoid using Euler angles as transport in any of your own code** —
they're singular at ±90° pitch and the decomposition order matters. Use
rotation matrices or quaternions internally.

## Quick checklist for any new OF2W case

Before submitting, verify:

1. [ ] Domain `blockMeshDict` has SWL at z=0 (or note the offset).
2. [ ] STL z-range covers OC4 design draft (z ∈ [-20, +10] for the OC4
       hull; tolerate ±1 m for small adjustments).
3. [ ] `setFieldsDict` water-box top is at z=0 (or note the IC offset).
4. [ ] `dynamicMeshDict.bodies.platformBody.centreOfMass` is the
       body-frame CoG offset from body origin.
5. [ ] `dynamicMeshDict.bodies.platformBody.transform.r` is the body
       origin's world coords at t=0.
6. [ ] `platformReferencePointBody` (optional) — body-frame coord that
       maps to ED's `PtfmRefxt/yt/zt = (0,0,0)`. If body origin is at
       world MSL, this can stay as default `(0,0,0)`.
7. [ ] `towerBasePoint` — body-frame coord that, after transform, lands
       on ED's `TowerBsHt = +10` world.
8. [ ] `Mooring/lines_oc4.txt` fairlead z values consistent with the
       intended world z (= body-frame z + transform.r.z).
9. [ ] ED `.fst` has `CompHydro=0`, `CompMooring=0`, `CompServo=0` for
       coupled OF2W decay (other modules: depends on test).
10. [ ] ED `.dat` platform IC values are all 0 (driven by OF).
