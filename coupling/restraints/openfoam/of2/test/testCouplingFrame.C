// testCouplingFrame.C — Foam-linked unit test of the PRODUCTION CouplingFrame
// (real Foam::tensor/vector, the exact type bridges + core compiled into
// libOF2.so). Confirms the frame contract handles the error scenarios we hit
// (lever-arm velocity, off-axis attitude, gimbal) WITHOUT any CFD run.
//
// Build:  wmake   (from this dir, OpenFOAM env sourced)
// Run:    testCouplingFrame
#include "CouplingFrame.H"
#include "tensor.H"
#include <cstdio>
#include <cmath>

using namespace Foam;
using namespace Foam::RBD::restraints;

static int FAILS = 0;
static const double d2r = M_PI/180.0, r2d = 180.0/M_PI;

static void chk(const char* name, double err, double tol)
{
    bool ok = (err <= tol) && std::isfinite(err);
    if (!ok) ++FAILS;
    std::printf("  [%s] %-46s err=%.3e (tol %.0e)\n", ok?"PASS":"FAIL", name, err, tol);
}
static void chk_in(const char* name, double v, double lo, double hi)
{
    bool ok = (v>=lo && v<=hi);
    if (!ok) ++FAILS;
    std::printf("  [%s] %-46s val=%.4f (in %.3f..%.3f)\n", ok?"PASS":"FAIL", name, v, lo, hi);
}

// elementary Foam-tensor rotations (world<-body active)
static tensor Rx(double a){ double c=std::cos(a),s=std::sin(a); return tensor(1,0,0, 0,c,-s, 0,s,c); }
static tensor Ry(double a){ double c=std::cos(a),s=std::sin(a); return tensor(c,0,s, 0,1,0, -s,0,c); }
static tensor Rz(double a){ double c=std::cos(a),s=std::sin(a); return tensor(c,-s,0, s,c,0, 0,0,1); }

int main()
{
    CouplingFrame F;
    F.setIC(/*roll*/0.0, /*pitch*/1.9, /*yaw*/0.0, /*surge*/5.0, /*sway*/0.0, /*heave*/0.0);
    const tensor I3(1,0,0, 0,1,0, 0,0,1);

    // -- 0: type bridge (a transposed accessor would corrupt everything) -------
    std::printf("\n[0] TYPE BRIDGE  (Foam::tensor <-> of2::Mat3 component map)\n");
    {
        tensor R = Ry(0.1234);
        of2::Mat3 M = toMat3(R);
        double e = std::fabs(M(0,0)-R.xx())+std::fabs(M(0,2)-R.xz())
                 + std::fabs(M(2,0)-R.zx())+std::fabs(M(1,1)-R.yy());
        chk("toMat3 preserves row-major components", e, 1e-15);
        vector v(1.0,-2.0,3.0); vector w = toFoam(toVec3(v));
        chk("toFoam(toVec3(v)) == v", mag(v-w), 1e-15);
    }

    // -- A: initial displacement (zero motion -> IC) ---------------------------
    std::printf("\n[A] INITIAL DISPLACEMENT\n");
    {
        double disp[6],vel[6],acc[6];
        bool ok = F.motionToModule(I3, vector::zero, vector::zero, vector::zero,
                                   vector::zero, vector::zero, disp, vel, acc);
        double e = std::fabs(disp[0]-5)+std::fabs(disp[1])+std::fabs(disp[2])
                 + std::fabs(disp[3])+std::fabs(disp[4]-1.9*d2r)+std::fabs(disp[5]);
        chk("disp == IC (surge5, pitch1.9)", e, 1e-12);
        chk("returns ok", ok?0.0:1.0, 0.5);
    }

    // -- B: divergence pose -> outputs match the verified standalone numbers ---
    std::printf("\n[B] MOTION (abs pitch -4.5 + roll + yaw, the divergence regime)\n");
    {
        tensor R_X0 = Rz(0.8*d2r) & Ry(-6.4*d2r) & Rx(1.0*d2r);   // abs pitch -4.5
        vector omega(0.02,-0.30,0.05), alpha(0.01,-0.05,0.02);
        double disp[6],vel[6],acc[6];
        bool ok = F.motionToModule(R_X0, vector::zero, omega, vector::zero,
                                   alpha, vector::zero, disp, vel, acc);
        std::printf("    disp(R,P,Y)=(%.4f %.4f %.4f) deg  vel(R,P,Y)=(%.5f %.5f %.5f)\n",
                    disp[3]*r2d,disp[4]*r2d,disp[5]*r2d, vel[3],vel[4],vel[5]);
        // expected values come from the verified g++ standalone test
        chk("disp.pitch == -4.5002 deg", std::fabs(disp[4]*r2d - (-4.5002)), 1e-3);
        chk("disp.roll  ==  1.0266 deg", std::fabs(disp[3]*r2d - ( 1.0266)), 1e-3);
        chk("vel.pitch  == -0.30025 rad/s (Jacobian)", std::fabs(vel[4]-(-0.30025)), 1e-4);
        chk("returns ok", ok?0.0:1.0, 0.5);
    }

    // -- C: force input -> world wrench ----------------------------------------
    std::printf("\n[C] FORCE (tower-base wrench -> world)\n");
    {
        vector F_T(5.473e5,0.0,-5.903e6), M_T(0.0,3.886e7,0.0), p_twb(5.3316,0,9.9945);
        vector F_W, M_O;
        F.loadToWorld(I3, F_T, M_T, p_twb, F_W, M_O);
        double c=std::cos(1.9*d2r), s=std::sin(1.9*d2r);
        vector F_exp(c*F_T.x()+s*F_T.z(), 0.0, -s*F_T.x()+c*F_T.z());
        chk("F_W == Ry(1.9)*F_T", mag(F_W-F_exp), 1e-2);
        double myExp = M_T.y() + (p_twb.z()*F_W.x() - p_twb.x()*F_W.z());
        chk("M_O.y == My + (p x F).y", std::fabs(M_O.y()-myExp), 1e-2);
        chk("no spurious lateral Fy", std::fabs(F_W.y()), 1e-6);
    }

    // -- D: robustness (singular gate) -----------------------------------------
    std::printf("\n[D] ROBUSTNESS (gimbal gate)\n");
    {
        double disp[6],vel[6],acc[6];
        bool ok = F.motionToModule(Ry(88.1*d2r), vector::zero, vector(0,0,0.1),
                                   vector::zero, vector::zero, vector::zero, disp,vel,acc);
        chk("pitch~90deg -> returns false", ok?1.0:0.0, 0.5);
        bool ok2 = F.motionToModule(Ry(-6.4*d2r), vector::zero, vector(0,0,0.01),
                                    vector::zero, vector::zero, vector::zero, disp,vel,acc);
        chk("healthy pose still passes", ok2?0.0:1.0, 0.5);
    }

    // -- E: lever-arm velocity (the rotor-inflow error we traced) --------------
    std::printf("\n[E] LEVER-ARM VELOCITY (PtfmRef vs body origin)\n");
    {
        vector CoR(4.5537,0,-13.4526);
        vector up = Ry(1.9*d2r) & vector(0,0,13.4526);
        vector PtfmRef = CoR + up;
        vector omega(0,-0.30,0);
        vector vRef = omega ^ (PtfmRef - CoR);
        vector vOrg = omega ^ (vector::zero - CoR);
        chk_in("|v(PtfmRef)-v(origin)| == |omega|*5m", mag(vRef-vOrg), 1.4, 1.6);
        double disp[6],vel[6],acc[6];
        F.motionToModule(I3, vector::zero, omega, vRef, vector::zero, vector::zero, disp,vel,acc);
        // vel[0:3] = v(PtfmRef) IDENTITY: ED Sg/Sw/Hv axes align with OF world
        // x/y/z (the axis map A applies ONLY to the rotation/Euler path).
        vector vE(vRef.x(), vRef.y(), vRef.z());
        double e = std::fabs(vel[0]-vE.x())+std::fabs(vel[1]-vE.y())+std::fabs(vel[2]-vE.z());
        chk("vel[0:3] == v(PtfmRef) identity", e, 1e-12);
    }

    std::printf("\n==== %s (%d failures) ====\n", FAILS?"TEST FAILURES":"ALL TESTS PASSED", FAILS);
    return FAILS ? 1 : 0;
}
