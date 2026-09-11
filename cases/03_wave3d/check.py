#!/usr/bin/env python3
"""Regular-wave generation check (waves2Foam, Stokes-II H=6 m, T=10 s,
depth 80 m).

Reads the consolidated wave-gauge file written by the waves2Foam
surfaceElevation function object (postProcessing/surfaceElevation/<t0>/
surfaceElevation.dat: one row per instant, one column per gauge) and
verifies the delivered wave against the target:
  1. eta(t) per gauge -> FFT fundamental over the steady window -> A(x).
  2. measured wavenumber k from the gauge-phase slope.
  3. least-squares incident/reflected split A_i, A_r -> H = 2 A_i, R.
PASS if H, k and R are within tolerance. Exit code 0/1.
"""
import glob
import os
import sys
import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
H0, T, DEPTH = 6.0, 10.0, 80.0
TWIN = (160.0, 300.0)          # steady window (integer periods used below)
XCLEAN = (-240.0, 190.0)       # gauges outside both relaxation zones
TOL_H, TOL_K, TOL_R = 0.06, 0.02, 0.10   # |H err|, |k err|, max reflection

g = 9.81
w = 2*np.pi/T
k0 = w*w/g
for _ in range(60):
    k0 = w*w/(g*np.tanh(k0*DEPTH))

fs = sorted(glob.glob(f"{HERE}/postProcessing/surfaceElevation/*/surfaceElevation.dat"))
if not fs:
    sys.exit("FAIL — no surfaceElevation.dat found (run the case first)")
raw = np.loadtxt(fs[-1], skiprows=1)
xg, dat = raw[0, 1:], raw[3:]
tv = dat[:, 0]
t1 = min(TWIN[1], tv[-1])
t1 = TWIN[0] + T*int((t1 - TWIN[0])/T)         # integer number of periods
win = (tv >= TWIN[0]) & (tv <= t1)
if win.sum() < 4*T:
    sys.exit(f"FAIL — run too short: gauges end at t={tv[-1]:.0f} s, "
             f"need > {TWIN[0] + 4*T:.0f} s")
tw = tv[win]
N = len(tw)
hn = np.hanning(N)
fr = np.fft.rfftfreq(N, np.median(np.diff(tw)))
i1 = np.argmin(abs(fr - 1/T))

order = np.argsort(xg)
X, C1 = [], []
for j in order:
    s = dat[win, 1 + j]
    F = np.fft.rfft((s - s.mean())*hn)
    X.append(xg[j])
    C1.append(F[i1]*2/hn.sum())
X, C1 = np.array(X), np.array(C1)
m = (X > XCLEAN[0]) & (X < XCLEAN[1])

ks = -np.polyfit(X[m], np.unwrap(np.angle(C1[m])), 1)[0]
G = np.vstack([np.exp(-1j*ks*X[m]), np.exp(1j*ks*X[m])]).T
coef = np.linalg.lstsq(G, C1[m], rcond=None)[0]
Ai, Ar = abs(coef[0]), abs(coef[1])
Hm, R = 2*Ai, Ar/Ai

print(f"window {TWIN[0]:.0f}-{t1:.0f} s, {m.sum()} gauges in x "
      f"[{XCLEAN[0]:.0f},{XCLEAN[1]:.0f}]")
print(f"incident H = {Hm:.3f} m  (target {H0}, {100*(Hm/H0 - 1):+.1f}%)")
print(f"wavenumber k = {ks:.5f}  (theory {k0:.5f}, {100*(ks/k0 - 1):+.2f}%)")
print(f"reflection R = {100*R:.1f}%")

fails = []
if abs(Hm/H0 - 1) > TOL_H:
    fails.append(f"wave height off by {100*(Hm/H0 - 1):+.1f}% (tol {100*TOL_H:.0f}%)")
if abs(ks/k0 - 1) > TOL_K:
    fails.append(f"wavenumber off by {100*(ks/k0 - 1):+.2f}% (tol {100*TOL_K:.0f}%)")
if R > TOL_R:
    fails.append(f"reflection {100*R:.1f}% (tol {100*TOL_R:.0f}%)")
if fails:
    print("FAIL — " + "; ".join(fails))
    sys.exit(1)
print("PASS — regular wave generated within tolerance "
      f"(H {100*(Hm/H0 - 1):+.1f}%, k {100*(ks/k0 - 1):+.2f}%, R {100*R:.1f}%)")
