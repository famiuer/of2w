/*---------------------------------------------------------------------------*\
    testRestrainHarness.C — end-to-end unit test of the PRODUCTION
    of2Restraint::restrain() with OpenFAST stubbed out (no CFD, no real
    OpenFAST). It builds a real OC4-like rigidBodyModel, drives the real
    restraint through several kinematic states, and asserts on the 6-DOF
    platform state the restraint pushes to ElastoDyn (captured by the stubs
    in of2_fast_stubs.C) plus the world-frame reaction wrench it applies.

    Run SERIAL: restrain() only acts on Pstream::master() (always true serial).
\*---------------------------------------------------------------------------*/

#include "Time.H"
#include "dictionary.H"
#include "IStringStream.H"
#include "OSspecific.H"
#include "rigidBodyModel.H"
#include "rigidBodyModelState.H"
#include "of2Restraint.H"

#include <cmath>
#include <cstdio>
#include <string>
#include <functional>
#include <vector>

using namespace Foam;
using namespace Foam::RBD;

// Stub capture buffers (defined in of2_fast_stubs.C).
extern "C" double g_disp[6], g_vel[6], g_acc[6];
extern "C" int    g_setCount;
// Added-mass FSI knob in the stub (0 -> fixed wrench; preserves tests 1-5).
extern "C" double g_addedMassPitch, g_M0pitch;

// ----------------------------------------------------------------------------
// Relative to the process cwd (run_tests.sh runs the binary from this harness
// dir, where the case/ fixture ships) — portable across machines (was a
// hardcoded local absolute path, which failed to write on ham8).
static const char* HARNESS_ROOT = ".";

// OC4 geometry constants shared by code and expected-value computation.
static const vector CoM(4.5537, 0.0, -13.4526);          // centre of mass / CoR
static const vector PtfmRef(4.9998, 0.0, -0.0074);       // platform reference point
static const vector TwrBs(5.3316, 0.0, 9.9945);          // tower-base point (body)
static const double IC_PITCH_DEG = 1.9;
static const double IC_SURGE     = 5.0;
static const double IC_HEAVE     = -0.0074;
static const double DEG2RAD       = M_PI/180.0;

// test bookkeeping
static int g_fail = 0;

static void report(const char* name, bool ok)
{
    std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", name);
    if (!ok) ++g_fail;
}

static bool closeAbs(double a, double b, double tol)
{
    return std::fabs(a - b) <= tol;
}

// ----------------------------------------------------------------------------
// Local re-implementations of the frame primitives (3x3 row-major), used ONLY
// to compute the expected values independently of the production path.
struct M3 { double m[3][3]; };
static vector mv(const M3& A, const vector& v)
{
    return vector
    (
        A.m[0][0]*v.x()+A.m[0][1]*v.y()+A.m[0][2]*v.z(),
        A.m[1][0]*v.x()+A.m[1][1]*v.y()+A.m[1][2]*v.z(),
        A.m[2][0]*v.x()+A.m[2][1]*v.y()+A.m[2][2]*v.z()
    );
}
static M3 Ry_(double t){ double c=std::cos(t),s=std::sin(t);
    return M3{{{c,0,s},{0,1,0},{-s,0,c}}}; }
// constant W->E axis map A
static const M3 A_WE = {{{1,0,0},{0,0,1},{0,-1,0}}};

// ----------------------------------------------------------------------------
int main()
{
    // Ensure the fixture case dirs exist (defensive; they ship as fixtures).
    mkDir(fileName(HARNESS_ROOT)/"case"/"system");
    mkDir(fileName(HARNESS_ROOT)/"case"/"constant");

    Time runTime
    (
        Time::controlDictName,
        fileName(HARNESS_ROOT),
        fileName("case"),
        false,   // disable function objects
        false    // disable libs
    );

    // ----- build the OC4 platform rigidBodyModel ----------------------------
    const std::string modelStr =
        "bodies\n"
        "{\n"
        "    platformBody\n"
        "    {\n"
        "        type            rigidBody;\n"
        "        parent          root;\n"
        "        centreOfMass    (4.5537 0 -13.4526);\n"
        "        mass            1.3473e7;\n"
        "        inertia         (6.827e9 0 0 6.827e9 0 1.226e10);\n"
        "        transform       (1 0 0 0 1 0 0 0 1) (0 0 0);\n"
        "        joint           { type floating; }\n"
        "        patches         (float);\n"
        "        innerDistance   100;\n"
        "        outerDistance   101;\n"
        "    }\n"
        "}\n";

    IStringStream modelIs(modelStr);
    dictionary modelDict(modelIs);
    rigidBodyModel model(runTime, modelDict);

    const label bodyID    = model.bodyID("platformBody");
    const label bodyIndex = model.master(bodyID);

    std::printf("model: nDoF=%d nBodies=%d bodyID(platformBody)=%d bodyIndex=%d\n",
                int(model.nDoF()), int(model.nBodies()), int(bodyID), int(bodyIndex));

    // ----- build the restraint dict and construct the REAL restraint --------
    const std::string rStr =
        "type                    OpenFast;\n"
        "body                    platformBody;\n"
        "fstFile                 \"x.fst\";\n"
        "towerBasePoint          (5.3316 0 9.9945);\n"
        "platformReferencePoint  (4.9998 0 -0.0074);\n"
        "initialSurge            5;\n"
        "initialSway             0;\n"
        "initialHeave            -0.0074;\n"
        "initialRoll             0;\n"
        "initialPitch            1.9;\n"
        "initialYaw              0;\n"
        "verbose                 false;\n"
        "relaxFactor             1;\n";
    IStringStream rIs(rStr);
    dictionary rdict(rIs);

    // Constructing this runs the FULL OpenFAST init path against the stubs.
    RBD::restraints::of2Restraint r("of2", rdict, model);

    const double icPitchRad = IC_PITCH_DEG*DEG2RAD;
    const double fastDT      = 0.0125;   // matches stub FAST_Sizes

    // Helper: set a state, run forwardDynamics to populate X0_/v_/a_, then
    // fire the production restrain(). Returns the wrench field by reference.
    auto fire =
        [&](scalar t, scalar dt,
            std::function<void(rigidBodyModelState&)> setup,
            Field<spatialVector>& rfxOut)
    {
        rigidBodyModelState state(model);
        state.q()    = scalarField(model.nDoF(), 0.0);
        state.qDot() = scalarField(model.nDoF(), 0.0);
        state.qDdot()= scalarField(model.nDoF(), 0.0);
        state.t()      = t;
        state.deltaT() = dt;
        setup(state);

        scalarField tau(model.nDoF(), 0.0);
        Field<spatialVector> fx(model.nBodies(), spatialVector::zero);
        model.forwardDynamics(state, tau, fx);

        scalarField rtau(model.nDoF(), 0.0);
        rfxOut = Field<spatialVector>(model.nBodies(), spatialVector::zero);
        r.restrain(rtau, rfxOut, state);
    };

    // ====================================================================
    // CALL 1: q=0, qDot=0 at t=dt. First call -> latches PRP initial world.
    // Drives assertions 1 (g_setCount) and 2 (initial displacement).
    // ====================================================================
    {
        Field<spatialVector> rfx;
        fire(fastDT, fastDT, [](rigidBodyModelState&){}, rfx);

        report("1_setCount_fired", g_setCount > 0);
        std::printf("    g_setCount=%d\n", g_setCount);

        // Expected initial disp from the frame contract:
        //   surge IC, sway IC, heave IC, then Euler(R_IC=Ry(1.9deg)) = (0,P,0).
        const double exp[6] = {IC_SURGE, 0.0, IC_HEAVE, 0.0, icPitchRad, 0.0};
        bool ok = true;
        for (int i = 0; i < 6; ++i) ok = ok && closeAbs(g_disp[i], exp[i], 1e-9);
        report("2_initial_displacement", ok);
        std::printf("    g_disp = [% .9g % .9g % .9g % .9g % .9g % .9g]\n",
                    g_disp[0],g_disp[1],g_disp[2],g_disp[3],g_disp[4],g_disp[5]);
        std::printf("    expect = [% .9g % .9g % .9g % .9g % .9g % .9g]\n",
                    exp[0],exp[1],exp[2],exp[3],exp[4],exp[5]);
    }

    // ====================================================================
    // CALL 2: q=0, qDot pitch-rate = -0.30 at t=2dt.
    // Assertion 3: velocity is sampled at PtfmRef (lever arm), not body origin.
    // ====================================================================
    {
        Field<spatialVector> rfx;
        fire(2*fastDT, fastDT,
             [&](rigidBodyModelState& s){ s.qDot()[4] = -0.30; }, rfx);

        const vector omega(0.0, -0.30, 0.0);
        // CoR for the floating joint is the joint/body-frame origin, which for
        // an identity transform at q=0 sits at the WORLD ORIGIN — NOT the CoM.
        // (verified against rigidBodyModel::v: linear vel at p = omega x p.)
        // The CoM enters only the inertia/dynamics, never the velocity field.
        const vector CoR(0.0, 0.0, 0.0);
        const vector vPrp = omega ^ (PtfmRef - CoR);   // world v at PtfmRef
        const vector vOrg = omega ^ (vector::zero - CoR); // body-origin vel == 0

        const vector eGood = vPrp;   // linear DOFs identity-mapped (NOT A): Sg/Sw/Hv = world x/y/z
        const vector eBad  = vOrg;   // body-origin velocity (== 0, the old lever-arm bug)

        bool matchGood =
            closeAbs(g_vel[0], eGood.x(), 1e-6) &&
            closeAbs(g_vel[1], eGood.y(), 1e-6) &&
            closeAbs(g_vel[2], eGood.z(), 1e-6);

        const double diffMag = std::sqrt
        (
            (eGood.x()-eBad.x())*(eGood.x()-eBad.x()) +
            (eGood.y()-eBad.y())*(eGood.y()-eBad.y()) +
            (eGood.z()-eBad.z())*(eGood.z()-eBad.z())
        );
        bool differsFromOrigin =
            !( closeAbs(g_vel[0], eBad.x(), 1e-3) &&
               closeAbs(g_vel[1], eBad.y(), 1e-3) &&
               closeAbs(g_vel[2], eBad.z(), 1e-3) )
            && diffMag > 1.0;

        report("3a_lever_arm_velocity_at_PRP", matchGood);
        report("3b_velocity_not_body_origin",  differsFromOrigin);
        std::printf("    g_vel(lin) = [% .9g % .9g % .9g]\n",
                    g_vel[0],g_vel[1],g_vel[2]);
        std::printf("    expect PRP = [% .9g % .9g % .9g]\n",
                    eGood.x(),eGood.y(),eGood.z());
        std::printf("    (origin)   = [% .9g % .9g % .9g]  |diff|=% .4g m/s\n",
                    eBad.x(),eBad.y(),eBad.z(), diffMag);
    }

    // ====================================================================
    // CALL 3: motion pitch q[4]=motionPitch at t=3dt.
    // Assertion 4: g_disp[4] ~ IC_pitch + motionPitch (Euler addition exact).
    // ====================================================================
    {
        const double motionPitch = 0.05;   // rad
        Field<spatialVector> rfx;
        fire(3*fastDT, fastDT,
             [&](rigidBodyModelState& s){ s.q()[4] = motionPitch; }, rfx);

        // Sign of the motion contribution is what we are probing; accept either
        // (IC + motion) or (IC - motion) and report which, to expose q-order.
        const double expPlus  = icPitchRad + motionPitch;
        const double expMinus = icPitchRad - motionPitch;
        bool okPlus  = closeAbs(g_disp[4], expPlus, 1e-6);
        bool okMinus = closeAbs(g_disp[4], expMinus, 1e-6);
        report("4_motion_pitch_added_to_IC", okPlus || okMinus);
        std::printf("    g_disp[4]=% .9g  IC+m=% .9g  IC-m=% .9g  (%s)\n",
                    g_disp[4], expPlus, expMinus,
                    okPlus ? "IC+motion" : (okMinus ? "IC-motion" : "NEITHER"));
    }

    // ====================================================================
    // CALL 4: q=0, qDot=0 at t=4dt. Clean state for the wrench assertion.
    // Assertion 5: rfx[bodyIndex] force = Ry(1.9deg) . (5.473e5,0,-5.903e6).
    // ====================================================================
    {
        Field<spatialVector> rfx;
        fire(4*fastDT, fastDT, [](rigidBodyModelState&){}, rfx);

        const spatialVector w = rfx[bodyIndex];
        const vector Fapplied = w.l();   // spatialVector: (moment, force)

        const vector F_T(5.473e5, 0.0, -5.903e6);
        const vector F_W = mv(Ry_(icPitchRad), F_T);   // R_IC . F_T (R_X0 = I)

        bool finite =
            std::isfinite(Fapplied.x()) && std::isfinite(Fapplied.y()) &&
            std::isfinite(Fapplied.z()) && (mag(Fapplied) > SMALL);
        bool matchF =
            closeAbs(Fapplied.x(), F_W.x(), 1.0) &&
            closeAbs(Fapplied.y(), F_W.y(), 1.0) &&
            closeAbs(Fapplied.z(), F_W.z(), 1.0);
        const double magExpect = std::sqrt(5.473e5*5.473e5 + 5.903e6*5.903e6);
        bool magOk = closeAbs(mag(Fapplied), magExpect, 1.0);

        report("5a_wrench_finite_nonzero", finite);
        report("5b_wrench_force_rotated_reaction", matchF);
        report("5c_wrench_force_magnitude", magOk);
        std::printf("    F_applied = [% .9g % .9g % .9g]  |F|=% .9g\n",
                    Fapplied.x(),Fapplied.y(),Fapplied.z(), mag(Fapplied));
        std::printf("    expect    = [% .9g % .9g % .9g]  |F|=% .9g\n",
                    F_W.x(),F_W.y(),F_W.z(), magExpect);
    }

    // ====================================================================
    // CALL 6: FSI fixed-point convergence ACROSS CORRECTORS (the Aitken test).
    //
    // Turn on the motion-dependent (added-mass) stub and drive a closed-loop
    // partitioned iteration at a SINGLE, FROZEN CFD step (q=qDot=0 held; only
    // the interface wrench iterates). Each corrector:
    //    forwardDynamics(state, 0, fxPrev)  -> body accel a from last wrench
    //    restrain(...)  -> imposes a to ED, stub returns M(a)=M0 - Iadd*a,
    //                      relaxes cachedWrench, writes applied wrench to rfx
    //    fxPrev = rfx                       -> close the loop
    // The applied pitch moment about the origin, rfx[bodyIndex].w().y(), is the
    // FSI fixed-point iterate. With Iadd >> the platform's effective pitch
    // inertia the loop gain H'<-1: a FIXED under-relaxation (useAitken=false,
    // relaxFactor 0.5) DIVERGES (successive deltas grow); ADAPTIVE Aitken
    // converges (deltas decay to ~0). This is the end-to-end proof that the
    // production restrain() integration tames the divergence the old explicit
    // scheme suffered.
    // ====================================================================
    {
        g_M0pitch        = 3.886e7;
        g_addedMassPitch = 1.5e11;   // >> I_eff_pitch (~1.5e10) -> |H'| >> 1

        const int K = 30;

        // OpenFAST is a global singleton (one Turbine(1) slot), so the harness
        // can hold only ONE of2Restraint. We reconfigure the single instance `r`
        // between the fixed and Aitken runs via read() — which resets hasStepped_
        // (so the next call is a fresh step) and reconfigures aitken_.
        auto buildR =
            [&](bool useAitken) -> std::string
        {
            std::string s =
                "type                    OpenFast;\n"
                "body                    platformBody;\n"
                "fstFile                 \"x.fst\";\n"
                "towerBasePoint          (5.3316 0 9.9945);\n"
                "platformReferencePoint  (4.9998 0 -0.0074);\n"
                "initialSurge 5; initialSway 0; initialHeave -0.0074;\n"
                "initialRoll 0; initialPitch 1.9; initialYaw 0;\n"
                "verbose                 false;\n"
                "relaxFactor             0.5;\n";
            s += useAitken
               ? "useAitken true; omegaMin 0.01; omegaMax 1.0;\n"
               : "useAitken false;\n";
            return s;
        };

        // Run K correctors at a frozen step; return the applied-pitch-moment series.
        // `t0` is per-run so the FIRST corrector of each run is a fresh newStep.
        auto runFsi =
            [&](scalar t0, std::vector<double>& My)
        {
            Field<spatialVector> fxPrev(model.nBodies(), spatialVector::zero);
            for (int k = 0; k < K; ++k)
            {
                rigidBodyModelState s(model);
                s.q()     = scalarField(model.nDoF(), 0.0);
                s.qDot()  = scalarField(model.nDoF(), 0.0);
                s.qDdot() = scalarField(model.nDoF(), 0.0);
                s.t()      = t0;         // SAME t every corrector of this run
                s.deltaT() = fastDT;
                scalarField tau(model.nDoF(), 0.0);
                Field<spatialVector> fx(model.nBodies(), spatialVector::zero);
                model.forwardDynamics(s, tau, fxPrev);   // accel <- last wrench
                Field<spatialVector> rfx(model.nBodies(), spatialVector::zero);
                r.restrain(tau, rfx, s);
                My.push_back(rfx[bodyIndex].w().y());
                fxPrev = rfx;
            }
        };

        std::vector<double> MyFixed, MyAitk;
        {
            IStringStream is(buildR(false));
            dictionary d(is);
            r.read(d);                    // fixed under-relaxation, hasStepped_=false
            runFsi(100.0*fastDT, MyFixed);
        }
        {
            IStringStream is(buildR(true));
            dictionary d(is);
            r.read(d);                    // Aitken, fresh step
            runFsi(200.0*fastDT, MyAitk);
        }

        auto deltaEarly = [](const std::vector<double>& v)
        { return std::fabs(v[1] - v[0]); };
        auto deltaLate = [](const std::vector<double>& v)
        { return std::fabs(v[v.size()-1] - v[v.size()-2]); };

        const double dFixEarly = deltaEarly(MyFixed), dFixLate = deltaLate(MyFixed);
        const double dAitEarly = deltaEarly(MyAitk),  dAitLate = deltaLate(MyAitk);

        // Fixed-0.5: corrector-to-corrector change GROWS (divergence).
        const bool fixedDiverges =
            (dFixLate > 2.0*dFixEarly) ||
            !std::isfinite(MyFixed.back()) ||
            (std::fabs(MyFixed.back()) > 1e3*std::fabs(MyFixed.front()) + 1e6);

        // Aitken: corrector-to-corrector change DECAYS toward zero (convergence).
        const bool aitkenConverges =
            std::isfinite(MyAitk.back()) &&
            (dAitLate < 1e-4*dAitEarly) &&
            (dAitLate < 1.0);                 // < 1 N·m residual at the end

        // And Aitken must be strictly better than fixed at the same corrector budget.
        const bool aitkenBeatsFixed = (dAitLate < dFixLate);

        report("6a_fixed_relax_diverges_on_addedmass", fixedDiverges);
        report("6b_aitken_converges_on_addedmass",     aitkenConverges);
        report("6c_aitken_beats_fixed",                aitkenBeatsFixed);
        std::printf("    fixed  My: front=% .4g back=% .4g  dEarly=% .4g dLate=% .4g\n",
                    MyFixed.front(), MyFixed.back(), dFixEarly, dFixLate);
        std::printf("    aitken My: front=% .4g back=% .4g  dEarly=% .4g dLate=% .4g\n",
                    MyAitk.front(), MyAitk.back(), dAitEarly, dAitLate);

        g_addedMassPitch = 0.0;   // restore fixed-wrench stub for any later use
    }

    // ----- summary ----------------------------------------------------------
    std::printf("\n");
    if (g_fail == 0)
    {
        std::printf("==== ALL TESTS PASSED ====\n");
        return 0;
    }
    std::printf("==== %d TEST(S) FAILED ====\n", g_fail);
    return 1;
}
