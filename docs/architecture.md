# OF² architecture

## Module split

```
coupling/
├── common/        # solver-agnostic utilities (Formatting, types, logging)
├── moordyn/       # MoorDyn C API adapter (ForcedMoorDyn-like wrapper)
├── openfast/      # OpenFAST C API adapter (FAST_OpFM_* wrapping)
│   ├── bridge/    # thin C ABI for plugins that can't link C++ directly
│   ├── runtime/   # process-lifecycle (FAST_Init, FAST_OpFM_Solution0, ...)
│   ├── src/       # ForcedOpenFASTCApiAdapter — the C++ class plugins use
│   └── include/   # public headers
├── restraints/    # adapters used by OpenFOAM-side restraints
│   ├── src/       # OpenFASTLoadProvider, MoorDynLoadProvider,
│   │              # MoorDynRestraintBridge — convert OF² kinematics to/from
│   │              # the wrench format the upstream libraries expect
│   └── openfoam/  # the actual OpenFOAM plugin classes
│       ├── moorDynRestraint.{C,H}    # body restraint, calls foamMooring
│       └── of2/of2Restraint.{C,H}    # body restraint, drives ElastoDyn
└── config/        # example input dicts for the adapters' unit tests
```

## Data flow per CFD step

1. OpenFOAM's `rigidBodyMotion` solver finishes the previous step,
   producing the body's new pose `X(t)` and velocity `V(t)`.
2. Inside `interFoam`'s PIMPLE loop, `rigidBodyMeshMotion` calls each
   restraint's `restrain()` method to compute the forces/moments to add
   for the next sub-iteration.
3. `of2Restraint::restrain()`:
   - Computes `prpWorld = bodyPoint(platformReferencePointBody_)`
   - On first call: latches `prpInitialWorld_ = prpWorld`
   - `dispWorld = prpWorld − prpInitialWorld_` → 6-vector
     `(Sg, Sw, Hv, R, P, Y)` (translations + Euler R/P/Y in rad)
   - Decomposes body angular velocity in world frame
   - Once per CFD step (gated by `lastStepTime_ + 0.5*dt`):
       a. `OF2_SetImposedPlatformState(disp, vel)` — push the 6-vector
          displacement and velocity into ElastoDyn's internal m%QT /
          m%QD2T arrays (this is the OF² Fortran patch on the famiuer
          openfast fork).
       b. `FAST_Update(...)` — advance ElastoDyn by one DT, evaluating
          tower dynamics under the imposed platform kinematics.
       c. `OF2_GetTowerBaseReaction(F, M)` — read the resulting tower-
          base wrench (force [N], moment [N·m]) in world coordinates.
       d. Cache the wrench for re-use on subsequent sub-iteration calls
          within the same CFD step.
   - Adds the cached wrench to the body at `towerBasePoint` (body-frame)
     on every call within the CFD step.
4. `moorDynR2::restrain()` (foamMooring's restraint, not OF²'s but used
   alongside): communicates the body's *absolute* world pose to MoorDyn,
   receives chain forces at the fairleads, adds them to the body.

## Why two coordinate conventions

`of2Restraint` uses **relative** coordinates (dispWorld) because
ElastoDyn's platform DOFs all start at zero in the coupled `.fst`
(`PtfmSurge=PtfmHeave=...=0`). The PRP's *absolute* world position lives
in OpenFOAM's `transform` and STL geometry; ED never sees it.

`moorDynR2` (from foamMooring) uses **absolute** coordinates because
MoorDyn computes chain tension from anchor world positions, and the
chain geometry depends on where the body actually is.

This means if you re-frame the body (change `transform`, lift/lower the
STL, etc.), the of2Restraint side is invariant — but the moorDyn side
sees a different fairlead world position, which changes the catenary
mooring force. The current v6 residual offset traces back to this:
fairleads are at world z = −11.72 instead of OC4 spec z = −14.

## File-level wiring

A coupled case dynamicMeshDict has two restraints:

```cpp
restraints
{
    mooring
    {
        type             moorDynR2;        // from librigidBodyMooring.so
        body             platformBody;
        couplingMode     "BODY";
        inputFile        "Mooring/lines_oc4.txt";
        bodies           ( platformBody );
        outerCorrector   3;
    }

    aero
    {
        type             OpenFast;         // from libOF2.so (this repo)
        body             platformBody;
        fstFile          "openfast/derisk.fst";
        towerBasePoint   (0 0 24);         // body-frame
        verbose          true;
    }
}
```

`libOF2.so` selects the OpenFAST restraint by type name `OpenFast`.
The library is registered through OpenFOAM's runtime selection
mechanism declared in `of2Restraint.C`.
