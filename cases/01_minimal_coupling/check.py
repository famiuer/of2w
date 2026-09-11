#!/usr/bin/env python3
"""Restart bench verdict: overlay the stop/restart twin (runB) on the
uninterrupted reference (runA) and quantify the mismatch over the
post-splice window. Channels cover all three coupled subsystems:
  OpenFOAM/RBD : surge + pitch (imposed ED disp from the [OF2] log lines)
  coupling     : tower-base F and raw M (same lines)
  MoorDyn      : FairTen2 (Mooring/lines_oc4.out)
  OpenFAST     : RotSpeed, BldPitch1, TwrBsMyt (openfast/derisk.out —
                 drivetrain, servo/DISCON and structural state)
Output: bench_overlay.png + printed rms table."""
import re
import numpy as np
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt

# LaTeX-style rendering when a TeX toolchain is available (else default fonts)
import shutil
if all(shutil.which(b) for b in ("latex", "dvipng", "gs")):
    plt.rcParams.update({
        "text.usetex": True, "font.family": "serif",
        "font.serif": ["Computer Modern Roman"], "axes.labelsize": 11,
        "legend.fontsize": 10, "xtick.labelsize": 10, "ytick.labelsize": 10,
    })

import os
B = os.path.dirname(os.path.abspath(__file__))
SPLICE = 0.5

RE = re.compile(
    r"\[OF2\] step \d+\s+t=([\deE.+-]+).*?ED disp=\(([^)]*)\).*?"
    r"TwrBs F=\(([^)]*)\) N\s+Mraw=\(([^)]*)\)", re.S)

def of2_series(*logs):
    rows = {}
    for lg in logs:
        txt = open(lg, errors="ignore").read()
        for m in RE.finditer(txt):
            t = float(m.group(1))
            d = [float(x.rstrip("deg")) for x in m.group(2).split(",")]
            F = [float(x) for x in m.group(3).split()]
            M = [float(x) for x in m.group(4).split()]
            rows[round(t, 6)] = [t, d[0], d[4], F[0], M[1]]  # surge,pitch,Fx,My
    a = np.array([rows[k] for k in sorted(rows)])
    return a

def mooring(path):
    d = np.loadtxt(path, skiprows=2, ndmin=2)
    d = d[d[:, 2] > 0]                # drop the pre-LoadState zero row
    return d[:, 0], d[:, 2] / 1e3     # t, FairTen2 [kN]

OF_CHANS = ["RotSpeed", "BldPitch1", "TwrBsMyt"]

def openfast_out(path):
    """Parse an OpenFAST .out: strong coupling re-fires OpenFAST every outer
    corrector, so times repeat — keep the LAST row per time (the converged
    corrector). NaN rows (e.g. appended by an aborted restart attempt) are
    dropped. Returns t plus the OF_CHANS columns."""
    lines = open(path, errors="ignore").read().splitlines()
    ihdr = next(i for i, l in enumerate(lines) if l.startswith("Time"))
    names = lines[ihdr].split("\t")
    cols = [names.index(c) for c in OF_CHANS]
    d = np.genfromtxt(path, skip_header=ihdr + 2, invalid_raise=False)
    d = d[~np.isnan(d[:, [0] + cols]).any(axis=1)]
    last = {}                          # per-corrector duplicates: last wins
    for i, tv in enumerate(d[:, 0]):
        last[round(tv, 6)] = i
    d = d[[last[k] for k in sorted(last)]]
    return d[:, 0], [d[:, c] for c in cols]

A  = of2_series(f"{B}/runA/log.solver")
Bt = of2_series(f"{B}/runB/log.solver.seg1", f"{B}/runB/log.solver.seg2")
tA, tB = A[:, 0], Bt[:, 0]
mtA, f2A = mooring(f"{B}/runA/Mooring/lines_oc4.out")
# seg2's restarted MoorDyn truncates the file and stamps rows with its
# INTERNAL clock (restarts at 0 after Init_NoIC+LoadState): shift by SPLICE.
mtB, f2B = mooring(f"{B}/runB/Mooring/lines_oc4.out")
mtB = mtB + SPLICE

print(f"runA: {len(tA)} steps to t={tA[-1]:.3f} | "
      f"runB: {len(tB)} steps to t={tB[-1]:.3f}")

# rms over the post-splice overlap on a common grid
tg = np.arange(SPLICE + 0.01, min(tA[-1], tB[-1]), 0.005)
chans = [("surge [m]", 1), ("pitch [deg]", 2),
         ("TwrBs Fx [N]", 3), ("TwrBs My,raw [N m]", 4)]
VER = []                              # (name, rms, signal_std) for verdict
print(f"\nrms(runB - runA) over t = {SPLICE}..{tg[-1]:.2f} s:")
for name, c in chans:
    d = np.interp(tg, tB, Bt[:, c]) - np.interp(tg, tA, A[:, c])
    ref = np.std(np.interp(tg, tA, A[:, c])) or 1.0
    VER.append((name, np.sqrt(np.mean(d**2)), ref))
    print(f"  {name:<20} rms = {np.sqrt(np.mean(d**2)):.4e}"
          f"   (signal std {ref:.3e})")
mg = tg[tg <= min(mtA[-1], mtB[-1])]
dT2 = np.interp(mg, mtB, f2B) - np.interp(mg, mtA, f2A)
VER.append(("FairTen2 [kN]", np.sqrt(np.mean(dT2**2)),
            np.std(np.interp(mg, mtA, f2A))))
print(f"  {'FairTen2 [kN]':<20} rms = {np.sqrt(np.mean(dT2**2)):.4e}"
      f"   (signal std {np.std(np.interp(mg, mtA, f2A)):.3e})")

# OpenFAST-side channels (drivetrain / servo / structure from derisk.out)
otA, ofA = openfast_out(f"{B}/runA/openfast/derisk.out")
otB, ofB = openfast_out(f"{B}/runB/openfast/derisk.out")
og = tg[(tg >= max(otA[0], otB[0])) & (tg <= min(otA[-1], otB[-1]))]
ofUnits = ["rpm", "deg", "kN m"]
for name, u, ya, yb in zip(OF_CHANS, ofUnits, ofA, ofB):
    d = np.interp(og, otB, yb) - np.interp(og, otA, ya)
    VER.append((name, np.sqrt(np.mean(d**2)), np.std(np.interp(og, otA, ya))))
    print(f"  {name + ' [' + u + ']':<20} rms = {np.sqrt(np.mean(d**2)):.4e}"
          f"   (signal std {np.std(np.interp(og, otA, ya)):.3e})")

fig, axs = plt.subplots(5, 1, figsize=(9, 12), sharex=True)
panels = [("surge [m]", tA, A[:, 1], tB, Bt[:, 1]),
          (r"TwrBs $M_{y,\mathrm{raw}}$ [N m]", tA, A[:, 4], tB, Bt[:, 4]),
          ("FairTen2 [kN]", mtA, f2A, mtB, f2B),
          ("RotSpeed [rpm]", otA, ofA[0], otB, ofB[0]),
          ("TwrBsMyt [kN m]", otA, ofA[2], otB, ofB[2])]
for ax, (yl, ta, ya, tb, yb) in zip(axs, panels):
    ax.plot(ta, ya, "-",  color="#0072B2", lw=1.2, label="reference (A)")
    ax.plot(tb, yb, "--", color="#D55E00", lw=1.2, label="stop/restart (B)")
    ax.axvline(SPLICE, color="0.4", lw=0.8, ls=":")
    ax.set_ylabel(yl)
    ax.grid(alpha=0.3, lw=0.5)
axs[0].legend(frameon=False)
axs[0].set_title(f"restart bench: splice at t = {SPLICE} s (dotted)")
axs[-1].set_xlabel("t [s]")
fig.savefig(f"{B}/bench_overlay.png", dpi=200, bbox_inches="tight")
print(f"\nwrote {B}/bench_overlay.png")

# ---- verdict: restart twin must overlay the reference ------------------------
# tolerance: rms < 1% of the channel's own variability (or tiny absolute
# floor for channels that are legitimately constant, e.g. RotSpeed fixed).
import sys
fails = [(n, r, s_) for n, r, s_ in VER if r > max(1e-2*s_, 1e-6)]
if fails:
    print("\nFAIL — restart twin deviates from reference:")
    for n, r, s_ in fails:
        print(f"  {n}: rms {r:.3e} vs allowed {max(1e-2*s_, 1e-6):.3e}")
    sys.exit(1)
print("\nPASS — stop/restart run overlays the uninterrupted reference "
      f"on all {len(VER)} channels (rms < 1% of signal std).")
