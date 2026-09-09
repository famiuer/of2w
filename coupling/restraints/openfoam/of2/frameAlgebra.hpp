// frameAlgebra.hpp — dependency-free core of the OF² coupling frame contract.
//
// Pure matrix algebra for the W<->E<->T reference-frame conversions (see
// coupling_unified_design.md). NO OpenFOAM / OpenFAST dependency so it can be
// unit-tested with plain g++. CouplingFrame.H mirrors these exact formulas with
// Foam::tensor/vector; keep the two in lock-step.
//
// Conventions:
//   Vec3 v        : 3-vector, v[0..2]
//   Mat3 M        : row-major 3x3, M(i,j)
//   frame W = OpenFOAM world, E = ElastoDyn DOF axes (A = W->E), T = ED load frame
#ifndef OF2_FRAME_ALGEBRA_HPP
#define OF2_FRAME_ALGEBRA_HPP

#include <cmath>

namespace of2 {

constexpr double EPS_COSP = 1.0e-6;   // gimbal-lock guard on |cos(pitch)|
constexpr double EPS2     = 1.0e-12;  // degenerate-denominator guard

struct Vec3 {
    double a[3];
    double  operator[](int i) const { return a[i]; }
    double& operator[](int i)       { return a[i]; }
};

struct Mat3 {
    double m[3][3];
    double  operator()(int i,int j) const { return m[i][j]; }
    double& operator()(int i,int j)       { return m[i][j]; }
};

inline Vec3 vec(double x,double y,double z){ return Vec3{{x,y,z}}; }

inline Mat3 matmul(const Mat3& A,const Mat3& B){
    Mat3 C{};
    for(int i=0;i<3;++i) for(int j=0;j<3;++j){
        double s=0; for(int k=0;k<3;++k) s+=A.m[i][k]*B.m[k][j]; C.m[i][j]=s;
    }
    return C;
}
inline Vec3 matvec(const Mat3& A,const Vec3& v){
    Vec3 r{};
    for(int i=0;i<3;++i){ double s=0; for(int k=0;k<3;++k) s+=A.m[i][k]*v.a[k]; r.a[i]=s; }
    return r;
}
inline Mat3 transpose(const Mat3& A){
    Mat3 T{}; for(int i=0;i<3;++i) for(int j=0;j<3;++j) T.m[i][j]=A.m[j][i]; return T;
}
inline Vec3 cross(const Vec3& u,const Vec3& v){
    return vec(u[1]*v[2]-u[2]*v[1], u[2]*v[0]-u[0]*v[2], u[0]*v[1]-u[1]*v[0]);
}
// skew(w): the matrix S with S*x == cross(w,x)
inline Mat3 skew(const Vec3& w){
    Mat3 S{}; S.m[0][0]=0; S.m[0][1]=-w[2]; S.m[0][2]= w[1];
              S.m[1][0]= w[2]; S.m[1][1]=0; S.m[1][2]=-w[0];
              S.m[2][0]=-w[1]; S.m[2][1]= w[0]; S.m[2][2]=0; return S;
}

// elementary rotations (world<-body active)
inline Mat3 Rx(double t){ double c=std::cos(t),s=std::sin(t);
    return Mat3{{{1,0,0},{0,c,-s},{0,s,c}}}; }
inline Mat3 Ry(double t){ double c=std::cos(t),s=std::sin(t);
    return Mat3{{{c,0,s},{0,1,0},{-s,0,c}}}; }
inline Mat3 Rz(double t){ double c=std::cos(t),s=std::sin(t);
    return Mat3{{{c,-s,0},{s,c,0},{0,0,1}}}; }

// The constant W->E axis map A (== frameOfToEdRotation's P).
inline Mat3 axisMapWtoE(){ return Mat3{{{1,0,0},{0,0,1},{0,-1,0}}}; }

// ---- Euler extraction (exact replica of decomposeEdEulerRPY), gated --------
// M = A * R_WB * A^T  (body orientation expressed in the E basis).
// Returns false on gimbal lock (|cosP| < EPS_COSP).
inline bool eulerExtractED(const Mat3& M, double& R,double& P,double& Y){
    double m10=M(1,0), m11=M(1,1), m12=M(1,2), m00=M(0,0), m20=M(2,0);
    double sinP = -m10; if(sinP> 1.0) sinP= 1.0; if(sinP<-1.0) sinP=-1.0;
    P = std::asin(sinP);
    double cosP = std::cos(P);
    if(std::fabs(cosP) < EPS_COSP) return false;     // <-- singular gate
    R = std::atan2(-m12, m11);
    Y = std::atan2(-m20, m00);
    return true;
}

// ---- DOF rates: J^{-1}(M) * omega_E, derived by differentiating the
// extraction. omegaE = A*omega_W. Mdot = skew(omegaE)*M. Gated.
inline bool dofRates(const Mat3& M,const Mat3& Mdot, double out[3]){
    double m10=M(1,0);
    double cosP = std::sqrt(std::fmax(0.0, 1.0 - m10*m10));
    if(cosP < EPS_COSP) return false;
    double denR = M(1,1)*M(1,1)+M(1,2)*M(1,2);
    double denY = M(0,0)*M(0,0)+M(2,0)*M(2,0);
    if(denR < EPS2 || denY < EPS2) return false;
    double Pdot = -Mdot(1,0)/cosP;
    double Rdot = (M(1,2)*Mdot(1,1) - M(1,1)*Mdot(1,2))/denR;
    double Ydot = (M(2,0)*Mdot(0,0) - M(0,0)*Mdot(2,0))/denY;
    out[0]=Rdot; out[1]=Pdot; out[2]=Ydot; return true;
}

// ---- DOF accelerations: d/dt of dofRates. Gated.
inline bool dofAccels(const Mat3& M,const Mat3& Mdot,const Mat3& Mddot, double out[3]){
    double m10=M(1,0);
    double cosP = std::sqrt(std::fmax(0.0, 1.0 - m10*m10));
    if(cosP < EPS_COSP) return false;
    double sinP = -m10;
    double denR = M(1,1)*M(1,1)+M(1,2)*M(1,2);
    double denY = M(0,0)*M(0,0)+M(2,0)*M(2,0);
    if(denR < EPS2 || denY < EPS2) return false;

    double Pdot  = -Mdot(1,0)/cosP;
    double Pddot = -Mddot(1,0)/cosP - Mdot(1,0)*sinP*Pdot/(cosP*cosP);

    double numR  =  M(1,2)*Mdot(1,1) - M(1,1)*Mdot(1,2);
    double dnumR =  M(1,2)*Mddot(1,1) - M(1,1)*Mddot(1,2);     // cross terms cancel
    double ddenR =  2.0*(M(1,1)*Mdot(1,1) + M(1,2)*Mdot(1,2));
    double Rddot = (dnumR*denR - numR*ddenR)/(denR*denR);

    double numY  =  M(2,0)*Mdot(0,0) - M(0,0)*Mdot(2,0);
    double dnumY =  M(2,0)*Mddot(0,0) - M(0,0)*Mddot(2,0);
    double ddenY =  2.0*(M(0,0)*Mdot(0,0) + M(2,0)*Mdot(2,0));
    double Yddot = (dnumY*denY - numY*ddenY)/(denY*denY);

    out[0]=Rddot; out[1]=Pddot; out[2]=Yddot; return true;
}

// ============================================================================
// High-level contract.  All inputs in W; outputs disp/vel/acc in E, load in W.
// ============================================================================
struct CouplingFrame {
    Mat3 A      = axisMapWtoE();
    Mat3 R_IC   = Mat3{{{1,0,0},{0,1,0},{0,0,1}}};   // identity until setIC()
    Vec3 pIC_E  = vec(0,0,0);         // (surge,sway,heave) IC in E frame
    Vec3 pOrigin= vec(0,0,0);         // RBD moment reference

    void setIC(double rollDeg,double pitchDeg,double yawDeg,
               double surge,double sway,double heave){
        const double d=M_PI/180.0;
        R_IC = matmul(matmul(Rz(yawDeg*d), Ry(pitchDeg*d)), Rx(rollDeg*d));
        pIC_E = vec(surge,sway,heave);
    }

    // motion W -> E.  R_X0 = motion-only world<-body rotation (model_.X0.E().T()).
    // dPos_W = PtfmRef motion displacement; omega/vlin/alpha/alin in W.
    // Returns false (singular) -> caller aborts.
    bool motionToModule(const Mat3& R_X0,const Vec3& dPos_W,
                        const Vec3& omega_W,const Vec3& vlin_W,
                        const Vec3& alpha_W,const Vec3& alin_W,
                        double disp[6],double vel[6],double acc[6]) const {
        Mat3 R_WB = matmul(R_IC, R_X0);
        Mat3 M    = matmul(matmul(A, R_WB), transpose(A));   // E-basis orientation
        double Rr,Pp,Yy;
        if(!eulerExtractED(M, Rr,Pp,Yy)) return false;

        // Linear DOFs (surge/sway/heave) are IDENTITY-mapped: ED's inertial
        // frame aligns with OF world (x-downwind, z-up). The axis map A is ONLY
        // plumbing for the rotation/Euler extraction below, NOT the linear
        // translation (applying A here mis-routed gravity into sway).
        disp[0]=dPos_W[0]+pIC_E[0]; disp[1]=dPos_W[1]+pIC_E[1]; disp[2]=dPos_W[2]+pIC_E[2];
        disp[3]=Rr; disp[4]=Pp; disp[5]=Yy;

        Vec3 omE = matvec(A, omega_W);
        Vec3 alE = matvec(A, alpha_W);
        Mat3 Mdot  = matmul(skew(omE), M);
        Mat3 Mddot = matmul(skew(alE), M);
        { Mat3 t=matmul(skew(omE), Mdot);
          for(int i=0;i<3;++i) for(int j=0;j<3;++j) Mddot.m[i][j]+=t.m[i][j]; }

        double r3[3], a3[3];
        if(!dofRates (M,Mdot,r3))        return false;
        if(!dofAccels(M,Mdot,Mddot,a3))  return false;
        // Linear vel/acc: identity (world x,y,z) -> ED Sg/Sw/Hv. Angular: r3/a3
        // from the Euler-rate Jacobian are already ED R/P/Y DOF rates.
        vel[0]=vlin_W[0];vel[1]=vlin_W[1];vel[2]=vlin_W[2]; vel[3]=r3[0];vel[4]=r3[1];vel[5]=r3[2];
        acc[0]=alin_W[0];acc[1]=alin_W[1];acc[2]=alin_W[2]; acc[3]=a3[0];acc[4]=a3[1];acc[5]=a3[2];
        return true;
    }

    // load T -> W.  F_T,M_T = ED tower-base reaction; p_twb_W = tower-base world pos.
    void loadToWorld(const Mat3& R_X0,const Vec3& F_T,const Vec3& M_T,
                     const Vec3& p_twb_W, Vec3& F_W,Vec3& M_O_W) const {
        Mat3 R_WB = matmul(R_IC, R_X0);
        F_W = matvec(R_WB, F_T);
        Vec3 M_W = matvec(R_WB, M_T);
        Vec3 lever = vec(p_twb_W[0]-pOrigin[0],p_twb_W[1]-pOrigin[1],p_twb_W[2]-pOrigin[2]);
        Vec3 c = cross(lever, F_W);
        M_O_W = vec(M_W[0]+c[0], M_W[1]+c[1], M_W[2]+c[2]);
    }
};

} // namespace of2
#endif
