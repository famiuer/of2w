#!/usr/bin/env python3
"""Pitch free-decay check: CFD (OF2W) vs the OpenFAST engineering baseline.

CFD signal   : PtfmPitch from background/openfast/derisk.out (the platform
               motion the CFD imposes on ElastoDyn — i.e. the OpenFOAM
               rigid-body solution).
Baseline     : PtfmPitch from openfast_baseline/minimal.outb — the SAME
               platform simulated standalone by OpenFAST (potential-flow
               HydroDyn + MoorDyn), pitch DOF only, same +5 deg release.
Metrics      : natural period from mean peak spacing, first-peaks amplitude.
Acceptance   : period within 12% of the baseline (CFD resolves viscous
               damping the potential-flow baseline lacks, so amplitudes
               decay faster; the period is the like-for-like metric) and a
               physically decaying signal. Writes pitch_decay_overlay.png.
"""
import os
import struct
import sys
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

HERE = os.path.dirname(os.path.abspath(__file__))
TOL_PERIOD = 0.12


def read_outb(path):
    """FAST binary .outb, FileID 2 (packed int16) or 3 (double)."""
    b = open(path, "rb").read()
    o = 0
    fid, = struct.unpack_from("<h", b, o); o += 2
    if fid == 4:
        chanlen, = struct.unpack_from("<h", b, o); o += 2
    else:
        chanlen = 10
    nc, = struct.unpack_from("<i", b, o); o += 4
    nt, = struct.unpack_from("<i", b, o); o += 4
    t0, dt = struct.unpack_from("<dd", b, o); o += 16
    if fid != 3:
        colscl = np.frombuffer(b, "<f4", nc, o); o += 4*nc
        coloff = np.frombuffer(b, "<f4", nc, o); o += 4*nc
    ndesc, = struct.unpack_from("<i", b, o); o += 4
    o += ndesc
    names = [b[o + i*chanlen:o + (i+1)*chanlen].decode().strip()
             for i in range(nc + 1)]
    o += chanlen*(nc + 1)          # names (incl. Time)
    o += chanlen*(nc + 1)          # units
    if fid == 3:
        data = np.frombuffer(b, "<f8", nt*nc, o).reshape(nt, nc)
    else:
        raw = np.frombuffer(b, "<i2", nt*nc, o).reshape(nt, nc)
        data = (raw - coloff)/colscl
    t = t0 + dt*np.arange(nt)
    return t, names[1:], data


def read_out(path):
    """FAST text .out: channel-name row then units row then data."""
    lines = open(path).readlines()
    for i, l in enumerate(lines):
        if l.split() and l.split()[0] == "Time":
            names = l.split()
            d = np.array([[float(x) for x in r.split()]
                          for r in lines[i+2:] if r.strip()])
            return d[:, 0], names[1:], d[:, 1:]
    sys.exit(f"FAIL — no channel header in {path}")


def peaks(t, y):
    i = np.where((y[1:-1] >= y[:-2]) & (y[1:-1] > y[2:]))[0] + 1
    i = i[y[i] > 0.05*abs(y).max()]         # ignore noise-level maxima
    return t[i], y[i]


# ---- load ----
cfd = f"{HERE}/background/openfast/derisk.out"
if not os.path.exists(cfd):
    sys.exit("FAIL — no background/openfast/derisk.out (run the case first)")
tc, nc_, dc = read_out(cfd)
tb, nb_, db = read_outb(f"{HERE}/openfast_baseline/minimal.outb")
try:
    yc = dc[:, nc_.index("PtfmPitch")]
    yb = db[:, nb_.index("PtfmPitch")]
except ValueError:
    sys.exit("FAIL — PtfmPitch channel missing")

tpc, apc = peaks(tc, yc)
tpb, apb = peaks(tb, yb)
if len(tpc) < 2:
    sys.exit(f"FAIL — fewer than 2 pitch peaks in the CFD record "
             f"(t_end={tc[-1]:.0f} s); run further")
n = min(4, len(tpc), len(tpb))          # same number of spacings on both
Tc = np.mean(np.diff(tpc[:n]))
Tb = np.mean(np.diff(tpb[:n]))
print(f"CFD:      {len(tpc)} peaks, period {Tc:.2f} s, "
      f"first peaks {apc[:3].round(2)} deg")
print(f"baseline: {len(tpb)} peaks, period {Tb:.2f} s, "
      f"first peaks {apb[:3].round(2)} deg")
print(f"period difference: {100*(Tc/Tb - 1):+.1f}%  (tolerance ±{100*TOL_PERIOD:.0f}%)")

fig, ax = plt.subplots(figsize=(9, 3.2))
ax.plot(tb, yb, "--", color="#c0392b", lw=1.1,
        label="OpenFAST baseline (potential flow)")
ax.plot(tc, yc, "-", color="#1f6fb4", lw=1.3, label="OF2W (CFD)")
ax.set_xlabel("t [s]"); ax.set_ylabel("platform pitch [deg]")
ax.legend(frameon=False); ax.grid(alpha=0.3, lw=0.5)
fig.savefig(f"{HERE}/pitch_decay_overlay.png", dpi=200, bbox_inches="tight")
print(f"wrote {HERE}/pitch_decay_overlay.png")

fails = []
if abs(Tc/Tb - 1) > TOL_PERIOD:
    fails.append(f"period off {100*(Tc/Tb-1):+.1f}%")
if not (apc[min(2, len(apc)-1)] < apc[0]):
    fails.append("pitch amplitude not decaying")
if not np.isfinite(yc).all():
    fails.append("non-finite values in CFD record")
if fails:
    print("FAIL — " + "; ".join(fails)); sys.exit(1)
print("PASS — CFD pitch decay consistent with the OpenFAST baseline "
      f"(period {Tc:.2f} s vs {Tb:.2f} s, {100*(Tc/Tb-1):+.1f}%).")
