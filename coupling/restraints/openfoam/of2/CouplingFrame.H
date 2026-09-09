// CouplingFrame.H — Foam-typed adapter over the verified frame core.
//
// The matrix algebra lives ONCE in frameAlgebra.hpp (unit-tested to machine
// precision by test/frameAlgebra_test.cpp). This header only converts between
// Foam::tensor/vector and of2::Mat3/Vec3 and forwards — so the production .so
// runs the exact code the test proved. See coupling_unified_design.md.
#ifndef OF2_COUPLING_FRAME_H
#define OF2_COUPLING_FRAME_H

#include "tensor.H"
#include "vector.H"
#include "frameAlgebra.hpp"

namespace Foam { namespace RBD { namespace restraints {

// ---- type bridges (Foam tensor is row-major: xx xy xz / yx yy yz / zx zy zz) -
inline of2::Mat3 toMat3(const Foam::tensor& t){
    of2::Mat3 M{}; M.m[0][0]=t.xx();M.m[0][1]=t.xy();M.m[0][2]=t.xz();
                   M.m[1][0]=t.yx();M.m[1][1]=t.yy();M.m[1][2]=t.yz();
                   M.m[2][0]=t.zx();M.m[2][1]=t.zy();M.m[2][2]=t.zz(); return M;
}
inline of2::Vec3 toVec3(const Foam::vector& v){ return of2::vec(v.x(),v.y(),v.z()); }
inline Foam::vector toFoam(const of2::Vec3& v){ return Foam::vector(v[0],v[1],v[2]); }

// ---- the coupling frame, Foam-facing -----------------------------------------
class CouplingFrame
{
    of2::CouplingFrame core_;

public:
    //- Set the initial-condition pose ONCE (degrees + metres). Mirrors the IC
    //  baked into the mesh; the ONLY place the IC is encoded for the coupling.
    void setIC(Foam::scalar rollDeg, Foam::scalar pitchDeg, Foam::scalar yawDeg,
               Foam::scalar surge, Foam::scalar sway, Foam::scalar heave)
    {
        core_.setIC(rollDeg,pitchDeg,yawDeg,surge,sway,heave);
    }

    //- Motion W -> E for OpenFAST. R_X0 = motion-only world<-body rotation
    //  (model_.X0(body).E().T()); dPosW = PtfmRef motion displacement; the rest
    //  are world-frame body kinematics at PtfmRef. Fills disp/vel/acc (E frame).
    //  Returns false on a singular (gimbal) pose -> caller must abort.
    bool motionToModule
    (
        const Foam::tensor& R_X0,
        const Foam::vector& dPosW,
        const Foam::vector& omegaW, const Foam::vector& vlinW,
        const Foam::vector& alphaW, const Foam::vector& alinW,
        double disp[6], double vel[6], double acc[6]
    ) const
    {
        return core_.motionToModule
        (
            toMat3(R_X0), toVec3(dPosW),
            toVec3(omegaW), toVec3(vlinW), toVec3(alphaW), toVec3(alinW),
            disp, vel, acc
        );
    }

    //- Load T -> W. F_T,M_T = ED tower-base reaction; p_twbW = tower-base world
    //  position. Returns the world force and the moment about the RBD origin.
    void loadToWorld
    (
        const Foam::tensor& R_X0,
        const Foam::vector& F_T, const Foam::vector& M_T,
        const Foam::vector& p_twbW,
        Foam::vector& F_W, Foam::vector& M_O_W
    ) const
    {
        of2::Vec3 fW, mO;
        core_.loadToWorld(toMat3(R_X0), toVec3(F_T), toVec3(M_T), toVec3(p_twbW), fW, mO);
        F_W   = toFoam(fW);
        M_O_W = toFoam(mO);
    }
};

}}} // namespace Foam::RBD::restraints
#endif
