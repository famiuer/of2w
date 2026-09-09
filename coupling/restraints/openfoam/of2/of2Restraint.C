/*---------------------------------------------------------------------------*\
    OC4 OF2-method OpenFAST coupling restraint (RBD framework).

    Phase B: OpenFAST init via FAST_* C ABI in constructor; FAST_End in
    destructor. restrain() still no-op (Phase C wires it). Hardening added:
      * FpeGuard RAII wrap around every FAST_* call (allows running with
        FOAM_SIGFPE=1, the OpenFOAM default).
      * Single-restraint-per-case guard (OpenFAST has one global Turbine(1)
        slot; multiple OpenFast restraints would corrupt it).
      * Pstream::master() gating scaffold (Phase D parallel safety).
\*---------------------------------------------------------------------------*/

#include "of2Restraint.H"

#include "rigidBodyModel.H"
#include "addToRunTimeSelectionTable.H"
#include "OSspecific.H"   // Info / WrScr
#include "Pstream.H"      // Pstream::master, Pstream::scatter (Phase D)
#include "Time.H"         // restart state IO (model_.time() is fwd-declared)
#include "IOdictionary.H" // of2RestraintState read/write
#include "OSspecific.H"   // isFile / rm (checkpoint validation + pruning)

// _GNU_SOURCE is already supplied by the OpenFOAM build flags, exposing
// glibc's feenableexcept / fedisableexcept / fegetexcept inside <cfenv>.
#include <cfenv>
#include <cstring>
#include <cstdlib>

// -------------------------------------------------------------------------- //
// FAST_* C ABI declarations come from the canonical OpenFAST header. This
// avoids hand-maintaining a parallel set of constants/prototypes that could
// silently desync from the libopenfastlib.so we link against.
//
// In particular, NumFixedInputs is defined here as
//   2 + 2 + MAXIMUM_BLADES + 1 + MAXIMUM_AFCTRL
//         + MAXIMUM_CABLE_DELTAL + MAXIMUM_CABLE_DELTALDOT
// (currently 51); if any of those upstream MAXIMUMs change in a future
// OpenFAST, we automatically pick up the new value. The previous
// hand-coded `51` would desync silently and cause FAST_Update to return
// ErrID_Fatal=4 without advancing OpenFAST state.
//
// FAST_Library.h provides (inside extern "C"): FAST_AllocateTurbines,
// FAST_Sizes (with default = NULL for trailing OPTIONAL args -- see the
// project memory on Fortran OPTIONAL/C++ default-arg interop), FAST_Start,
// FAST_Update, FAST_End, plus the constants we need:
//   INTERFACE_STRING_LENGTH   (string-buffer length for paths / errMsg)
//   CHANNEL_LENGTH            (width of one channel-name column)
//   MAXIMUM_OUTPUTS           (max output channels)
//   NumFixedInputs            (sum derived from MAXIMUM_BLADES, MAXIMUM_AFCTRL,
//                              MAXIMUM_CABLE_DELTAL, MAXIMUM_CABLE_DELTALDOT)
//   ErrID_None .. ErrID_Fatal (severity levels)
// -------------------------------------------------------------------------- //
#include "FAST_Library.h"

namespace
{
    // Size of the ChannelNames buffer FAST_Sizes fills (one C string with each
    // name padded to CHANNEL_LENGTH chars + a trailing NUL).
    constexpr int OF2_ChannelNamesBufSize  = CHANNEL_LENGTH * MAXIMUM_OUTPUTS + 1;

    // ----------------------------------------------------------------------
    // RAII guard: disable FE_INVALID/FE_DIVBYZERO/FE_OVERFLOW traps for the
    // duration of an OpenFAST call, then restore the host process's trap mask.
    // OpenFAST init/step does benign FP ops (subnormal compares, NaN guards)
    // that OpenFOAM's default FOAM_SIGFPE=1 catches as fatal. The guard
    // scopes the relaxation tightly so OpenFOAM-side FP bugs still get caught.
    // Exception-safe: destructor restores even on throw.
    //
    // fegetexcept / fedisableexcept / feenableexcept are glibc extensions
    // (Linux-only). Standard C99 fenv.h has only the FLAG manipulation calls
    // (fegetexceptflag etc.), not the TRAP MASK ones we need here.
    // ----------------------------------------------------------------------
    class FpeGuard
    {
        int saved_excepts_;
    public:
        FpeGuard()
        :
            saved_excepts_(fegetexcept())
        {
            fedisableexcept(FE_ALL_EXCEPT);
        }
        ~FpeGuard()
        {
            // Clear any FE flags raised inside the wrapped call; otherwise
            // subsequent fenv queries would see leftover state.
            std::feclearexcept(FE_ALL_EXCEPT);
            if (saved_excepts_ > 0)
            {
                feenableexcept(saved_excepts_);
            }
        }
        FpeGuard(const FpeGuard&) = delete;
        FpeGuard& operator=(const FpeGuard&) = delete;
    };

    // ----------------------------------------------------------------------
    // Coordinate-system conversion: OpenFOAM to ED-internal.
    //
    // OF uses (x downwind, y lateral, z up). ED uses an internal storage
    // basis where:    z1 = OF_x,   z2 = OF_z,   z3 = -OF_y.
    // (see SetCoordSy in ElastoDyn.f90 around line 6301; comments label
    // these as IEC xi/zi/-yi.) The frame-change matrix is:
    //
    //   P = | 1  0  0 |   such that  v_ED = P * v_OF
    //       | 0  0  1 |
    //       | 0 -1  0 |
    //
    // P is orthogonal (P^T = P^-1) and proper (det = +1).
    //
    // ED constructs the platform rotation matrix R_ED^(inertial<-body) from
    // (R, P_pitch, Y) DOFs via intrinsic yaw->pitch->roll about its own basis
    // (z2 / alpha3 / beta1). Code at SetCoordSy lines 6317-6329 gives:
    //
    //   R_ED[0][0] =  cos(P)cos(Y)
    //   R_ED[1][0] = -sin(P)
    //   R_ED[1][1] =  cos(R)cos(P)
    //   R_ED[1][2] = -sin(R)cos(P)
    //   R_ED[2][0] = -cos(P)sin(Y)
    //   ... etc
    //
    // To extract (R, P_pitch, Y) from an arbitrary R_ED (no gimbal lock
    // since the OC4 case stays in small-pitch territory):
    //
    //   P_pitch = asin(-R_ED[1][0])                  (in [-pi/2, pi/2])
    //   R       = atan2(-R_ED[1][2], R_ED[1][1])     (cos(P) != 0)
    //   Y       = atan2(-R_ED[2][0], R_ED[0][0])     (cos(P) != 0)
    //
    // Hand-verified for pure 5-deg pitch: input M_of = R_y(5deg, right-hand)
    // -> R_ED = P*M*P^T as derived -> decomposition returns (0, 5deg, 0).
    // ----------------------------------------------------------------------
    inline Foam::tensor frameOfToEdRotation(const Foam::tensor& M_of)
    {
        // Build P explicitly. tensor() ctor: row-major (xx, xy, xz, yx, ...).
        static const Foam::tensor P(
             1, 0,  0,
             0, 0,  1,
             0,-1,  0
        );
        // P^T:
        static const Foam::tensor PT(
             1, 0,  0,
             0, 0, -1,
             0, 1,  0
        );
        return (P & M_of) & PT;
    }

    // Decompose an ED rotation matrix into (R, P, Y) Euler angles (radians).
    // Returns true on success; false if cos(P) is near zero (gimbal lock).
    inline bool decomposeEdEulerRPY
    (
        const Foam::tensor& R_ED,
        double& R, double& P, double& Y
    )
    {
        const double r10 = R_ED.yx();   // row 1, col 0
        const double r11 = R_ED.yy();
        const double r12 = R_ED.yz();
        const double r00 = R_ED.xx();
        const double r20 = R_ED.zx();

        // Clamp to [-1, 1] in case of fp noise on -r10.
        double sinP = -r10;
        if (sinP >  1.0) sinP =  1.0;
        if (sinP < -1.0) sinP = -1.0;
        P = std::asin(sinP);
        const double cosP = std::cos(P);

        if (std::fabs(cosP) < 1.0e-9)
        {
            return false;   // gimbal lock; caller should not hit this for OC4
        }
        R = std::atan2(-r12, r11);
        Y = std::atan2(-r20, r00);
        return true;
    }
}

// FAST_* C declarations are provided by FAST_Library.h (included above).
// We only need to declare the OF2_* bridge symbols (those are exported by
// PMI_Bridge.f90 — separate from the FAST_* family).
extern "C" {
    void OF2_SetImposedPlatformState(const double disp[6],
                                     const double vel[6],
                                     const double acc[6]);
    void OF2_GetTowerBaseReaction(double F[3], double M[3]);
}

// * * * * * * * * * * * * * * Static Data Members * * * * * * * * * * * * * //

namespace Foam
{
namespace RBD
{
namespace restraints
{
    defineTypeNameAndDebug(of2Restraint, 0);

    addToRunTimeSelectionTable
    (
        restraint,
        of2Restraint,
        dictionary
    );
}
}
}


// * * * * * * * * * * * * * * * * Constructors  * * * * * * * * * * * * * * //

Foam::RBD::restraints::of2Restraint::of2Restraint
(
    const word& name,
    const dictionary& dict,
    const rigidBodyModel& model
)
:
    restraint(name, dict, model),
    fstFilePath_(),
    towerBasePointBody_(Zero),
    platformReferencePointBody_(Zero),
    verbose_(false),
    initialRollDeg_(0),
    initialPitchDeg_(0),
    initialYawDeg_(0),
    initialSurge_(0),
    initialSway_(0),
    initialHeave_(0),
    lastStepTime_(-GREAT),
    hasStepped_(false),
    cachedWrench_(Zero),
    fastInitialized_(false),
    fastAbortErrLev_(4),    // default = ErrID_Fatal; overwritten by FAST_Sizes
    fastDT_(0.0),
    fastTMax_(0.0),
    fastCheckpointRoot_(),
    checkpointKeep_(3),
    chkpHistory_(),
    fastNumInputs_(0),
    fastNumOutputs_(0),
    fastInputAry_(),
    fastOutputAry_(),
    prpInitialWorld_(Zero),
    prpInitialCaptured_(false),
    prevLinVelWorld_(Zero),
    prevAngVelWorld_(Zero),
    accInitialized_(false),
    accCfdTime_(0.0),
    fastUpdateCount_(0),
    stepCount_(0),
    relaxFactor_(1.0),
    useAitken_(true),
    omegaMin_(0.1),
    omegaMax_(1.0),
    fsiTol_(1.0e-3),
    fsiConverged_(false),
    outerCorr_(0),
    firedThisStep_(0),
    accCfdTimeStepStart_(0.0),
    fastStepIdx_(0)
{
    read(dict);
    // NOTE: we CANNOT capture prpInitialWorld_ here. At restraint-construction
    // time the body's dict-specified `transform (... ... ...)` hasn't been
    // applied yet, so bodyPoint() returns (0, 0, 0). prpInitialWorld_ is
    // captured on the first restrain() call instead, when the body sits at
    // its initial physical pose.
    Info<< " [OF2] of2Restraint '" << name << "' constructed."
        << " fstFile=" << fstFilePath_
        << " towerBasePoint=" << towerBasePointBody_
        << endl;
    restoreRestartState();
    initOpenFAST();
}


Foam::RBD::restraints::of2Restraint::of2Restraint
(
    const of2Restraint& other
)
:
    restraint(other),
    fstFilePath_(other.fstFilePath_),
    towerBasePointBody_(other.towerBasePointBody_),
    platformReferencePointBody_(other.platformReferencePointBody_),
    verbose_(other.verbose_),
    initialRollDeg_(other.initialRollDeg_),
    initialPitchDeg_(other.initialPitchDeg_),
    initialYawDeg_(other.initialYawDeg_),
    initialSurge_(other.initialSurge_),
    initialSway_(other.initialSway_),
    initialHeave_(other.initialHeave_),
    lastStepTime_(-GREAT),
    hasStepped_(false),
    cachedWrench_(Zero),
    // The OpenFAST runtime is single-instance (FAST_AllocateTurbines(1)) and
    // already owned by `other`. Clones must NOT re-init or re-end. They share
    // the single Turbine(1) slot through the FAST_Library module state.
    fastInitialized_(false),
    fastAbortErrLev_(other.fastAbortErrLev_),
    fastDT_(other.fastDT_),
    fastTMax_(other.fastTMax_),
    fastCheckpointRoot_(other.fastCheckpointRoot_),
    checkpointKeep_(other.checkpointKeep_),
    chkpHistory_(),
    fastNumInputs_(other.fastNumInputs_),
    fastNumOutputs_(other.fastNumOutputs_),
    fastInputAry_(other.fastInputAry_),
    fastOutputAry_(other.fastOutputAry_),
    prpInitialWorld_(other.prpInitialWorld_),
    prpInitialCaptured_(other.prpInitialCaptured_),
    // Runtime accel/sub-step state is per-instance; clones start fresh so
    // their numerical-diff doesn't blend with the original's history.
    prevLinVelWorld_(Zero),
    prevAngVelWorld_(Zero),
    accInitialized_(false),
    accCfdTime_(0.0),
    fastUpdateCount_(0),
    stepCount_(0),
    relaxFactor_(other.relaxFactor_),
    useAitken_(other.useAitken_),
    omegaMin_(other.omegaMin_),
    omegaMax_(other.omegaMax_),
    fsiTol_(other.fsiTol_),
    fsiConverged_(false),
    outerCorr_(0),
    firedThisStep_(0),
    accCfdTimeStepStart_(0.0),
    fastStepIdx_(0)
{
    // Clone does not call read(); carry the parent's Aitken configuration so a
    // cloned instance under-relaxes identically (runtime ω history starts fresh).
    aitken_.configure(relaxFactor_, omegaMin_, omegaMax_);
    Info<< " [OF2] of2Restraint cloned (clone does NOT re-init OpenFAST)."
        << endl;
}


// * * * * * * * * * * * * * * * * Destructor  * * * * * * * * * * * * * * * //

Foam::RBD::restraints::of2Restraint::~of2Restraint()
{
    endOpenFAST();
}


// * * * * * * * * * * * * * * * Init / cleanup helpers  * * * * * * * * * * //

void Foam::RBD::restraints::of2Restraint::initOpenFAST()
{
    if (fastInitialized_)
    {
        return;
    }

    // Pre-Phase-C harden #3: only the MPI master rank drives OpenFAST.
    // Other ranks treat the restraint as inert at init (the wrench they
    // receive in restrain() will be scattered from master via Pstream).
    // For NP=1 (Phase B test) this is always true - no behavior change.
    if (!Pstream::master())
    {
        Info<< " [OF2] non-master rank, skipping OpenFAST init." << endl;
        return;
    }

    // Pre-Phase-C harden #2: OpenFAST uses a single global Turbine(1) slot
    // (set by FAST_AllocateTurbines(1)). A second OpenFast restraint in the
    // same case would corrupt it. Catch that case cleanly here.
    static bool s_alreadyAllocated = false;
    if (s_alreadyAllocated)
    {
        FatalErrorIn("of2Restraint::initOpenFAST()")
            << "Multiple OpenFast restraints in a single case are not "
            << "supported - OpenFAST has one global Turbine(1) slot. "
            << "Remove the extra `type OpenFast;` block from "
            << "dynamicMeshDict's restraints { ... }."
            << abort(FatalError);
    }

    int errStat = 0;
    char errMsg[INTERFACE_STRING_LENGTH];
    std::memset(errMsg, 0, sizeof(errMsg));

    // 1. Allocate 1 turbine slot.
    int nTurbines = 1;
    {
        FpeGuard g;   // OpenFAST allocator does benign FP ops; trap-safe.
        FAST_AllocateTurbines(&nTurbines, &errStat, errMsg);
    }
    if (errStat > 0)
    {
        FatalErrorIn("of2Restraint::initOpenFAST()")
            << "FAST_AllocateTurbines failed (ErrStat=" << errStat
            << "): " << errMsg << abort(FatalError);
    }
    Info<< " [OF2] FAST_AllocateTurbines(1) -> ok" << endl;

    // ---- RESTART branch (phase 2b) --------------------------------------
    // A checkpoint rootname was restored from of2RestraintState:
    // FAST_Restart replaces FAST_Sizes + FAST_Start entirely — it reads the
    // binary <root>.chkp and restores ALL module states (ED/AD/SrvD/tower,
    // rotor azimuth, n_t_global), returning the sizing info FAST_Sizes
    // would have provided (except TMax, which the checkpoint doesn't carry).
    if (!fastCheckpointRoot_.empty())
    {
        char rootBuf[INTERFACE_STRING_LENGTH];
        std::memset(rootBuf, 0, sizeof(rootBuf));
        const std::string rootStr = fastCheckpointRoot_;
        if (rootStr.size() + 1 >= INTERFACE_STRING_LENGTH)
        {
            FatalErrorIn("of2Restraint::initOpenFAST()")
                << "checkpoint rootname too long: " << fastCheckpointRoot_
                << abort(FatalError);
        }
        std::strcpy(rootBuf, rootStr.c_str());

        int    iTurbR       = 0;
        int    abortErrLevR = 0;
        int    numOutsR     = 0;
        double dtR          = 0.0;
        int    nTGlobalR    = 0;
        std::memset(errMsg, 0, sizeof(errMsg));
        {
            FpeGuard g;
            FAST_Restart(
                &iTurbR,
                rootBuf,
                &abortErrLevR,
                &numOutsR,
                &dtR,
                &nTGlobalR,
                &errStat,
                errMsg
            );
        }
        if (errStat >= abortErrLevR && abortErrLevR > 0)
        {
            FatalErrorIn("of2Restraint::initOpenFAST()")
                << "FAST_Restart failed (ErrStat=" << errStat
                << "): " << errMsg << nl
                << "  checkpoint = " << fastCheckpointRoot_ << ".chkp"
                << abort(FatalError);
        }

        fastDT_          = dtR;
        fastTMax_        = -1;              // not reported by FAST_Restart
        fastAbortErrLev_ = abortErrLevR;
        fastNumOutputs_  = numOutsR;
        fastNumInputs_   = NumFixedInputs;  // see the FAST_Update ABI note below
        fastInputAry_.assign(fastNumInputs_,  0.0);
        fastOutputAry_.assign(fastNumOutputs_, 0.0);

        Info<< " [OF2] FAST_Restart -> ok."
            << " checkpoint=" << fastCheckpointRoot_ << ".chkp"
            << " DT=" << fastDT_
            << " NumOuts=" << numOutsR
            << " n_t_global=" << nTGlobalR
            << " (OpenFAST clock t=" << nTGlobalR*fastDT_ << " s)" << endl;

        // Do NOT call FAST_CFD_InitIOarrays_SubStep here. The checkpoint has
        // already restored the sub-step IO/state save slots; re-initialising
        // them does MESH_NEWCOPY onto the restored meshes and corrupts the
        // turbine state — ElastoDyn then returns NaN tower-base loads on the
        // first FAST_Update (relay legs 18255077-79). The reference
        // implementation (openfast-cpp OpenFAST.cpp, fast::trueRestart) also
        // omits it on restart; it belongs to the cold-start path only.

        fastInitialized_   = true;
        s_alreadyAllocated = true;
        return;
    }

    // 2. Pass the .fst path to FAST_Sizes — must be NUL-terminated and
    //    sized to IntfStrLen. The path is relative to the case directory
    //    (OpenFAST resolves child paths from its current working dir).
    char fstNameBuf[INTERFACE_STRING_LENGTH];
    std::memset(fstNameBuf, 0, sizeof(fstNameBuf));
    const std::string fstStr = fstFilePath_;
    if (fstStr.size() + 1 >= INTERFACE_STRING_LENGTH)
    {
        FatalErrorIn("of2Restraint::initOpenFAST()")
            << "fstFile path is too long (" << fstStr.size()
            << " >= " << (INTERFACE_STRING_LENGTH - 1) << " chars): "
            << fstFilePath_ << abort(FatalError);
    }
    std::strcpy(fstNameBuf, fstStr.c_str());

    int iTurb_c = 0;          // C-side 0-based turbine index
    int abortErrLev = 0;
    int numOuts = 0;
    double dt = 0.0, dtOut = 0.0, tmax = 0.0;
    std::vector<char> channelNames(OF2_ChannelNamesBufSize, 0);

    std::memset(errMsg, 0, sizeof(errMsg));
    {
        FpeGuard g;   // ED + module inits raise benign FE flags.
        FAST_Sizes(
            &iTurb_c,
            fstNameBuf,
            &abortErrLev,
            &numOuts,
            &dt,
            &dtOut,
            &tmax,
            &errStat,
            errMsg,
            channelNames.data()
            // OPTIONAL trailing args (TMax, InitInpAry) deliberately omitted -
            // see ABI block above for rationale.
        );
    }
    if (errStat >= abortErrLev && abortErrLev > 0)
    {
        FatalErrorIn("of2Restraint::initOpenFAST()")
            << "FAST_Sizes failed (ErrStat=" << errStat
            << ", AbortErrLev=" << abortErrLev << "): "
            << errMsg << nl
            << "  fstFile = " << fstFilePath_ << abort(FatalError);
    }
    fastDT_          = dt;
    fastTMax_        = tmax;
    fastAbortErrLev_ = abortErrLev;   // cache for use in restrain() error checks
    // FAST_Sizes returns numOuts = SIZE(y_FAST%ChannelNames). FAST_Start
    // requires NumOutputs_c == SIZE(ChannelNames), so they're the same value.
    // OutputAry is then allocated to numOuts doubles: OutputAry[0] holds the
    // time channel and OutputAry[1..numOuts-1] hold the named channels
    // (see FAST_Library.f90 line 237-239: OutputAry(1)=t_global, then
    // OutputAry(2:NumOutputs_c) = Outputs). Confirmed by FastLibAPI which
    // passes num_outs directly as the NumOutputs argument.
    fastNumOutputs_ = numOuts;
    // FAST_Update REQUIRES NumInputs_c == NumFixedInputs (=51) or
    // NumFixedInputs+3 (=54) - see FAST_Library.f90 line 310. With wrong
    // size, FAST_Update silently returns ErrID_Fatal=4 without advancing
    // OpenFAST state - a bug that bit us in Phase C Stage-1 testing.
    // FAST_Start doesn't validate NumInputs_c, so we set the right value
    // here too for consistency. Contents are Simulink-style external control
    // inputs (gen torque, blade-pitch commands, ...). With CompServo=0 in
    // our deck OpenFAST ignores them; zeros are fine.
    fastNumInputs_  = NumFixedInputs;   // from FAST_Library.h; currently 51
    fastInputAry_.assign(fastNumInputs_,  0.0);
    fastOutputAry_.assign(fastNumOutputs_, 0.0);

    Info<< " [OF2] FAST_Sizes -> ok. DT=" << fastDT_
        << " TMax=" << fastTMax_
        << " NumOuts=" << numOuts
        << " AbortErrLev=" << abortErrLev << endl;

    // 3. FAST_Start: solve at t=0, fills first row of OutputAry.
    std::memset(errMsg, 0, sizeof(errMsg));
    {
        FpeGuard g;
        FAST_Start(
            &iTurb_c,
            &fastNumInputs_,
            &fastNumOutputs_,
            fastInputAry_.data(),
            fastOutputAry_.data(),
            &errStat,
            errMsg
        );
    }
    if (errStat >= abortErrLev && abortErrLev > 0)
    {
        FatalErrorIn("of2Restraint::initOpenFAST()")
            << "FAST_Start failed (ErrStat=" << errStat
            << "): " << errMsg << abort(FatalError);
    }

    Info<< " [OF2] FAST_Start -> ok. t_initial=" << fastOutputAry_[0]
        << " (first output = time channel)" << endl;

    // 4. Allocate the in-memory sub-step SAVED state slots, so restrain() can
    //    snapshot (FAST_CFD_Store_SubStep) and roll back (FAST_CFD_Reset_SubStep)
    //    OpenFAST between PIMPLE outer correctors for STRONG (implicit) coupling.
    //    Must follow FAST_Start, which populates the CURR states this copies
    //    from. These are the SOWFA/AMR-Wind sub-step C hooks (FAST_Library.h).
    {
        int  errStatSS = 0;
        char errMsgSS[INTERFACE_STRING_LENGTH];
        std::memset(errMsgSS, 0, sizeof(errMsgSS));
        int  iTurbSS = 0;
        {
            FpeGuard g;
            FAST_CFD_InitIOarrays_SubStep(&iTurbSS, &errStatSS, errMsgSS);
        }
        if (errStatSS >= fastAbortErrLev_ && fastAbortErrLev_ > 0)
        {
            FatalErrorIn("of2Restraint::initOpenFAST()")
                << "FAST_CFD_InitIOarrays_SubStep failed (ErrStat="
                << errStatSS << "): " << errMsgSS << nl
                << "  This OpenFAST build may predate the sub-step C ABI "
                << "(FAST_CFD_*_SubStep). Strong (implicit) coupling requires it."
                << abort(FatalError);
        }
        Info<< " [OF2] FAST_CFD_InitIOarrays_SubStep -> ok"
            << " (per-corrector rollback enabled)" << endl;
    }

    fastInitialized_     = true;
    s_alreadyAllocated   = true;   // bind the single-instance guard
}


void Foam::RBD::restraints::of2Restraint::endOpenFAST()
{
    if (!fastInitialized_)
    {
        return;
    }
    // Only master rank ran init -> only master calls End. Non-masters never
    // reach here because fastInitialized_ stays false on them.
    int iTurb_c = 0;
    bool stopTheProgram = false;   // we keep the OpenFOAM process alive
    {
        FpeGuard g;   // FAST_End writes summary files; benign FP ops.
        FAST_End(&iTurb_c, &stopTheProgram);
    }
    Info<< " [OF2] FAST_End() -> done." << endl;
    fastInitialized_ = false;
}


// * * * * * * * * * * * * * * * Member functions  * * * * * * * * * * * * * //

void Foam::RBD::restraints::of2Restraint::restrain
(
    scalarField& tau,
    Field<spatialVector>& fx,
    const rigidBodyModelState& state
) const
{
    (void)tau;   // unused; we apply via fx wrench, not joint torques

    // Per-step driving sequence (MASTER-ONLY pattern, mirrors moorDynR2):
    //   1. Non-master returns immediately. The rigidBody solver integrates
    //      body state on master and the resulting kinematics are propagated
    //      to other ranks via OpenFOAM's internal mesh-motion sync. Our
    //      contribution to fx[] only needs to land on master.
    //   2. Master: once-per-CFD-step gate; advance OpenFAST; build cachedWrench_
    //   3. Master: fx[bodyIndex_] += cachedWrench_

    if (!Pstream::master() || !fastInitialized_)
    {
        return;
    }

    const scalar t  = state.t();
    const scalar dt = state.deltaT();

    // Strong (implicit) coupling gate -------------------------------------
    // RBD calls restrain() on EVERY PIMPLE outer corrector (the case sets
    // moveMeshOuterCorrectors yes; verified overInterDyMFoam.C:136 ->
    // mesh.update() -> rigidBodyMeshMotion::solve() whose model_.solve() runs
    // OUTSIDE the curTimeIndex_ gate, so the body re-integrates each corrector).
    // We re-fire OpenFAST each corrector against the CONVERGING platform
    // motion: the FIRST corrector of a CFD step snapshots OpenFAST
    // (FAST_CFD_Store_SubStep) then advances; each REPEAT corrector rolls back
    // to that snapshot (FAST_CFD_Reset_SubStep) then re-advances with the
    // updated motion. The last corrector's advance persists (never rolled back)
    // and becomes the next step's snapshot, so the OpenFAST clock stays
    // balanced. The wrench is under-relaxed across correctors (relaxFactor_).
    if (dt > SMALL)
    {
        const bool newStep = (!hasStepped_ || t > lastStepTime_ + 0.5*dt);

        // 3. body state in world frame -------------------------------------
        const point  prpWorld = bodyPoint(platformReferencePointBody_);

        // First-call latch: capture the PRP's initial world position now
        // (the body has been positioned by its dict transform; we couldn't
        // get this in the constructor — bodyPoint() returned (0,0,0) there).
        if (!prpInitialCaptured_)
        {
            prpInitialWorld_    = prpWorld;
            prpInitialCaptured_ = true;
            Info<< " [OF2] PRP_initial(world) captured at t=" << t
                << ": " << prpInitialWorld_ << endl;
        }

        const spatialVector vSpat = bodyPointVelocity(platformReferencePointBody_);
        const vector linVelWorld = vSpat.l();
        const vector angVelWorld = vSpat.w();  // body angular velocity in world frame

        // OF body MOTION rotation: model_.X0(body).E() = R_body<-world;
        // transpose gives R_world<-body (motion only). The IC lives in the
        // pre-rotated mesh; CouplingFrame composes it (R_WB = R_IC . R_X0).
        const tensor R_of_inertialFromBody = model_.X0(bodyID_).E().T();

        // Acceleration from RBD's integrator state, transformed to world at the
        // PRP. Featherstone bakes gravity into the root link's spatial accel
        // (forwardDynamics.C:176): a body at apparent rest returns linear.z()
        // ~ +g, so add model_.g() back to recover the classical acceleration.
        // On the FIRST restrain() call of a run (cold start or restart)
        // model_.a() has not been produced by a forwardDynamics solve yet and
        // is identically zero; the +g correction would then impose a spurious
        // free-fall acceleration of -9.81 m/s^2 on OpenFAST (seen in relay
        // legs 18255077-79). Send zero classical acceleration instead.
        const spatialVector aSpat
        (
            spatialTransform(R_of_inertialFromBody, prpWorld)
          & model_.a(bodyID_)
        );
        const vector linAccWorld =
            accInitialized_ ? vector(aSpat.l() + model_.g()) : vector(Zero);
        const vector angAccWorld = accInitialized_ ? aSpat.w() : vector(Zero);
        prevLinVelWorld_ = linVelWorld;
        prevAngVelWorld_ = angVelWorld;
        accInitialized_  = true;

        // Unified frame conversion W -> E (single source of truth). CouplingFrame
        // owns A, R_IC, the Euler-rate Jacobian and the singular gate. dispWorld
        // is the PtfmRef motion displacement; the IC translation is added inside.
        const vector dispWorld = prpWorld - prpInitialWorld_;
        double disp[6], vel[6], acc[6];
        if
        (
           !frame_.motionToModule
            (
                R_of_inertialFromBody, dispWorld,
                angVelWorld, linVelWorld, angAccWorld, linAccWorld,
                disp, vel, acc
            )
        )
        {
            FatalErrorIn("of2Restraint::restrain()")
                << "Singular platform pose (|cos(pitch)| < 1e-6) at t=" << t
                << " - gimbal lock / wrong physics. Aborting."
                << abort(FatalError);
        }

        // --------------------------------------------------------------------
        // STRONG-COUPLING snapshot / rollback. On the first corrector of a CFD
        // step, snapshot OpenFAST's start-of-step state. On each repeat
        // corrector, roll OpenFAST back to that snapshot (exactly the fires we
        // did this step) before re-advancing below with the updated motion.
        // --------------------------------------------------------------------
        const point twrBsGlobal = bodyPoint(towerBasePointBody_);

        int  errStatC = 0;
        char errMsgC[INTERFACE_STRING_LENGTH];
        int  iTurbC   = 0;

        if (newStep)
        {
            // Snapshot the start-of-step state. (The previous step's last
            // corrector advance is the current state — exactly what we want to
            // return to should this step take repeat correctors.)
            std::memset(errMsgC, 0, sizeof(errMsgC));
            {
                FpeGuard g;
                FAST_CFD_Store_SubStep(&iTurbC, &fastStepIdx_, &errStatC, errMsgC);
            }
            if (errStatC >= fastAbortErrLev_ && fastAbortErrLev_ > 0)
            {
                FatalErrorIn("of2Restraint::restrain()")
                    << "FAST_CFD_Store_SubStep failed at t=" << t
                    << " (ErrStat=" << errStatC << "): " << errMsgC
                    << abort(FatalError);
            }
            outerCorr_           = 1;
            accCfdTimeStepStart_ = accCfdTime_;
        }
        else
        {
            // Repeat outer corrector. Roll OpenFAST back to the step-start
            // snapshot if it advanced this step; if it did not (firedThisStep_
            // == 0, CFD dt < fastDT), there is nothing to roll back and the
            // held wrench is simply re-applied below.
            if (firedThisStep_ > 0)
            {
                int nBack = int(firedThisStep_);
                std::memset(errMsgC, 0, sizeof(errMsgC));
                {
                    FpeGuard g;
                    FAST_CFD_Reset_SubStep(&iTurbC, &nBack, &errStatC, errMsgC);
                }
                if (errStatC >= fastAbortErrLev_ && fastAbortErrLev_ > 0)
                {
                    FatalErrorIn("of2Restraint::restrain()")
                        << "FAST_CFD_Reset_SubStep failed at t=" << t
                        << " (nBack=" << nBack << ", ErrStat=" << errStatC
                        << "): " << errMsgC << abort(FatalError);
                }
            }
            // Restore the accumulator so the sub-step loop re-fires the same
            // number of OpenFAST steps deterministically.
            accCfdTime_ = accCfdTimeStepStart_;
            ++outerCorr_;
        }

        // --------------------------------------------------------------------
        // Advance OpenFAST: sub-step while the accumulated CFD time exceeds one
        // fastDT_. With dt == fastDT this fires exactly once per corrector; the
        // loop also covers dt > fastDT (multiple fires, all rolled back
        // together on the next corrector via firedThisStep_) and dt < fastDT
        // (no fire — the wrench is held). Each iteration pushes the CURRENT
        // (this-corrector) body state and reads the tower-base wrench back.
        // --------------------------------------------------------------------
        accCfdTime_ += dt;
        int  subSteps  = 0;
        vector lastF(Zero), lastM(Zero), lastRawM(Zero);

        while (accCfdTime_ >= fastDT_)
        {
            OF2_SetImposedPlatformState(disp, vel, acc);

            int  errStat  = 0;
            char errMsg[INTERFACE_STRING_LENGTH];
            std::memset(errMsg, 0, sizeof(errMsg));
            int  iTurb_c  = 0;
            bool endEarly = false;
            // Local copies (immutable-after-init, but FAST_Update wants int*).
            int  nIn  = fastNumInputs_;
            int  nOut = fastNumOutputs_;
            {
                FpeGuard g;
                FAST_Update(
                    &iTurb_c,
                    &nIn,
                    &nOut,
                    fastInputAry_.data(),
                    fastOutputAry_.data(),
                    &endEarly,
                    &errStat,
                    errMsg
                );
            }
            if (errStat >= fastAbortErrLev_)
            {
                FatalErrorIn("of2Restraint::restrain()")
                    << "FAST_Update failed at t=" << t
                    << " (outerCorr " << outerCorr_
                    << ", sub-step " << (subSteps + 1)
                    << ", ErrStat=" << errStat
                    << ", AbortErrLev=" << fastAbortErrLev_ << "): "
                    << errMsg << abort(FatalError);
            }
            if (endEarly)
            {
                WarningInFunction
                    << "OpenFAST requested early end at t=" << t
                    << " (outerCorr " << outerCorr_
                    << ", sub-step " << (subSteps + 1)
                    << "). Ignoring; OpenFOAM-side simulation continues."
                    << endl;
            }

            // Read back the wrench cached by PMI_Bridge in ED_CalcOutput.
            // It is expressed in the PITCHED tower-base (platform) frame, so
            // rotate it into the OF GLOBAL frame by the body's physical
            // orientation before applying (Finding-A fix). Verified: rotating
            // the +5 deg tilted wrench by R_y(5) returns the weight to pure
            // vertical (-W) instead of leaking W*sin(theta) into F_x.
            double F[3], M[3];
            OF2_GetTowerBaseReaction(F, M);
            lastRawM = vector(M[0], M[1], M[2]);  // raw ED tower-base moment (diagnostic)
            frame_.loadToWorld
            (
                R_of_inertialFromBody,
                vector(F[0], F[1], F[2]), vector(M[0], M[1], M[2]),
                twrBsGlobal, lastF, lastM   // lastF=F_W, lastM=moment about origin
            );

            accCfdTime_ -= fastDT_;
            ++subSteps;
            ++fastUpdateCount_;
        }
        firedThisStep_ = subSteps;   // rollback count for the next corrector

        // --------------------------------------------------------------------
        // Build the wrench (about the GLOBAL ORIGIN, RBD fx[] convention) and
        // UNDER-RELAX it across the outer correctors of this step:
        //   corrector 1 (predictor) : take the new wrench as-is; open a fresh
        //                             Aitken sweep (warm-start ω from last step).
        //   corrector k             : residual r = wrenchNew - cached;
        //                             cachedWrench_ <- cached + ω·r, where ω is
        //                             the Aitken Δ² factor adapted from the
        //                             MOMENT-residual secant (r.w(): the pitch
        //                             moment is the divergent quantity, so we
        //                             drive convergence on it and apply the one
        //                             scalar ω to the FULL 6-DOF wrench). With
        //                             useAitken_ false this reduces to the fixed
        //                             relaxFactor_ blend.
        // If no fire happened this step (subSteps==0) the previous wrench is
        // held unchanged. fsiRho is the normalized FSI residual rho =
        // max(|dF|/(m_s g), |dM|/|M|) (eqs 43-45); once rho < fsiTol_ the gate
        // (fsiConverged_) closes and the wrench is held. fsiOmega is the factor.
        // --------------------------------------------------------------------
        scalar fsiRho   = 0;       // normalized FSI residual rho (eqs 43-45)
        scalar fsiOmega = relaxFactor_;
        if (subSteps > 0)
        {
            const spatialVector wrenchNew(lastM, lastF);  // lastM already about origin
            if (newStep)
            {
                cachedWrench_ = wrenchNew;
                aitken_.resetStep();   // warm-start ω; clear residual history
                fsiConverged_ = false; // open the convergence gate for this step
            }
            else if (!fsiConverged_)
            {
                const spatialVector resid  = wrenchNew - cachedWrench_;
                const vector        residM = resid.w();   // moment residual (N·m)

                // Normalized FSI residual (eqs 43-45): force by the structure
                // weight m_s·g, moment relative to |M|; rho = max of the two.
                const scalar weightRef =
                    max(model_.bodies()[bodyID_].m()*mag(model_.g()), SMALL);
                const scalar rForce  = mag(resid.l()) / weightRef;
                const scalar rMoment = mag(residM) / max(mag(cachedWrench_.w()), SMALL);
                fsiRho = max(rForce, rMoment);

                if (fsiRho < fsiTol_)
                {
                    // Converged: close the gate and HOLD the wrench for the rest
                    // of the outer correctors (further updates only add OpenFAST
                    // rollback noise once the iterate has settled).
                    fsiConverged_ = true;
                }
                else if (useAitken_)
                {
                    const double rv[3] = { residM.x(), residM.y(), residM.z() };
                    fsiOmega = aitken_.update(rv);   // ω from MOMENT secant (eq 41, |·|)
                    cachedWrench_ = cachedWrench_ + fsiOmega*resid;
                }
                else
                {
                    //  cached + ω·(new − cached) == (1−ω)·cached + ω·new.
                    cachedWrench_ = cachedWrench_ + fsiOmega*resid;
                }
            }
        }

        if (newStep)
        {
            lastStepTime_ = t;
            hasStepped_   = true;
            ++stepCount_;
            ++fastStepIdx_;
        }

        // Logging: first step, every 100th step (new-step summary), and every
        // corrector when verbose. Shows the FSI residual shrinking across the
        // outer loop — the signature of a converging strong coupling.
        if (verbose_ || stepCount_ == 1 || (newStep && (stepCount_ % 100) == 0))
        {
            Info<< " [OF2] step " << stepCount_ << "  t=" << t
                << "  outerCorr=" << outerCorr_
                << "  dt=" << dt
                << "  subSteps=" << subSteps
                << "  fired=" << firedThisStep_
                << "  omega=" << fsiOmega
                << "  rho=" << fsiRho
                << (fsiConverged_ ? "(conv)" : "")
                << "  ED disp=("
                << disp[0] << "," << disp[1] << "," << disp[2] << ","
                << disp[3]*180.0/M_PI << "deg,"
                << disp[4]*180.0/M_PI << "deg,"
                << disp[5]*180.0/M_PI << "deg)";
            // DIAGNOSTIC: the platform state IMPOSED on OpenFAST. pitchRate
            // (vel[4]) should be smooth; Aacc[1]=acc[4] is the pitch angular
            // acceleration fed to ED.
            Info<< "  pitchRate=" << vel[4] << "rad/s"
                << "  Aacc=(" << acc[3] << "," << acc[4] << "," << acc[5] << ")rad/s2"
                << "  Lacc=(" << acc[0] << "," << acc[1] << "," << acc[2] << ")m/s2";
            if (subSteps > 0)
            {
                Info<< "  TwrBs F=" << lastF << " N"
                    << "  Mraw=" << lastRawM << " N-m" << endl;
            }
            else
            {
                Info<< "  TwrBs wrench: held (no fire this step)" << endl;
            }
        }
    }

    // Apply the cached wrench every restrain() call on MASTER ONLY.
    // The rigid-body solver runs its ODE integration on master and
    // distributes the resulting kinematics to all ranks via OpenFOAM's
    // internal mesh-motion sync; non-master fx[] additions are wasted
    // (and broadcasting cachedWrench_ from inside restrain() hit
    // MPI_ERR_TRUNCATE because non-master never reaches the matching
    // collective call).
    fx[bodyIndex_] += cachedWrench_;

    // Restart support (phase 2a): persist the coupling state alongside the
    // end-of-step fluid/body snapshot. Cheap (a few dict entries); every
    // corrector of a write-time step overwrites, so the LAST corrector's
    // values — the ones consistent with the written fields — persist.
    writeRestartState();
}


void Foam::RBD::restraints::of2Restraint::restoreRestartState()
{
    // ALL RANKS must run this. IOdictionary is a GLOBAL IO object: its
    // header probe / read under the file handler is a COLLECTIVE
    // (master-read + broadcast). Gating this on Pstream::master() desynced
    // the parallel streams and segfaulted the very next collective (the
    // overset zone-transform reads) -- found by the restart bench. The
    // restored values are applied on every rank; non-masters simply never
    // use them (restrain()/initOpenFAST are master-gated).

    const Time& runTime = model_.time();

    IOobject io
    (
        "of2RestraintState",
        runTime.timeName(),
        "uniform",
        runTime,
        IOobject::READ_IF_PRESENT,
        IOobject::NO_WRITE,
        IOobject::NO_REGISTER
    );

    if (!io.typeHeaderOk<IOdictionary>(true))
    {
        return;   // fresh run (or pre-restart-support snapshot): cold start
    }

    IOdictionary rs(io);

    prpInitialWorld_    = rs.get<point>("prpInitialWorld");
    prpInitialCaptured_ = true;   // do NOT re-latch at the restart pose
    accCfdTime_         = rs.get<scalar>("accCfdTime");
    cachedWrench_       = rs.get<spatialVector>("cachedWrench");
    prevLinVelWorld_    = rs.getOrDefault<vector>("prevLinVelWorld", Zero);
    prevAngVelWorld_    = rs.getOrDefault<vector>("prevAngVelWorld", Zero);
    // NOT restored to true: model_.a() is zero until this process runs its
    // first forwardDynamics solve, regardless of the restored motion state.
    accInitialized_     = false;
    fastUpdateCount_    = rs.getOrDefault<label>("fastUpdateCount", 0);
    stepCount_          = rs.getOrDefault<label>("stepCount", 0);
    fastCheckpointRoot_ = rs.getOrDefault<fileName>("fastCheckpointRoot", fileName());

    Info<< " [OF2] restart state restored from "
        << runTime.timeName()/"uniform"/"of2RestraintState" << nl
        << "       PRP_initial(world)=" << prpInitialWorld_
        << " accCfdTime=" << accCfdTime_
        << " |cachedWrench|=(" << mag(cachedWrench_.w())
        << " N, " << mag(cachedWrench_.l()) << " N-m)" << endl;

    if (fastCheckpointRoot_.empty())
    {
        WarningInFunction
            << "of2RestraintState carries no OpenFAST checkpoint (snapshot "
            << "predates phase 2b?). Turbine module states will be "
            << "COLD-STARTED - tower/rotor/aero history is lost across this "
            << "restart; expect a turbine-side transient." << endl;
    }
    else if (!isFile(fastCheckpointRoot_ + ".chkp"))
    {
        // Refuse to silently cold-start when the snapshot PROMISED a
        // checkpoint: that would corrupt the splice invisibly.
        FatalErrorInFunction
            << "of2RestraintState names checkpoint '"
            << fastCheckpointRoot_ << ".chkp' but the file is missing. "
            << "Restore it (or delete the fastCheckpointRoot entry to "
            << "accept a cold turbine start)."
            << abort(FatalError);
    }
}


void Foam::RBD::restraints::of2Restraint::writeRestartState() const
{
    // Caller context is master-only (restrain() gates non-masters out).
    const Time& runTime = model_.time();

    if (!runTime.writeTime())
    {
        return;
    }

    IOdictionary rs
    (
        IOobject
        (
            "of2RestraintState",
            runTime.timeName(),
            "uniform",
            runTime,
            IOobject::NO_READ,
            IOobject::NO_WRITE,
            IOobject::NO_REGISTER
        )
    );

    rs.add("prpInitialWorld", prpInitialWorld_);
    rs.add("accCfdTime",      accCfdTime_);
    rs.add("cachedWrench",    cachedWrench_);
    rs.add("prevLinVelWorld", prevLinVelWorld_);
    rs.add("prevAngVelWorld", prevAngVelWorld_);
    rs.add("fastUpdateCount", fastUpdateCount_);
    rs.add("stepCount",       stepCount_);

    // ---- OpenFAST full-state checkpoint (phase 2b) ----------------------
    // Written on every corrector of a write-time step (same overwrite-wins
    // semantics as the dict): the LAST corrector's checkpoint is the one
    // consistent with the written fields. ~MBs, negligible next to the
    // fluid snapshot. checkpointKeep 0 disables.
    if (fastInitialized_ && checkpointKeep_ > 0)
    {
        const fileName chkRoot("openfast/chkpt_" + runTime.timeName());

        char rootBuf[INTERFACE_STRING_LENGTH];
        std::memset(rootBuf, 0, sizeof(rootBuf));
        const std::string rootStr = chkRoot;
        if (rootStr.size() + 1 < INTERFACE_STRING_LENGTH)
        {
            std::strcpy(rootBuf, rootStr.c_str());
            int  iTurbC   = 0;
            int  errStatC = 0;
            char errMsgC[INTERFACE_STRING_LENGTH];
            std::memset(errMsgC, 0, sizeof(errMsgC));
            {
                FpeGuard g;
                FAST_CreateCheckpoint(&iTurbC, rootBuf, &errStatC, errMsgC);
            }
            if (errStatC >= fastAbortErrLev_ && fastAbortErrLev_ > 0)
            {
                WarningInFunction
                    << "FAST_CreateCheckpoint failed (ErrStat=" << errStatC
                    << "): " << errMsgC << " - this snapshot will restart "
                    << "with a COLD turbine." << endl;
            }
            else
            {
                rs.add("fastCheckpointRoot", chkRoot);

                // FIFO pruning (one entry per write TIME, not per corrector)
                if (chkpHistory_.empty() || chkpHistory_.last() != chkRoot)
                {
                    chkpHistory_.append(chkRoot);
                }
                while (chkpHistory_.size() > checkpointKeep_)
                {
                    Foam::rm(chkpHistory_.first() + ".chkp");
                    // FAST_CreateCheckpoint also writes a small ServoDyn
                    // DLL-state companion (52 B with CompServo=0)
                    Foam::rm(chkpHistory_.first() + ".dll.chkp");
                    for (label i = 1; i < chkpHistory_.size(); ++i)
                    {
                        chkpHistory_[i-1] = chkpHistory_[i];
                    }
                    chkpHistory_.resize(chkpHistory_.size() - 1);
                }
            }
        }
    }

    rs.regIOobject::writeObject
    (
        IOstreamOption(runTime.writeFormat()),
        true
    );
}


bool Foam::RBD::restraints::of2Restraint::read(const dictionary& dict)
{
    restraint::read(dict);

    coeffs_.readEntry("fstFile", fstFilePath_);
    coeffs_.readIfPresent("towerBasePoint",           towerBasePointBody_);
    coeffs_.readIfPresent("platformReferencePoint",   platformReferencePointBody_);
    coeffs_.readIfPresent("verbose",                  verbose_);
    // IC orientation (deg) added to the imposed attitude (see header).
    coeffs_.readIfPresent("initialRoll",              initialRollDeg_);
    coeffs_.readIfPresent("initialPitch",             initialPitchDeg_);
    coeffs_.readIfPresent("initialYaw",               initialYawDeg_);
    coeffs_.readIfPresent("initialSurge",             initialSurge_);
    coeffs_.readIfPresent("initialSway",              initialSway_);
    coeffs_.readIfPresent("initialHeave",             initialHeave_);
    // Strong-coupling wrench under-relaxation. relaxFactor_ is the initial /
    // warm-start factor; with useAitken_ (default true) the per-corrector factor
    // is then adapted by Aitken Δ² on the moment residual, clamped to
    // [omegaMin_, omegaMax_]. See header.
    coeffs_.readIfPresent("relaxFactor",              relaxFactor_);
    coeffs_.readIfPresent("useAitken",                useAitken_);
    coeffs_.readIfPresent("omegaMin",                 omegaMin_);
    coeffs_.readIfPresent("omegaMax",                 omegaMax_);
    coeffs_.readIfPresent("fsiTol",                   fsiTol_);
    // Restart: number of OpenFAST checkpoints kept on disk (0 = disable).
    coeffs_.readIfPresent("checkpointKeep",           checkpointKeep_);
    aitken_.configure(relaxFactor_, omegaMin_, omegaMax_);

    // Configure the unified frame converter ONCE from the IC (the single place
    // the initial pose is encoded for the coupling). R_IC = Rz.Ry.Rx, plus the
    // translational IC. Everything downstream (disp/vel/acc and the load wrench)
    // derives from this via CouplingFrame.
    frame_.setIC
    (
        initialRollDeg_, initialPitchDeg_, initialYawDeg_,
        initialSurge_,   initialSway_,     initialHeave_
    );

    // Force a rebuild / re-init on next restrain() so dictionary edits take effect.
    hasStepped_ = false;

    return true;
}


void Foam::RBD::restraints::of2Restraint::write(Ostream& os) const
{
    restraint::write(os);   // writes "type" and "body"

    os.writeEntry("fstFile",                 fstFilePath_);
    os.writeEntry("towerBasePoint",          towerBasePointBody_);
    os.writeEntry("platformReferencePoint",  platformReferencePointBody_);
    os.writeEntry("verbose",                 verbose_);
    os.writeEntry("initialRoll",             initialRollDeg_);
    os.writeEntry("initialPitch",            initialPitchDeg_);
    os.writeEntry("initialYaw",              initialYawDeg_);
    os.writeEntry("initialSurge",            initialSurge_);
    os.writeEntry("initialSway",             initialSway_);
    os.writeEntry("initialHeave",            initialHeave_);
    os.writeEntry("relaxFactor",             relaxFactor_);
    os.writeEntry("useAitken",               useAitken_);
    os.writeEntry("checkpointKeep",          checkpointKeep_);
    os.writeEntry("omegaMin",                omegaMin_);
    os.writeEntry("omegaMax",                omegaMax_);
    os.writeEntry("fsiTol",                  fsiTol_);
}


// ************************************************************************* //
