/*---------------------------------------------------------------------------*\
    of2_fast_stubs.C — stand-in definitions for every OpenFAST / PMI_Bridge
    C-ABI symbol that the PRODUCTION of2Restraint.C references.

    Linking these into the test exe lets the REAL of2Restraint constructor and
    restrain() run end-to-end with NO real OpenFAST and NO CFD. The stubs:
      * satisfy the FAST_* init sequence (AllocateTurbines / Sizes / Start /
        CFD_InitIOarrays_SubStep) with success ErrStat and canned sizes,
      * capture the 6-DOF platform state pushed to ElastoDyn each restrain()
        call into g_disp/g_vel/g_acc (read by the harness assertions),
      * return a fixed, OC4-representative tower-base reaction.

    IMPORTANT: this file deliberately does NOT include FAST_Library.h — that
    header declares FAST_Sizes with C++ default args (= NULL) and including it
    here would re-declare them and conflict. C linkage means the linker matches
    these definitions to the header-declared prototypes by bare name regardless
    of the (defaulted) trailing parameters.
\*---------------------------------------------------------------------------*/

#include <cstring>

extern "C" {

// ---- capture buffers (read by the harness) --------------------------------
double g_disp[6] = {0,0,0,0,0,0};
double g_vel[6]  = {0,0,0,0,0,0};
double g_acc[6]  = {0,0,0,0,0,0};
int    g_setCount = 0;

// ---- added-mass FSI knob (default OFF -> fixed wrench, preserves tests 1-5) -
// When g_addedMassPitch != 0 the tower-base pitch moment becomes motion-
// dependent:  M_y = g_M0pitch - g_addedMassPitch * a_pitch_imposed, where the
// imposed pitch acceleration is g_acc[4] (set by OF2_SetImposedPlatformState
// earlier in the SAME restrain() call). This is the canonical added-mass map:
// the reaction OPPOSES the imposed acceleration with an effective inertia, the
// mechanism that drives the partitioned-FSI negative-damping divergence. A
// large g_addedMassPitch (> the platform's effective pitch inertia) makes a
// fixed under-relaxation diverge while Aitken converges.
double g_addedMassPitch = 0.0;        // [kg m^2] effective added pitch inertia
double g_M0pitch        = 3.886e7;    // [N m]    baseline overturning moment

// ---- PMI_Bridge bridge symbols --------------------------------------------
void OF2_SetImposedPlatformState
(
    const double disp[6],
    const double vel[6],
    const double acc[6]
)
{
    for (int i = 0; i < 6; ++i)
    {
        g_disp[i] = disp[i];
        g_vel[i]  = vel[i];
        g_acc[i]  = acc[i];
    }
    ++g_setCount;
}

void OF2_GetTowerBaseReaction(double F[3], double M[3])
{
    // OC4-representative tower-base reaction expressed in the ED tower-base
    // (platform) frame: streamwise thrust + weight in F, overturning in M.
    F[0] =  5.473e5;  F[1] = 0.0;  F[2] = -5.903e6;
    M[0] =  0.0;      M[1] = 3.886e7;  M[2] = 0.0;

    if (g_addedMassPitch != 0.0)
    {
        // Motion-dependent overturning moment (added-mass FSI map). a_pitch is
        // the imposed pitch acceleration just pushed to ElastoDyn this corrector.
        M[1] = g_M0pitch - g_addedMassPitch * g_acc[4];
    }
}

// ---- FAST_* lifecycle -----------------------------------------------------
void FAST_AllocateTurbines(int* /*iTurb*/, int* ErrStat, char* ErrMsg)
{
    *ErrStat = 0;
    if (ErrMsg) ErrMsg[0] = '\0';
}

void FAST_Sizes
(
    int*        /*iTurb*/,
    const char* /*InputFileName*/,
    int*        AbortErrLev,
    int*        NumOuts,
    double*     dt,
    double*     dt_out,
    double*     tmax,
    int*        ErrStat,
    char*       ErrMsg,
    char*       ChannelNames,
    double*     TMax,
    double*     InitInputAry
)
{
    *AbortErrLev = 4;       // ErrID_Fatal
    *NumOuts     = 1;       // time channel only
    *dt          = 0.0125;
    *dt_out      = 0.0125;
    *tmax        = 600.0;
    *ErrStat     = 0;
    if (ErrMsg)       ErrMsg[0] = '\0';
    if (ChannelNames) ChannelNames[0] = '\0';
    if (TMax)         *TMax = 600.0;
    (void)InitInputAry;
}

void FAST_Start
(
    int*    /*iTurb*/,
    int*    /*NumInputs_c*/,
    int*    /*NumOutputs_c*/,
    double* /*InputAry*/,
    double* OutputAry,
    int*    ErrStat,
    char*   ErrMsg
)
{
    if (OutputAry) OutputAry[0] = 0.0;   // time channel at t=0
    *ErrStat = 0;
    if (ErrMsg) ErrMsg[0] = '\0';
}

void FAST_Update
(
    int*    /*iTurb*/,
    int*    /*NumInputs_c*/,
    int*    /*NumOutputs_c*/,
    double* /*InputAry*/,
    double* /*OutputAry*/,
    bool*   EndSimulationEarly,
    int*    ErrStat,
    char*   ErrMsg
)
{
    if (EndSimulationEarly) *EndSimulationEarly = false;
    *ErrStat = 0;
    if (ErrMsg) ErrMsg[0] = '\0';
}

void FAST_End(int* /*iTurb*/, bool* /*stopThisProgram*/)
{
    // no-op
}

void FAST_CFD_Store_SubStep(int* /*iTurb*/, int* /*n_t_global*/, int* ErrStat, char* ErrMsg)
{
    *ErrStat = 0;
    if (ErrMsg) ErrMsg[0] = '\0';
}

void FAST_CFD_Reset_SubStep(int* /*iTurb*/, int* /*n_timesteps*/, int* ErrStat, char* ErrMsg)
{
    *ErrStat = 0;
    if (ErrMsg) ErrMsg[0] = '\0';
}

void FAST_CFD_InitIOarrays_SubStep(int* /*iTurb*/, int* ErrStat, char* ErrMsg)
{
    *ErrStat = 0;
    if (ErrMsg) ErrMsg[0] = '\0';
}

// ---- restart / checkpoint (phase 2b) --------------------------------------
void FAST_Restart
(
    int*        /*iTurb*/,
    const char* /*CheckpointRootname*/,
    int*        AbortErrLev,
    int*        NumOuts,
    double*     dt,
    int*        n_t_global,
    int*        ErrStat,
    char*       ErrMsg
)
{
    *AbortErrLev = 4;       // ErrID_Fatal
    *NumOuts     = 1;       // time channel only
    *dt          = 0.0125;
    *n_t_global  = 0;
    *ErrStat     = 0;
    if (ErrMsg) ErrMsg[0] = '\0';
}

void FAST_CreateCheckpoint
(
    int*        /*iTurb*/,
    const char* /*CheckpointRootname*/,
    int*        ErrStat,
    char*       ErrMsg
)
{
    *ErrStat = 0;
    if (ErrMsg) ErrMsg[0] = '\0';
}

} // extern "C"
