/*---------------------------------------------------------------------------*\
    OC4 OF2-method OpenFAST coupling restraint (rigid-body-dynamics framework).

    This is the OpenFOAM-side libOF2 restraint per Campana-Alonso et al.
    (Wind Energ. Sci. 8, 1597, 2023). It is the AERODYNAMIC counterpart of the
    moorDynCoupling restraint used for moorings: on each CFD step it imposes
    the body's 6-DOF state onto OpenFAST (via the patched-ElastoDyn PMI_Bridge
    C ABI - OF2_SetImposedPlatformState) and applies the tower-base reaction
    wrench returned by OpenFAST (OF2_GetTowerBaseReaction) as an external
    force/moment on the body.

    Coupling sequence per CFD time step:
       1. body kinematics -> OF2_SetImposedPlatformState(disp, vel, acc)
       2. FAST_Update(...)  - advances OpenFAST one DT
       3. OF2_GetTowerBaseReaction(F, M)  - reads the tower-base reaction
       4. Apply wrench:  fx[bodyIndex] += spatialVector(r_PRP_to_TwrBs x F + M, F)

    This file (Phase A): SKELETON. The restraint registers as type "OpenFast"
    so it can be added to dynamicMeshDict, but restrain() is a no-op (returns
    zero wrench). Phase B adds OpenFAST init via FAST_*; Phase C wires the
    per-step driving in restrain().

    Driven by `motionSolver rigidBodyMotion` (see dynamicMeshDict_rbd).
    Built independently of liboc4MoorDynRestraint; the two coexist in a case.
\*---------------------------------------------------------------------------*/

#ifndef RBD_of2Restraint_H
#define RBD_of2Restraint_H

#include "rigidBodyRestraint.H"
#include "Switch.H"
#include "fileName.H"
#include "DynamicList.H"
#include "CouplingFrame.H"
#include "aitkenRelaxation.hpp"

#include <vector>

namespace Foam
{
namespace RBD
{
namespace restraints
{

/*---------------------------------------------------------------------------*\
                       Class of2Restraint Declaration
\*---------------------------------------------------------------------------*/

class of2Restraint
:
    public restraint
{
    // Private data

        //- OpenFAST primary input file (.fst) path, relative to the case dir
        fileName fstFilePath_;

        //- Body-frame point at the tower base (where OpenFAST's reaction
        //  wrench is applied). For OC4 this is roughly at the top of the
        //  platform / bottom of the tower.
        point towerBasePointBody_;

        //- Body-frame platform reference point (PRP). ED's Sg/Sw/Hv DOFs
        //  describe this point's world displacement relative to its initial
        //  position. Default Zero = body CoR.
        point platformReferencePointBody_;

        //- Whether to print verbose per-step diagnostics
        Switch verbose_;

        //- Initial-condition platform orientation (degrees), ADDED to the
        //  motion-relative Euler angles before they are imposed on OpenFAST.
        //
        //  Why this exists: the IC pose (e.g. +5 deg pitch decay) is baked into
        //  the MESH via transformPoints, but the rigidBodyMotion state X0 is
        //  initialised from the dict `transform` (identity). So the orientation
        //  decomposed from X0 in restrain() is RELATIVE to the as-built IC pose,
        //  not absolute. OpenFAST imposes the platform attitude absolutely, so
        //  feeding it the relative angle made ElastoDyn compute the tower/RNA
        //  overturning moment at the WRONG pitch (0 deg while the hull was at
        //  +5 deg) -> halved tower moment -> wrong pitch equilibrium/decay.
        //  Adding the IC here transfers the ABSOLUTE attitude. Default 0.
        //  (Single-DOF decay ICs only: Euler addition is exact per axis.)
        scalar initialRollDeg_;
        scalar initialPitchDeg_;
        scalar initialYawDeg_;

        //- Initial-condition platform translation (m), ADDED to the motion-
        //  relative Sg/Sw/Hv before they are imposed on OpenFAST. Same
        //  rationale as the initial*Deg_ angles: the displacement derived from
        //  the rigidBodyMotion state is RELATIVE to the as-built IC pose (the
        //  initial world position is latched and subtracted), so the ABSOLUTE
        //  initial translation is otherwise never communicated. For the OC4
        //  tower-base reaction these do not change the result (the wrench is
        //  attitude-driven), but transferring them keeps OpenFAST's imposed
        //  state fully consistent with the IC across all 6 DOF. Default 0.
        scalar initialSurge_;
        scalar initialSway_;
        scalar initialHeave_;


    // Runtime state (mutable: restrain() is const)

        //- Solution time of the last OpenFAST step (for once-per-step gating)
        mutable scalar lastStepTime_;

        //- Whether OpenFAST has been stepped at least once
        mutable bool hasStepped_;

        //- Cached body wrench (about the GLOBAL ORIGIN) from the last step,
        //  reapplied within a CFD step on subsequent restrain() calls.
        mutable spatialVector cachedWrench_;

        //- Unified W<->E<->T frame converter: single owner of the axis map A,
        //  the IC rotation R_IC, the Euler-rate Jacobian and the singular gate.
        //  Configured once in read() via setIC(); see CouplingFrame.H.
        CouplingFrame frame_;

        //- True once FAST_AllocateTurbines + FAST_Sizes + FAST_Start have
        //  completed without aborting. Gates destructor's FAST_End().
        bool fastInitialized_;

        //- OpenFAST's AbortErrLev as reported by FAST_Sizes (= ErrID_Fatal = 4
        //  by default). errStat >= this value from any FAST_* call is fatal.
        int fastAbortErrLev_;

        //- OpenFAST's internal time step (s), reported by FAST_Sizes.
        double fastDT_;

        //- OpenFAST's TMax (s), reported by FAST_Sizes. Set to -1 (unknown)
        //  on a checkpoint restart: FAST_Restart does not report it.
        double fastTMax_;

        //- Checkpoint rootname restored from of2RestraintState ("" = cold
        //  start). When set, initOpenFAST() takes the FAST_Restart branch.
        fileName fastCheckpointRoot_;

        //- Number of OpenFAST checkpoints to retain on disk (FIFO pruning);
        //  0 disables checkpoint writing. Dict key "checkpointKeep".
        label checkpointKeep_;

        //- Rootnames of the checkpoints written this run (pruning FIFO).
        mutable DynamicList<fileName> chkpHistory_;

        //- Number of input channels OpenFAST expects per FAST_Start / FAST_Update.
        //  Set once in initOpenFAST(), then read-only. restrain() takes a
        //  local copy so it can pass &local_int to the C API (which expects
        //  int* even though INTENT(IN) — Fortran ABI quirk).
        int fastNumInputs_;

        //- Number of output channels OpenFAST will fill per FAST_Start / FAST_Update.
        //  Same as fastNumInputs_: read-only after init, restrain() local-copies.
        int fastNumOutputs_;

        //- C-boundary scratch buffer. Conceptually doesn't change between
        //  steps (we always feed dummy zeros) but the FAST_Update C ABI
        //  expects a writable double*, so mutable.
        mutable std::vector<double> fastInputAry_;

        //- Genuine output buffer: FAST_Update writes the channel time-row
        //  here each step. Mutable since restrain() is const but this is
        //  runtime state.
        mutable std::vector<double> fastOutputAry_;

        //- Initial world-frame position of the PRP, captured on the FIRST
        //  restrain() call (NOT in the constructor — the body's dict-specified
        //  transform isn't applied yet at construct time, so bodyPoint()
        //  returns (0,0,0) there). Used to compute ED platform DOFs (Sg, Sw, Hv)
        //  as current_world_PRP - initial_world_PRP.
        mutable point prpInitialWorld_;

        //- Whether prpInitialWorld_ has been captured yet (first-call latch).
        mutable bool prpInitialCaptured_;

        //- Previous-step world-frame linear / angular velocity of the PRP,
        //  used for numerical acceleration (v - v_prev)/dt.
        mutable vector prevLinVelWorld_;
        mutable vector prevAngVelWorld_;

        //- Whether prev velocities have been recorded yet (first-call latch
        //  for the numerical-diff acceleration; first call sends acc=0).
        mutable bool   accInitialized_;

        //- Accumulated CFD wall-time since last FAST_Update fire. Drives the
        //  sub-stepping loop: while accCfdTime_ >= fastDT_, call FAST_Update
        //  again. Keeps OpenFAST's internal time in sync with interFoam's
        //  adaptive wall-clock without forcing interFoam to use a fixed dt.
        mutable double accCfdTime_;

        //- Total FAST_Update fires across the run (diagnostic; differs from
        //  stepCount_ when interFoam dt != fastDT_).
        mutable long   fastUpdateCount_;

        //- Step counter (for logging cadence; first + every N steps).
        mutable long stepCount_;


    // Strong-coupling (implicit FSI) state

        //- Under-relaxation factor (alpha) applied to the tower-base wrench
        //  across the PIMPLE outer correctors of a CFD step. alpha = 1 -> pure
        //  implicit (the latest-corrector wrench is taken as-is); alpha < 1
        //  blends cachedWrench_ = (1-alpha)*old + alpha*new to damp the
        //  added-mass / negative-aero-damping feedback that diverged the
        //  explicit scheme. Default 1.0 (no relaxation); set via dict key
        //  "relaxFactor". When useAitken_ is true (default) this is only the
        //  INITIAL / warm-start factor; the per-corrector factor is then set
        //  adaptively by aitken_ (see below).
        scalar relaxFactor_;

        //- Use Aitken Δ² (Irons-Tuck) dynamic relaxation instead of the fixed
        //  relaxFactor_. The factor is recomputed each outer corrector from the
        //  MOMENT-residual secant, so a stiff added-mass coupling that diverges
        //  at fixed ω converges. Default true. Dict key "useAitken".
        Switch useAitken_;

        //- Aitken ω clamp bounds [omegaMin_, omegaMax_]. The adaptive factor is
        //  clamped here for robustness (omegaMin avoids stalling; omegaMax caps
        //  the implicit step). Chow & Ng (2016) / Wang et al. restrict ω to
        //  [0.1, 1]; default omegaMin_ = 0.1. Dict keys "omegaMin"/"omegaMax".
        scalar omegaMin_;
        scalar omegaMax_;

        //- FSI convergence tolerance on the normalized residual rho =
        //  max(|dF|/(m_s g), |dM|/|M|) (eqs 43-45 of Wang et al.). Once rho <
        //  fsiTol_ within a CFD step, the wrench is held for the remaining outer
        //  correctors (a soft convergence gate — the partitioned iterate has
        //  settled, so further updates only add OpenFAST-rollback noise). Dict
        //  key "fsiTol"; default 1e-3.
        scalar fsiTol_;

        //- Per-step latch: set true once rho < fsiTol_ this CFD step; reset on
        //  each new step. While true, cachedWrench_ is held (gate closed).
        mutable bool fsiConverged_;

        //- Aitken dynamic-relaxation engine. Driven on the tower-base MOMENT
        //  residual (3 consistent N·m components — the pitch moment is the
        //  divergent quantity); the resulting scalar ω is applied to the full
        //  6-DOF wrench. Configured in read() from relaxFactor_/omega{Min,Max}_.
        //  Dependency-free (aitkenRelaxation.hpp) so it is unit-tested with g++.
        mutable of2::AitkenRelaxation aitken_;

        //- Outer-corrector index within the current CFD step: 1 on the first
        //  corrector (snapshots OpenFAST + advances), incremented on each
        //  repeat corrector (rolls OpenFAST back + re-advances with the updated
        //  platform motion). Diagnostic / logging.
        mutable label outerCorr_;

        //- Number of FAST_Update fires during the current CFD step's first
        //  corrector. Each repeat corrector rolls back exactly this many
        //  OpenFAST sub-steps (FAST_CFD_Reset_SubStep n_timesteps) before
        //  re-advancing. 0 => OpenFAST did not advance this CFD step (dt <
        //  fastDT, wrench held) => no rollback, strong coupling idle this step.
        mutable label firedThisStep_;

        //- accCfdTime_ value at the start of the current CFD step, restored on
        //  each repeat corrector so the sub-step while-loop re-fires the
        //  identical number of OpenFAST steps deterministically.
        mutable double accCfdTimeStepStart_;

        //- Cosmetic step index handed to FAST_CFD_Store_SubStep (the Fortran
        //  routine computes but never uses t_global from it; correctness rests
        //  on FAST_CFD_Reset_SubStep using the live module n_t_global).
        mutable int fastStepIdx_;


    // Private member functions

        //- Allocate the OpenFAST turbine, read the .fst file, run init.
        //  Phase B: errors abort with Foam::FatalError.
        void initOpenFAST();

        //- Shut down OpenFAST cleanly (no-op if fastInitialized_ == false).
        void endOpenFAST();

        //- Restore persisted coupling state from
        //  <startTime>/uniform/of2RestraintState (restart support, phase 2a).
        //  Master only; no-op when the file is absent (fresh run). Restores
        //  prpInitialWorld_ (the ED Sg/Sw/Hv displacement reference — the
        //  critical restart trap: re-latching it at the restart pose would
        //  silently zero the imposed platform displacement), accCfdTime_
        //  (sub-step commensurability), cachedWrench_ (held-wrench case) and
        //  the acceleration-diff latches.
        void restoreRestartState();

        //- Persist the coupling state to <t>/uniform/of2RestraintState.
        //  Called at the end of every master restrain() of a write-time step;
        //  the last outer corrector's values persist, consistent with the
        //  end-of-step fluid/body snapshot.
        void writeRestartState() const;


public:

    //- Runtime type information
    TypeName("OpenFast");


    // Constructors

        of2Restraint
        (
            const word& name,
            const dictionary& dict,
            const rigidBodyModel& model
        );

        //- Copy constructor (preserves configuration; resets runtime state).
        of2Restraint(const of2Restraint& other);

        //- Construct and return a clone
        virtual autoPtr<restraint> clone() const
        {
            return autoPtr<restraint>(new of2Restraint(*this));
        }


    //- Destructor
    virtual ~of2Restraint();


    // Member Functions

        //- Accumulate the tower-base wrench into the external-force field.
        //  Phase A: no-op (returns zero). Phase B+C wire the OpenFAST driving.
        virtual void restrain
        (
            scalarField& tau,
            Field<spatialVector>& fx,
            const rigidBodyModelState& state
        ) const;

        //- Update properties from given dictionary
        virtual bool read(const dictionary& dict);

        //- Write
        virtual void write(Ostream&) const;
};


} // End namespace restraints
} // End namespace RBD
} // End namespace Foam

#endif

// ************************************************************************* //
