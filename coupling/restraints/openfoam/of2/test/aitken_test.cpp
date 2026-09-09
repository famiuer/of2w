// aitken_test.cpp — unit test for the Aitken Δ² dynamic relaxation core.
// Build: g++ -std=c++17 -O2 -I.. aitken_test.cpp -o aitken_test
//
// Strategy: a STIFF linear fixed-point H(w)=a·w+b mimics the FSI added-mass map.
// For a<-3 the fixed-ω=0.5 iteration DIVERGES (oscillating, |r| grows); Aitken
// adapts ω -> 1/(1-a) and converges. We verify both, plus vector + clamp cases.
#include "../aitkenRelaxation.hpp"
#include <cstdio>
#include <cmath>
#include <vector>
using namespace of2;

static int FAILS=0;
static void check(const char* name,bool ok,const char* detail=""){
    if(!ok)++FAILS;
    printf("  [%s] %-46s %s\n", ok?"PASS":"FAIL", name, detail);
}

// run a scalar fixed-point H(w)=a*w+b with a given relaxation policy.
//   policy "fixed": ω=omegaF constant ; "aitken": Aitken core.
// returns final |residual| and #iters, and the final ω (aitken).
struct Result{ double rfin; int iters; double omega; bool blewup; };
static Result runScalar(double a,double b,double w0,bool aitken,double omegaF,int maxit){
    AitkenRelaxation ait; ait.configure(0.1,0.01,1.0); ait.resetStep();
    double w=w0; Result R{0,0,omegaF,false};
    for(int k=0;k<maxit;++k){
        double r = (a*w+b) - w;              // residual r = H(w)-w
        R.rfin=std::fabs(r); R.iters=k;
        if(std::fabs(r)<1e-12) return R;
        if(!std::isfinite(r) || std::fabs(r)>1e8){ R.blewup=true; return R; }
        double rv[3]={r,0.0,0.0};
        double om = aitken ? ait.update(rv) : omegaF;
        R.omega=om;
        w += om*r;
    }
    return R;
}

int main(){
    // ---- A: STIFF FSI-like map a=-4 (fixed-0.5 diverges, Aitken converges) ----
    printf("\n[A] STIFF map H(w)=-4w+5  (w*=1) — the FSI added-mass analogue\n");
    {
        Result fx = runScalar(-4,5,0,/*aitken*/false,0.5,40);
        printf("    fixed  w=0.5 : |r|=%.3e after %d it (blewup=%d)\n", fx.rfin, fx.iters, fx.blewup);
        check("fixed-0.5 DIVERGES on stiff map", fx.blewup || fx.rfin>1.0);

        Result ai = runScalar(-4,5,0,/*aitken*/true,0.0,40);
        printf("    aitken       : |r|=%.3e after %d it, final omega=%.4f\n", ai.rfin, ai.iters, ai.omega);
        check("aitken CONVERGES on stiff map", ai.rfin<1e-10 && !ai.blewup);
        check("aitken omega -> 1/(1-a)=0.2", std::fabs(ai.omega-0.2)<1e-6);
        check("aitken converges fast (<6 it)", ai.iters<6);
    }

    // ---- B: mild contracting map a=-0.5 (both fine, aitken still converges) ----
    printf("\n[B] MILD map H(w)=-0.5w+3  (w*=2)\n");
    {
        Result ai = runScalar(-0.5,3,0,true,0.0,60);
        check("aitken converges on mild map", ai.rfin<1e-10 && !ai.blewup);
    }

    // ---- C: vector map, mixed stiffness (scalar omega must still converge) ----
    printf("\n[C] VECTOR map diag(a)=(-4,-0.5,0.3) — one stiff direction\n");
    {
        const double a[3]={-4.0,-0.5,0.3}, b[3]={5.0,3.0,-1.0};
        // w* solves (a-1)w+b=0 -> w*=b/(1-a)
        AitkenRelaxation fxd; double wf[3]={0,0,0}; bool fblew=false;
        for(int k=0;k<60;++k){ double r[3]; double n=0;
            for(int i=0;i<3;++i){ r[i]=(a[i]*wf[i]+b[i])-wf[i]; n+=r[i]*r[i]; }
            if(std::sqrt(n)>1e8){fblew=true;break;}
            for(int i=0;i<3;++i) wf[i]+=0.5*r[i]; }
        AitkenRelaxation ait; ait.configure(0.1,0.01,1.0); ait.resetStep();
        double wa[3]={0,0,0}; double rn=1;
        for(int k=0;k<80;++k){ double r[3]; double n=0;
            for(int i=0;i<3;++i){ r[i]=(a[i]*wa[i]+b[i])-wa[i]; n+=r[i]*r[i]; }
            rn=std::sqrt(n); if(rn<1e-10) break;
            double om=ait.update(r);
            for(int i=0;i<3;++i) wa[i]+=om*r[i]; }
        printf("    fixed-0.5 blewup=%d ; aitken |r|=%.3e\n", fblew, rn);
        check("fixed-0.5 diverges on stiff vector dir", fblew);
        check("aitken converges on vector map", rn<1e-10);
        check("aitken solution = b/(1-a)",
              std::fabs(wa[0]-b[0]/(1-a[0]))+std::fabs(wa[1]-b[1]/(1-a[1]))+std::fabs(wa[2]-b[2]/(1-a[2]))<1e-8);
    }

    // ---- D: clamping (omegaMax forces a smaller factor; still bounded) --------
    printf("\n[D] CLAMP: omegaMax=0.1 on the stiff map\n");
    {
        AitkenRelaxation ait; ait.configure(0.1,0.01,0.1); ait.resetStep();  // max 0.1
        double w=0; double r=0; bool inrange=true;
        for(int k=0;k<200;++k){ r=(-4.0*w+5.0)-w; if(std::fabs(r)<1e-10) break;
            double rv[3]={r,0.0,0.0};
            double om=ait.update(rv);
            if(om<0.01-1e-12 || om>0.1+1e-12) inrange=false;
            w+=om*r; }
        check("omega stays within [0.01,0.1]", inrange);
        check("still converges under clamp", std::fabs(r)<1e-9);
    }

    // ---- E: |·| anti-flooring on a MONOTONE residual (the smoke pathology) ----
    // A monotonically growing residual makes the SIGNED Aitken factor negative,
    // which would clamp to omegaMin (freeze the wrench). The eq-41 |·| form must
    // keep ω positive and moderate instead.
    printf("\n[E] |.| keeps omega positive on a monotone residual (eq 41)\n");
    {
        AitkenRelaxation ait; ait.configure(0.5, 0.1, 1.0); ait.resetStep();
        double r0[3]={1,0,0};  ait.update(r0);          // seeds rPrev, returns warm 0.5
        double r1[3]={2,0,0};  double om = ait.update(r1); // monotone growth r1>r0
        // signed form would give -0.5 -> clamp to 0.1 (floor); |.| gives +0.5.
        check("monotone residual -> omega stays positive", om > 0.1 + 1e-9, "");
        check("|.| recovers omega=0.5 (not floored)", std::fabs(om-0.5)<1e-9, "");
        printf("    omega after monotone step = %.4f (floor=0.1)\n", om);
    }

    printf("\n==== %s (%d failures) ====\n", FAILS?"TEST FAILURES":"ALL TESTS PASSED", FAILS);
    return FAILS?1:0;
}
