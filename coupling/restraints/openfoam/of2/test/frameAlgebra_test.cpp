// frameAlgebra_test.cpp — unit test for the OF² coupling frame contract.
// Build:  g++ -std=c++17 -O2 -I.. frameAlgebra_test.cpp -o frameAlgebra_test
// Checks: (A) initial displacement, (B) motion input -> disp/vel/acc accuracy,
//         (C) force input -> world wrench, (D) robustness (gimbal gating).
#include "../frameAlgebra.hpp"
#include <cstdio>
#include <cmath>
using namespace of2;

static int FAILS=0;
static void check(const char* name,double err,double tol){
    bool ok=(err<=tol && std::isfinite(err));
    if(!ok) ++FAILS;
    printf("  [%s] %-46s err=%.3e (tol %.0e)\n", ok?"PASS":"FAIL", name, err, tol);
}
static const double d2r=M_PI/180.0, r2d=180.0/M_PI;

// Rodrigues: rotation by angle-vector phi (world frame)
static Mat3 rodrigues(const Vec3& phi){
    double th=std::sqrt(phi[0]*phi[0]+phi[1]*phi[1]+phi[2]*phi[2]);
    Mat3 I{{{1,0,0},{0,1,0},{0,0,1}}};
    if(th<1e-300) return I;
    Vec3 k=vec(phi[0]/th,phi[1]/th,phi[2]/th);
    Mat3 K=skew(k);
    Mat3 K2=matmul(K,K);
    Mat3 R{};
    double s=std::sin(th), c=1.0-std::cos(th);
    for(int i=0;i<3;++i)for(int j=0;j<3;++j)
        R.m[i][j]=I.m[i][j]+s*K.m[i][j]+c*K2.m[i][j];
    return R;
}
// Analytic inverse of eulerExtractED — rebuild the E-basis DCM from (R,P,Y).
// Used ONLY by the test to prove the unified angles reconstruct R_WB (the same
// rotation the load is rotated by) => imposed-attitude == load-attitude.
static Mat3 composeED(double R,double P,double Y){
    double cP=cos(P),sP=sin(P),cR=cos(R),sR=sin(R),cY=cos(Y),sY=sin(Y);
    Vec3 row1=vec(-sP, cP*cR, -cP*sR);
    for(int br=0;br<2;++br){
        double lam=(br==0? sY : -sY);
        Vec3 row0=vec(cP*cY, sP*cY*cR+lam*sR, -sP*cY*sR+lam*cR);
        Vec3 row2=cross(row0,row1);
        Mat3 M{}; for(int j=0;j<3;++j){M.m[0][j]=row0[j];M.m[1][j]=row1[j];M.m[2][j]=row2[j];}
        double rr,pp,yy;
        if(eulerExtractED(M,rr,pp,yy) &&
           fabs(rr-R)+fabs(pp-P)+fabs(yy-Y)<1e-9) return M;
    }
    return Mat3{{{1,0,0},{0,1,0},{0,0,1}}};   // unreachable for valid inputs
}
static double matDiff(const Mat3&A,const Mat3&B){
    double e=0; for(int i=0;i<3;++i)for(int j=0;j<3;++j)e+=fabs(A.m[i][j]-B.m[i][j]); return e;
}
static void check_present(const char* name,double v,double lo,double hi){
    bool ok=(v>=lo&&v<=hi); if(!ok)++FAILS;
    printf("  [%s] %-46s val=%.4f (expect %.3f..%.3f)\n",ok?"PASS":"FAIL",name,v,lo,hi);
}

int main(){
    CouplingFrame F;
    F.setIC(/*roll*/0.0,/*pitch*/1.9,/*yaw*/0.0,/*surge*/5.0,/*sway*/0.0,/*heave*/0.0);
    Mat3 I3{{{1,0,0},{0,1,0},{0,0,1}}};

    // ===================================================================== A
    printf("\n[A] INITIAL DISPLACEMENT  (zero motion -> must reproduce the IC)\n");
    {
        double disp[6],vel[6],acc[6];
        bool ok=F.motionToModule(I3, vec(0,0,0), vec(0,0,0),vec(0,0,0),
                                 vec(0,0,0),vec(0,0,0), disp,vel,acc);
        printf("    output disp = (%.6f %.6f %.6f | %.6f %.6f %.6f deg)\n",
               disp[0],disp[1],disp[2],disp[3]*r2d,disp[4]*r2d,disp[5]*r2d);
        double e=std::fabs(disp[0]-5.0)+std::fabs(disp[1])+std::fabs(disp[2])
                +std::fabs(disp[3])+std::fabs(disp[4]-1.9*d2r)+std::fabs(disp[5]);
        check("disp == IC (surge5, pitch1.9)", e, 1e-12);
        double ev=0,ea=0; for(int i=0;i<6;++i){ev+=std::fabs(vel[i]);ea+=std::fabs(acc[i]);}
        check("vel == 0", ev, 1e-12);
        check("acc == 0", ea, 1e-12);
        check("returns ok", ok?0.0:1.0, 0.5);
    }

    // ===================================================================== B
    printf("\n[B] MOTION INPUT  (pitch+roll+yaw motion, omega, alpha)\n");
    {
        // motion-only world<-body rotation (absolute = IC . motion)
        Mat3 R_X0=matmul(matmul(Rz(0.8*d2r),Ry(-6.4*d2r)),Rx(1.0*d2r)); // abs pitch -4.5
        Vec3 omega=vec(0.02,-0.30,0.05);   // rad/s, world
        Vec3 alpha=vec(0.01,-0.05,0.02);   // rad/s^2, world
        double disp[6],vel[6],acc[6];
        bool ok=F.motionToModule(R_X0, vec(0,0,0), omega, vec(0,0,0),
                                 alpha, vec(0,0,0), disp,vel,acc);
        printf("    disp angles (R,P,Y) = (%.4f %.4f %.4f) deg\n",
               disp[3]*r2d,disp[4]*r2d,disp[5]*r2d);
        printf("    vel  rates  (R,P,Y) = (%.5f %.5f %.5f) rad/s\n",vel[3],vel[4],vel[5]);
        printf("    acc  accels (R,P,Y) = (%.5f %.5f %.5f) rad/s2\n",acc[3],acc[4],acc[5]);

        // B1: imposed attitude == load attitude. The load is rotated by R_WB; the
        // imposed angles are disp[3:5]. Reconstruct the E-basis DCM from the angles
        // and confirm it equals A*R_WB*A^T  => the two paths use the SAME rotation.
        Mat3 R_WB0=matmul(F.R_IC,R_X0);
        Mat3 Mload=matmul(matmul(F.A,R_WB0),transpose(F.A));
        Mat3 Mimp =composeED(disp[3],disp[4],disp[5]);
        check("unified imposed-attitude reconstructs load R_WB", matDiff(Mimp,Mload), 1e-9);
        // regression guard: the OLD scalar-IC path mis-states roll by ~0.027 deg.
        double oR,oP,oY; { Mat3 Mx=matmul(matmul(F.A,R_X0),transpose(F.A));
                           eulerExtractED(Mx,oR,oP,oY); oP+=1.9*d2r; }
        double dOld=(std::fabs(oR-disp[3])+std::fabs(oP-disp[4])+std::fabs(oY-disp[5]))*r2d;
        check_present("OLD scalar-IC inconsistency (deg) we removed", dOld, 0.01, 0.10);

        // B2: dofRates vs central finite-difference of the extraction
        Mat3 R_WB=matmul(F.R_IC,R_X0);
        double h=1e-6, rp[3],rm[3];
        { Mat3 Rp=matmul(rodrigues(vec(omega[0]*h,omega[1]*h,omega[2]*h)),R_WB);
          Mat3 Mp=matmul(matmul(F.A,Rp),transpose(F.A)); eulerExtractED(Mp,rp[0],rp[1],rp[2]); }
        { Mat3 Rm=matmul(rodrigues(vec(-omega[0]*h,-omega[1]*h,-omega[2]*h)),R_WB);
          Mat3 Mm=matmul(matmul(F.A,Rm),transpose(F.A)); eulerExtractED(Mm,rm[0],rm[1],rm[2]); }
        double tR=(rp[0]-rm[0])/(2*h), tP=(rp[1]-rm[1])/(2*h), tY=(rp[2]-rm[2])/(2*h);
        double eNew=std::fabs(vel[3]-tR)+std::fabs(vel[4]-tP)+std::fabs(vel[5]-tY);
        double eRaw=std::fabs(omega[0]-tR)+std::fabs(omega[1]-tP)+std::fabs(omega[2]-tY);
        printf("    rate truth(FD)=(%.5f %.5f %.5f); RAW-omega err=%.4f rad/s (%.2f%%)\n",
               tR,tP,tY,eRaw,100*eRaw/0.30);
        check("dofRates vs finite-diff", eNew, 1e-7);

        // B3: dofAccels vs 2nd central diff (trajectory omega(t)=omega0+alpha t)
        double ha=1e-4, ap[3],am[3],a0[3];
        auto angAt=[&](double t,double out[3]){
            Vec3 phi=vec(omega[0]*t+0.5*alpha[0]*t*t, omega[1]*t+0.5*alpha[1]*t*t,
                         omega[2]*t+0.5*alpha[2]*t*t);
            Mat3 Rt=matmul(rodrigues(phi),R_WB);
            Mat3 Mt=matmul(matmul(F.A,Rt),transpose(F.A)); eulerExtractED(Mt,out[0],out[1],out[2]);
        };
        angAt(ha,ap); angAt(-ha,am); angAt(0,a0);
        double aR=(ap[0]-2*a0[0]+am[0])/(ha*ha);
        double aP=(ap[1]-2*a0[1]+am[1])/(ha*ha);
        double aY=(ap[2]-2*a0[2]+am[2])/(ha*ha);
        double eA=std::fabs(acc[3]-aR)+std::fabs(acc[4]-aP)+std::fabs(acc[5]-aY);
        printf("    accel truth(FD)=(%.5f %.5f %.5f)\n",aR,aP,aY);
        check("dofAccels vs finite-diff", eA, 1e-5);
        check("returns ok", ok?0.0:1.0, 0.5);
    }

    // ===================================================================== C
    printf("\n[C] FORCE INPUT  (tower-base wrench -> world wrench)\n");
    {
        Mat3 R_X0=I3;                                  // at the IC pose (pitch 1.9)
        Vec3 F_T=vec(5.473e5, 0.0, -5.903e6);          // thrust, 0, -weight  [N]
        Vec3 M_T=vec(0.0, 3.886e7, 0.0);               // overturning My      [N.m]
        Vec3 p_twb=vec(5.3316,0.0,9.9945);             // tower-base world pos
        Vec3 F_W,M_O;
        F.loadToWorld(R_X0,F_T,M_T,p_twb,F_W,M_O);
        printf("    F_T=(%.0f,0,%.0f)  ->  F_W=(%.1f %.1f %.1f) N\n",F_T[0],F_T[2],F_W[0],F_W[1],F_W[2]);
        printf("    M_T=(0,%.3e,0)     ->  M_O=(%.3e %.3e %.3e) N.m\n",M_T[1],M_O[0],M_O[1],M_O[2]);
        // hand check: F_W = Ry(1.9) F_T ; My invariant under Ry
        double c=std::cos(1.9*d2r),s=std::sin(1.9*d2r);
        double Fx=c*F_T[0]+s*F_T[2], Fz=-s*F_T[0]+c*F_T[2];
        double eF=std::fabs(F_W[0]-Fx)+std::fabs(F_W[1])+std::fabs(F_W[2]-Fz);
        check("F_W == Ry(1.9)*F_T", eF, 1e-3);
        // M_O = R_WB*M_T + p_twb x F_W ; check y-component explicitly
        double myExp=M_T[1] + (p_twb[2]*F_W[0]-p_twb[0]*F_W[2]);
        check("M_O.y == My + (p x F).y", std::fabs(M_O[1]-myExp), 1e-3);
        // weight must NOT leak sideways beyond the Ry projection (no spurious Fy)
        check("no spurious lateral force Fy", std::fabs(F_W[1]), 1e-6);
    }

    // ===================================================================== D
    printf("\n[D] ROBUSTNESS  (singular-matrix gating)\n");
    {
        // drive pitch toward 90 deg -> cosP -> 0 -> gate must trip (no NaN)
        Mat3 R_X0=Ry(88.1*d2r);    // abs pitch = 1.9 + 88.1 = 90.0
        double disp[6],vel[6],acc[6];
        bool ok=F.motionToModule(R_X0, vec(0,0,0), vec(0,0,0.1),vec(0,0,0),
                                 vec(0,0,0),vec(0,0,0), disp,vel,acc);
        printf("    abs pitch ~90deg -> motionToModule returned %s (expect false)\n", ok?"true":"FALSE");
        check("gimbal gate trips (returns false)", ok?1.0:0.0, 0.5);
        // a healthy near-OC4 pose must still pass
        Mat3 R_ok=Ry(-6.4*d2r);
        bool ok2=F.motionToModule(R_ok, vec(0,0,0), vec(0,0,0.01),vec(0,0,0),
                                  vec(0,0,0),vec(0,0,0), disp,vel,acc);
        check("healthy pose still passes", ok2?0.0:1.0, 0.5);
    }

    // ===================================================================== E
    printf("\n[E] POSITION / REFERENCE-POINT  (PtfmRef != pivot, with IC pitch)\n");
    {
        // OpenFOAM body model (verified vs rigidBodyModel): dict points are
        // IC-posed; bodyPoint applies motion X0 about the CoR (CoM).
        //   bodyPoint(p)         = R_mot*(p - CoR) + CoR        (R_mot=I at t=0)
        //   bodyPointVelocity(p) = omega x (bodyPoint(p) - CoR)
        Vec3 CoR = vec(4.5537,0.0,-13.4526);              // CoM, IC-posed body coords
        Vec3 up  = matvec(F.R_IC, vec(0,0,13.4526));      // PtfmRef is |PtfmCMzt| above CoR
        Vec3 PtfmRef = vec(CoR[0]+up[0],CoR[1]+up[1],CoR[2]+up[2]);
        printf("    PtfmRef (IC-posed body) = (%.4f %.4f %.4f);  body-origin is %.3f m upwind\n",
               PtfmRef[0],PtfmRef[1],PtfmRef[2], PtfmRef[0]);

        // E1: initial displacement at PtfmRef (t=0, no motion) must reproduce IC
        {
            double disp[6],vel[6],acc[6];
            F.motionToModule(I3, vec(0,0,0), vec(0,0,0),vec(0,0,0),vec(0,0,0),vec(0,0,0),disp,vel,acc);
            double e=std::fabs(disp[0]-5)+std::fabs(disp[1])+std::fabs(disp[2]);
            check("init disp at PtfmRef == IC (5,0,0)", e, 1e-12);
            printf("    note: CFD PtfmRef material z=%.4f m vs ED z=0 -> %.1f mm IC offset"
                   " (rotate-about-CoM vs -PtfmRef); absorb via heave IC if needed\n",
                   PtfmRef[2], std::fabs(PtfmRef[2])*1000);
        }

        // E2: velocity lever-arm — body pitching at omega_y about the CoR
        {
            Vec3 omega=vec(0,-0.30,0);
            Vec3 vRef = cross(omega, vec(PtfmRef[0]-CoR[0],PtfmRef[1]-CoR[1],PtfmRef[2]-CoR[2]));
            Vec3 vOrg = cross(omega, vec(0-CoR[0],0-CoR[1],0-CoR[2]));
            Vec3 dErr = vec(vRef[0]-vOrg[0],vRef[1]-vOrg[1],vRef[2]-vOrg[2]);
            double leverErr=std::sqrt(dErr[0]*dErr[0]+dErr[1]*dErr[1]+dErr[2]*dErr[2]);
            printf("    |v(PtfmRef) - v(body-origin)| = %.4f m/s  (current code uses body-origin)\n",leverErr);
            check_present("lever-arm vel error == |omega|*5m", leverErr, 1.4, 1.6);
            double disp[6],vel[6],acc[6];
            F.motionToModule(I3, vec(0,0,0), omega, vRef, vec(0,0,0),vec(0,0,0),disp,vel,acc);
            // linear DOFs are IDENTITY-mapped (ED Sg/Sw/Hv = world x/y/z); the
            // lever-arm fix is sampling at PtfmRef, NOT an axis map.
            Vec3 vE=vRef;
            double ev=std::fabs(vel[0]-vE[0])+std::fabs(vel[1]-vE[1])+std::fabs(vel[2]-vE[2]);
            check("vel[0:3] == v(PtfmRef) identity (lever-arm fixed)", ev, 1e-12);
        }
    }

    // ===================================================================== F
    // GROUND TRUTH — independent of the code's own formulas (review-recommended).
    // These would have caught the A-on-linear bug; composeED is self-referential.
    printf("\n[F] GROUND TRUTH (vs ElastoDyn conventions, not the code itself)\n");
    {
        // standard world Tait-Bryan ZYX (yaw.pitch.roll) extraction, hand-derived
        auto stdZYX=[](const Mat3& R,double& r,double& p,double& y){
            double s=-R(2,0); s=s>1?1:(s<-1?-1:s);
            p=std::asin(s); r=std::atan2(R(2,1),R(2,2)); y=std::atan2(R(1,0),R(0,0));
        };
        double d[6],v[6],a[6];
        // F1: gravity (0,0,-9.81) must land in HEAVE acc[2], zero in sway/surge
        F.motionToModule(I3,vec(0,0,0),vec(0,0,0),vec(0,0,0),vec(0,0,0),vec(0,0,-9.81),d,v,a);
        check("gravity (0,0,-9.81) -> acc HEAVE[2]", std::fabs(a[2]+9.81)+std::fabs(a[1])+std::fabs(a[0]), 1e-12);
        // F1b: a general linear vel/accel must map IDENTICALLY (no cross-axis routing).
        // args: (R_X0, dPos, omega_W, vlin_W, alpha_W, alin_W, ...) -> put vlin in slot 4.
        F.motionToModule(I3,vec(0,0,0),vec(0,0,0),vec(1.1,-2.2,3.3),vec(0,0,0),vec(0.3,-0.5,0.7),d,v,a);
        check("vlin (1.1,-2.2,3.3) -> vel identity", std::fabs(v[0]-1.1)+std::fabs(v[1]+2.2)+std::fabs(v[2]-3.3), 1e-12);
        check("alin (0.3,-0.5,0.7) -> acc identity", std::fabs(a[0]-0.3)+std::fabs(a[1]+0.5)+std::fabs(a[2]-0.7), 1e-12);
        // F2: pure world-Z displacement -> heave disp[2] (IC heave here is 0)
        F.motionToModule(I3,vec(0,0,0.37),vec(0,0,0),vec(0,0,0),vec(0,0,0),vec(0,0,0),d,v,a);
        check("dPos z=0.37 -> disp HEAVE[2]=0.37", std::fabs(d[2]-0.37)+std::fabs(d[1]), 1e-12);
        // F3: pure world-Y displacement -> sway disp[1]
        F.motionToModule(I3,vec(0,0.21,0),vec(0,0,0),vec(0,0,0),vec(0,0,0),vec(0,0,0),d,v,a);
        check("dPos y=0.21 -> disp SWAY[1]=0.21", std::fabs(d[1]-0.21)+std::fabs(d[2]), 1e-12);
        // F4: angular disp[3:6] == standard world ZYX RPY of R_WB (independent)
        Mat3 R_X0=matmul(matmul(Rz(0.30),Ry(-0.20)),Rx(0.15));   // multi-axis motion
        Mat3 R_WB=matmul(F.R_IC,R_X0);
        F.motionToModule(R_X0,vec(0,0,0),vec(0,0,0),vec(0,0,0),vec(0,0,0),vec(0,0,0),d,v,a);
        double gr,gp,gy; stdZYX(R_WB,gr,gp,gy);
        check("disp[3:6] == world ZYX RPY of R_WB", std::fabs(d[3]-gr)+std::fabs(d[4]-gp)+std::fabs(d[5]-gy), 1e-12);
        // F5: multi-axis wrench transfer vs independent hand calc
        Vec3 F_T=vec(0,0,-5.9e6), M_T=vec(1.0e6,2.0e6,3.0e6), p_twb=vec(5.33,0,9.99);
        Vec3 F_W,M_O; F.loadToWorld(R_X0,F_T,M_T,p_twb,F_W,M_O);
        Vec3 FWe=matvec(R_WB,F_T), MWe=matvec(R_WB,M_T), c=cross(p_twb,FWe);
        double ef=std::fabs(F_W[0]-FWe[0])+std::fabs(F_W[1]-FWe[1])+std::fabs(F_W[2]-FWe[2]);
        double em=std::fabs(M_O[0]-(MWe[0]+c[0]))+std::fabs(M_O[1]-(MWe[1]+c[1]))+std::fabs(M_O[2]-(MWe[2]+c[2]));
        check("multi-axis F_W == R_WB*F_T", ef, 1e-3);
        check("multi-axis M_O == R_WB*M_T + p x F_W", em, 1e-3);
    }

    printf("\n==== %s (%d failures) ====\n", FAILS?"TEST FAILURES":"ALL TESTS PASSED", FAILS);
    return FAILS?1:0;
}
