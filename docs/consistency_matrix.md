# OC4-DeepCwind OF² consistency matrix

Cross-check between the OC4-DeepCwind spec (Robertson 2014, NREL/TP-5000-60601,
Tables 3-1, 3-2, 3-3), OpenFAST ElastoDyn (`*ElastoDyn.dat`), OpenFOAM rigid
body (`constant/dynamicMeshDict`), and MoorDyn (`Mooring/lines_oc4.txt`) for
the OF² coupled mode.

The convention here is **Option C: body-frame ≡ world-frame at design
equilibrium**. Every body-frame coordinate in every file equals the OC4
spec's world-frame value (z = 0 at SWL). At t = 0 the rigid-body
transform is identity, so body-frame and world-frame are coincident.

## Geometry — Table 3-1 / 3-2

| Quantity | OC4 spec | OpenFAST ED | OpenFOAM body | Mooring | Match? |
|---|---|---|---|---|---|
| z = 0 reference | SWL | `PtfmRefzt = 0` | water box top at z=0; `transform.r = (0 0 0)` | — | ✓ |
| Hull bottom z | −20 m | — | STL z_min = **−20** | — | ✓ |
| Offset column top z | +12 m | — | STL z_max = **+12** | — | ✓ |
| Main column top z (= tower base) | +10 m | `TowerBsHt 10` | `towerBasePoint (0 0 10)` | — | ✓ |
| Tower top z (above SWL) | +87.6 m | `TowerHt 87.6` | n/a | — | ✓ |
| UC1, UC3 plan x | +14.43 m | — | STL ✓ | — | ✓ |
| UC2 plan x (asymmetric column) | −28.87 m | — | STL ✓ | — | ✓ |
| UC1 / UC3 plan y | ±25 m | — | STL ✓ | — | ✓ |
| Base column diameter | 24 m | — | bbox extent matches | — | ✓ |
| Offset column diameter | 12 m | — | snappy cavity matches | — | ✓ |
| Main column diameter | 6.5 m | — | snappy cavity matches | — | ✓ |

## Mass and inertia — Table 3-3 (total platform, including ballast)

| Quantity | OC4 spec | OpenFAST ED | OpenFOAM body | Match? |
|---|---|---|---|---|
| Total platform mass | 1.3473×10⁷ kg | `PtfmMass 13473000` ¹ | `mass 1.3473e7` | ✓ |
| CM location below SWL | 13.46 m | `PtfmCMzt -13.46` ¹ | `centreOfMass (0 0 -13.46)` | ✓ |
| Roll inertia about CM | 6.827×10⁹ kg·m² | `PtfmRIner 6.827E+09` ¹ | `inertia(0,0) = 6.827e9` | ✓ |
| Pitch inertia about CM | 6.827×10⁹ kg·m² | `PtfmPIner 6.827E+09` ¹ | `inertia(3,3) = 6.827e9` | ✓ |
| Yaw inertia about CM | 1.226×10¹⁰ kg·m² | `PtfmYIner 1.226E+10` ¹ | `inertia(5,5) = 1.226e10` | ✓ |

¹ — **In coupled mode (CompHydro = 0) we override the stock standalone values.** See note below.

## Mooring (Robertson 2014 Phase II)

| Quantity | OC4 spec | Mooring file | Match? |
|---|---|---|---|
| Fairlead radius | 40.87 m | Point 5 at (−40.87, 0) | ✓ |
| Fairlead world z | −14 m | Points 4/5/6 Z = `-14.0` | ✓ |
| Fairlead angular positions | 60° / 180° / 300° | (20.43, 35.39), (−40.87, 0), (20.43, −35.39) | ✓ |
| Anchor radius | 837 m | Point 2 at (−837.6, 0) | ✓ |
| Anchor depth | −200 m | Points 1/2/3 Z = `-200.0` | ✓ |
| Chain unstretched length | 835.35 m | `UnstrLen 835.35` | ✓ |
| Chain mass / length | 113.35 kg/m | `Mass/m 113.35` | ✓ |
| Chain EA | 7.536×10⁸ N | `EA 7.536e8` | ✓ |
| Chain Cd | 2.0 | `Cd 2.0` | ✓ |
| Chain Ca | 0.8 | `Ca 0.8` | ✓ |

## OpenFAST module switches in coupled mode

| Module | Coupled `.fst` | Why |
|---|---|---|
| `CompElast` | 1 (ED only active module) | ED computes tower-base reaction wrench |
| `CompHydro` | 0 | CFD replaces HydroDyn |
| `CompMooring` | 0 | OpenFOAM-side `moorDynR2` replaces ED-side MoorDyn |
| `CompServo` | 0 | rotor stopped for v6 decay test |
| `CompAero` | 0 | rotor stopped for v6 decay test |
| `CompInflow` | 0 | rotor stopped for v6 decay test |
| `CompSeaSt` | 0 | no waves in decay test |
| `CompSub` | 0 | platform is rigid (OpenFOAM model assumption); SubDyn is incompatible |
| `CompIce` | 0 | not in scope |

## ¹ Note: why ED's Ptfm* are overridden in coupled mode

The OC4 spec Table 3-3 gives the **total platform** (structure + ballast)
mass, CM, and inertias. In **standalone OpenFAST + HydroDyn**, the
inputs follow a different convention:

- `PtfmMass = 3,852,180 kg` (dry structure only)
- `PtfmCMzt = -8.6588 m` (dry structure CM)
- `PtfmRIner = PtfmPIner = 2.56193e9, PtfmYIner = 4.24265e9` (about dry structure CM)
- HydroDyn separately adds the 9.62×10⁶ kg sea-water ballast via
  Morison fill-density inside the column-base volumes (z ≈ −20 to −14)
- The combined ED + HydroDyn system has the correct total dynamics
  matching spec Table 3-3.

In **OF² coupled** mode (`CompHydro = 0`), HydroDyn is off:

- The OpenFOAM rigid body owns the *complete* platform dynamics
  (structure + ballast)
- ED's role reduces to computing the tower-base reaction wrench
- ED's structural inputs only matter for the moment-arm of that wrench
- For physical consistency, ED's Ptfm* inputs in coupled mode should
  reflect the **total platform**, i.e. spec Table 3-3 values
- Therefore in the coupled `.fst` we set:
  - `PtfmMass = 1.3473e7` (total)
  - `PtfmCMzt = -13.46` (total CM)
  - `PtfmRIner = PtfmPIner = 6.827e9` (about total CM)
  - `PtfmYIner = 1.226e10` (about total CM)

This is **not** the conventional standalone ED input convention. It
is the correct OF²-coupled convention. Future readers seeing
`PtfmCMzt = -13.46` in a coupled `.fst` should NOT "fix" it back to
`-8.6588` — that would re-introduce a 4.8 m moment-arm error in the
tower-base wrench for pitch/roll modes.

## Why CM is at z = −13.46 (well below the platform mid-height)

Naively the CM should be near the geometric center of the platform
(z ≈ −4, midway between −20 hull bottom and +12 column top). It's
not, because **71 % of the platform mass is sea-water ballast packed
into the base columns near the bottom**:

| Component | Mass | Fraction | Mass-weighted z |
|---|---|---|---|
| Dry structure | 3.85×10⁶ kg | 29 % | ≈ −8.66 m |
| Sea-water ballast | 9.62×10⁶ kg | 71 % | ≈ −15.4 m |
| **Weighted average** | **1.3473×10⁷ kg** | 100 % | **−13.46 m** ✓ |

The low CM gives the floater its metacentric stability under a 5 MW
turbine on top. It is by design.

## Summary

After applying the consistency edits ("Option C"), every coordinate
and every physical parameter in the OF²-coupled `oc4-decay-v6` /
`oc4_stl_confirmation` case agrees with:

1. **OC4-DeepCwind spec** (Robertson 2014, Tables 3-1, 3-2, 3-3) — exactly
2. **OpenFAST ED convention** — with the documented coupled-mode total-platform override
3. **OpenFOAM rigid-body convention** — body-frame ≡ world-frame at design eq
4. **MoorDyn body-frame attachment convention** — fairlead body-frame z = world z = −14

Any residual mismatch between an OF² run's heave decay and an OpenFAST +
HydroDyn baseline is now physics (CFD-vs-WAMIT model difference), not
coordinate-system or mass-distribution bookkeeping.
