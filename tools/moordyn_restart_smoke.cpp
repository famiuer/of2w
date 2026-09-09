// MoorDyn v2.6 SaveState/LoadState round-trip smoke test (restart phase 3
// feasibility). Drives the OC4 deck coupled body with a surge oscillation,
// saves at t=5 s, restores into a fresh instance (Init_NoIC + LoadState),
// runs both to t=8 s and compares the coupled-DOF forces.
// RESULT (2026-08-05, v2.6, cases/oc4-decay-v6 deck): PASS, bit-exact
// (max|dF| = 0.0 at max|F| = 2.19e6 N over 240 steps).
// Build:  g++ -O2 -I$HOME/.local/include moordyn_restart_smoke.cpp \
//             -L$HOME/.local/lib -lmoordyn -Wl,-rpath,$HOME/.local/lib -o smoke
// Run in a dir containing the deck as lines_oc4.txt.
#include <moordyn/MoorDyn2.h>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

static void pose(double t, double* x, double* xd, unsigned n)
{
    std::memset(x, 0, n*sizeof(double));
    std::memset(xd, 0, n*sizeof(double));
    const double A = 0.5, w = 2.0*M_PI/10.0;   // 0.5 m surge @ T=10 s
    x[0]  = A*std::sin(w*t);
    xd[0] = A*w*std::cos(w*t);
}

int main()
{
    const double dt = 0.0125, tSave = 5.0, tEnd = 8.0;

    MoorDyn s1 = MoorDyn_Create("lines_oc4.txt");
    if (!s1) { std::printf("FAIL: Create s1\n"); return 1; }
    unsigned n = 0;
    MoorDyn_NCoupledDOF(s1, &n);
    std::printf("coupled DOF: %u\n", n);
    std::vector<double> x(n), xd(n), f1(n), f2(n);

    pose(0.0, x.data(), xd.data(), n);
    if (MoorDyn_Init(s1, x.data(), xd.data()) != MOORDYN_SUCCESS)
    { std::printf("FAIL: Init s1\n"); return 1; }

    // leg A: 0 -> 5 s
    double t = 0.0;
    while (t < tSave - 0.5*dt)
    {
        double tt = t, hh = dt;
        pose(t + dt, x.data(), xd.data(), n);
        if (MoorDyn_Step(s1, x.data(), xd.data(), f1.data(), &tt, &hh)
            != MOORDYN_SUCCESS) { std::printf("FAIL: Step A t=%g\n", t); return 1; }
        t += dt;
    }
    if (MoorDyn_SaveState(s1, "md_state.bin") != MOORDYN_SUCCESS)
    { std::printf("FAIL: SaveState\n"); return 1; }
    std::printf("SaveState at t=%.4f ok\n", t);

    // fresh instance restored at t=5
    MoorDyn s2 = MoorDyn_Create("lines_oc4.txt");
    if (!s2) { std::printf("FAIL: Create s2\n"); return 1; }
    pose(t, x.data(), xd.data(), n);
    if (MoorDyn_Init_NoIC(s2, x.data(), xd.data()) != MOORDYN_SUCCESS)
    { std::printf("FAIL: Init_NoIC\n"); return 1; }
    if (MoorDyn_LoadState(s2, "md_state.bin") != MOORDYN_SUCCESS)
    { std::printf("FAIL: LoadState\n"); return 1; }
    std::printf("LoadState ok\n");

    // both legs: 5 -> 8 s, compare forces
    double maxAbs = 0.0, maxDiff = 0.0, t2 = t;
    while (t < tEnd - 0.5*dt)
    {
        double ta = t, tb = t2, ha = dt, hb = dt;
        pose(t + dt, x.data(), xd.data(), n);
        if (MoorDyn_Step(s1, x.data(), xd.data(), f1.data(), &ta, &ha)
            != MOORDYN_SUCCESS) { std::printf("FAIL: Step A2\n"); return 1; }
        if (MoorDyn_Step(s2, x.data(), xd.data(), f2.data(), &tb, &hb)
            != MOORDYN_SUCCESS) { std::printf("FAIL: Step B\n"); return 1; }
        for (unsigned i = 0; i < n; ++i)
        {
            maxAbs  = std::max(maxAbs,  std::fabs(f1[i]));
            maxDiff = std::max(maxDiff, std::fabs(f1[i] - f2[i]));
        }
        t += dt; t2 += dt;
    }
    const double rel = maxDiff/(maxAbs > 0 ? maxAbs : 1.0);
    std::printf("max|F| = %.6e N, max|dF| = %.6e N, rel = %.3e\n",
                maxAbs, maxDiff, rel);
    std::printf(rel < 1e-6 ? "PASS\n" : "FAIL: divergence\n");
    MoorDyn_Close(s1);
    MoorDyn_Close(s2);
    return rel < 1e-6 ? 0 : 2;
}
