// aitkenRelaxation.hpp — dependency-free Aitken Δ² (Irons-Tuck) dynamic relaxation
// for the partitioned OF² FSI coupling. Replaces a fixed under-relaxation factor
// with one adapted from the residual SECANT, so a stiff (added-mass) coupling that
// diverges at fixed ω converges. NO OpenFOAM dependency -> unit-testable with g++.
//
// Fixed-point per CFD step, corrector k:   w^{k+1} = w^k + ω^k · r^k
//   r^k        = residual (here the MOMENT residual, 3-vector, = wrenchNew − cached)
//   ω^k (k≥1)  = −ω^{k-1} · ⟨r^{k-1}, Δr⟩ / ⟨Δr,Δr⟩ ,   Δr = r^k − r^{k-1}
// For an FSI added-mass map H'(<0, |H'|>1) the optimal ω is a positive under-
// relaxation in (0,1); ω is clamped to [omegaMin, omegaMax] for robustness.
// See coupling_unified_design.md / the FSI-instability analysis.
#ifndef OF2_AITKEN_RELAXATION_HPP
#define OF2_AITKEN_RELAXATION_HPP

#include <cmath>

namespace of2 {

struct AitkenRelaxation
{
    double omega    = 0.5;     // current relaxation factor ω (carries across steps)
    double omegaMin = 0.1;     // clamp lower bound (Chow & Ng 2016: ω in [0.1,1])
    double omegaMax = 1.0;     // clamp upper bound (pure under-relaxation by default)
    double rPrev[3] = {0,0,0}; // previous residual r^{k-1}
    bool   hasPrev  = false;   // false at the first corrector of a CFD step

    //- Set parameters (call once at read()).
    void configure(double omegaInit, double omin, double omax)
    {
        omegaMin = omin; omegaMax = omax;
        omega = clampOmega(omegaInit);
    }

    //- Start of a new CFD step: warm-start ω from the previous step (clamped),
    //  clear the residual history so corrector 0 uses the warm-started ω.
    void resetStep()
    {
        omega   = clampOmega(omega);
        hasPrev = false;
    }

    //- One corrector. Pass the current residual r (3-vector). Returns ω to apply
    //  as  w += ω·(full residual).  Updates internal state for the next corrector.
    double update(const double r[3])
    {
        if (hasPrev)
        {
            const double dr[3] = { r[0]-rPrev[0], r[1]-rPrev[1], r[2]-rPrev[2] };
            const double den   = dr[0]*dr[0] + dr[1]*dr[1] + dr[2]*dr[2];
            if (den > 1.0e-300)   // guard ÷0 if the residual didn't change
            {
                const double num = rPrev[0]*dr[0] + rPrev[1]*dr[1] + rPrev[2]*dr[2];
                // Aitken Δ² (Küttler-Wall) factor, eq (41) form: take the
                // MAGNITUDE so ω stays POSITIVE even on a monotone (non-
                // oscillatory) residual. The signed form (−ω·num/den) returns a
                // negative factor there and would pin ω to the floor, FREEZING
                // the wrench (the failure seen in the 12-corrector smoke). For a
                // contracting/oscillatory residual |·| equals the signed value,
                // so the optimal ω=1/(1−a) is recovered exactly.
                omega = std::fabs(omega * num / den);
            }
            omega = clampOmega(omega);
        }
        rPrev[0]=r[0]; rPrev[1]=r[1]; rPrev[2]=r[2];
        hasPrev = true;
        return omega;
    }

    double clampOmega(double w) const
    {
        if (!std::isfinite(w)) return omegaMin;
        if (w < omegaMin) return omegaMin;
        if (w > omegaMax) return omegaMax;
        return w;
    }
};

} // namespace of2
#endif
