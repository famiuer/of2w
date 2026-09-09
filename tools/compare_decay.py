#!/usr/bin/env python3
"""Three-way comparison: A baseline vs B v3 vs B v6.

3-panel row showing the three quantities that directly compare across all
three runs. Surge is dropped because v3/v6 are P_z-locked (≡ 0) so the
surge panel was uninformative.

Panel (a): de-trended heave (slow drift removed via 1-period boxcar) — the
core dynamics comparison; shows oscillation period, amplitude and decay rate.

Panel (b): tower-base reaction Fz — coupling correctness probe; if ED↔OF²
data exchange is right, all three curves overlap at -5874 kN with the same
oscillation pattern.

Panel (c): mean fairlead mooring tension — verifies that the mooring force
on the body is computed consistently with A's MoorDyn.
"""
import os, sys, numpy as np
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
from scipy.signal import savgol_filter
from scipy.ndimage import uniform_filter1d

matplotlib.rcParams.update({
    "text.usetex": True,
    "font.family": "serif",
    "font.size": 18,
    "axes.titlesize": 16,
    "axes.labelsize": 16,
    "legend.fontsize": 16,
    "xtick.labelsize": 16,
    "ytick.labelsize": 16,
})

PTFMCMZT = -8.6588
DESIGN_HULL_BOTTOM = -20.0
COG_ABOVE_HULL_BOTTOM = PTFMCMZT - DESIGN_HULL_BOTTOM  # +11.3412


def stl_hull_bottom(path):
    zmin = float("inf")
    with open(path) as f:
        for L in f:
            L = L.strip()
            if L.startswith("vertex"):
                z = float(L.split()[3])
                if z < zmin: zmin = z
    return zmin


def water_level_z(setfields_path):
    import re
    with open(setfields_path) as f:
        for L in f:
            m = re.search(r"box\s*\(\s*-?\d+\s+-?\d+\s+-?\d+\s*\)\s*\(\s*-?\d+\s+-?\d+\s+(-?[0-9.]+)\s*\)", L)
            if m: return float(m.group(1))
    raise ValueError(f"no box line in {setfields_path}")


def datum_shift(stl_path, setfields_path):
    hull_bottom    = stl_hull_bottom(stl_path)
    cog_init_z     = hull_bottom + COG_ABOVE_HULL_BOTTOM
    water_z        = water_level_z(setfields_path)
    design_eq_cog  = water_z + PTFMCMZT
    return cog_init_z - design_eq_cog


def load_A(path, openfast_io_dir=None):
    if openfast_io_dir:
        sys.path.insert(0, openfast_io_dir)
    from openfast_io.FAST_output_reader import load_binary_output
    data, info, _ = load_binary_output(path)
    ch = info["attribute_names"]; idx = {n: i for i, n in enumerate(ch)}
    out = {}
    for k in ["Time", "PtfmSurge", "PtfmSway", "PtfmHeave", "PtfmRoll",
              "PtfmPitch", "PtfmYaw", "TwrBsFxt", "TwrBsFyt", "TwrBsFzt"]:
        if k in idx: out[k] = data[:, idx[k]]
    return out


def load_derisk(path):
    with open(path) as f: lines = f.readlines()
    for i, L in enumerate(lines):
        if L.startswith("Time"):
            cols = L.strip().split('\t'); data_start = i + 2; break
    data = np.array([[float(x) for x in L.split()] for L in lines[data_start:] if L.strip()])
    return {c: data[:, j] for j, c in enumerate(cols)}


def load_md(path):
    if not os.path.exists(path): return None
    with open(path) as f: raw = f.readlines()
    rows = []
    for L in raw:
        p = L.split()
        if len(p) >= 7:
            try: rows.append([float(x) for x in p[:7]])
            except: pass
    if not rows: return None
    a = np.array(rows)
    return {"t": a[:, 0], "F1": a[:, 1], "F2": a[:, 2], "F3": a[:, 3]}


def detrend(t, h, period_s=17.4):
    """Subtract a boxcar low-pass (width = 1 natural period) to remove the
    slow drift / CFD-eq offset and leave only the oscillation residual."""
    if len(t) < 10: return h - h.mean()
    dt = np.median(np.diff(t))
    window = max(3, int(period_s / dt))
    if window % 2 == 0: window += 1
    return h - uniform_filter1d(h, size=window, mode='nearest')


def main():
    import argparse
    ap = argparse.ArgumentParser(
        description="Compare an OF² coupled run against a standalone OpenFAST baseline. "
                    "Heave is shown as the de-trended residual on a water-aware datum; "
                    "TwrBs and mooring are direct overlays.")
    ap.add_argument("--of2",      type=str, required=True,
                    help="Path to the OF² case dir (e.g. cases/oc4-decay-v6). Expects "
                         "openfast/derisk.out, Mooring/lines_oc4.out, "
                         "constant/triSurface/float.stl, system/setFieldsDict.")
    ap.add_argument("--baseline", type=str, default=None,
                    help="Path to the standalone OpenFAST .outb output (A baseline). "
                         "If omitted, only OF² traces are plotted.")
    ap.add_argument("--baseline-md", type=str, default=None,
                    help="Optional path to A's .MD.out (MoorDyn output). "
                         "Defaults to <baseline-dir>/minimal.MD.out.")
    ap.add_argument("--legacy-of2", type=str, default=None,
                    help="Optional path to a SECOND OF² case dir for historical "
                         "comparison (e.g. an earlier version like v3).")
    ap.add_argument("--openfast-io", type=str,
                    default=os.environ.get("OPENFAST_IO_DIR", "external/openfast/openfast_io"),
                    help="Path to the openfast_io python package "
                         "(default: external/openfast/openfast_io, "
                         "or set $OPENFAST_IO_DIR).")
    ap.add_argument("--out", type=str, default="compare_decay.png",
                    help="Output figure path (default: compare_decay.png).")
    args = ap.parse_args()

    of2_dir   = os.path.abspath(args.of2)
    of2_out   = os.path.join(of2_dir, "openfast",  "derisk.out")
    of2_md    = os.path.join(of2_dir, "Mooring",   "lines_oc4.out")
    of2_stl   = os.path.join(of2_dir, "constant",  "triSurface", "float.stl")
    of2_sfd   = os.path.join(of2_dir, "system",    "setFieldsDict")

    A = AM = None
    if args.baseline:
        a_outb = os.path.abspath(args.baseline)
        a_md   = args.baseline_md or os.path.join(os.path.dirname(a_outb), "minimal.MD.out")
        print(f"Loading A baseline: {a_outb}")
        A = load_A(a_outb, openfast_io_dir=os.path.abspath(args.openfast_io))
        print(f"  A: N={len(A['Time'])}, t_end={A['Time'][-1]:.2f}s")
        AM = load_md(a_md) if os.path.exists(a_md) else None

    legacy_C = CM = None
    if args.legacy_of2:
        legacy_dir = os.path.abspath(args.legacy_of2)
        print(f"Loading legacy OF² case: {legacy_dir}")
        legacy_C = load_derisk(os.path.join(legacy_dir, "openfast", "derisk.out"))
        CM       = load_md(os.path.join(legacy_dir, "Mooring", "lines_oc4.out"))
        legacy_stl = os.path.join(legacy_dir, "constant", "triSurface", "float.stl")
        legacy_sfd = os.path.join(legacy_dir, "system",   "setFieldsDict")
        shift_C    = datum_shift(legacy_stl, legacy_sfd)
        C_hv       = legacy_C["PtfmHeave"] + shift_C
        print(f"  legacy OF² datum shift = {shift_C:+.4f} m, N={len(legacy_C['Time'])}")

    print(f"Loading OF² case: {of2_dir}")
    E       = load_derisk(of2_out)
    EM      = load_md(of2_md) if os.path.exists(of2_md) else None
    shift_E = datum_shift(of2_stl, of2_sfd)
    E_hv    = E["PtfmHeave"] + shift_E
    print(f"  OF² datum shift = {shift_E:+.4f} m, N={len(E['Time'])}, t_end={E['Time'][-1]:.2f}s")

    # De-trended residuals (slow drift removed)
    A_de = detrend(A["Time"], A["PtfmHeave"]) if A else None
    C_de = detrend(legacy_C["Time"], C_hv)    if legacy_C is not None else None
    E_de = detrend(E["Time"], E_hv)

    # ----- 3-panel row figure -----
    fig, axes = plt.subplots(1, 3, figsize=(17, 5.2))
    fig.suptitle(r"Heave, tower-base reaction, mooring tension",
                 fontsize=20)

    # (a) De-trended heave residual
    ax = axes[0]
    if A is not None:
        ax.plot(A["Time"], A_de, lw=1.4, color="C0", label=r"A: OpenFAST")
    if legacy_C is not None:
        ax.plot(legacy_C["Time"], C_de, lw=1.1, color="C2", label=r"legacy OF\textsuperscript{2}", alpha=0.85)
    ax.plot(E["Time"], E_de, lw=1.5, color="C4", label=r"OF\textsuperscript{2} coupled")
    ax.axhline(0, color="k", lw=0.5, alpha=0.5)
    ax.set_xlabel(r"$t$ (s)"); ax.set_ylabel(r"heave $-$ 1-period boxcar (m)")
    ax.set_title(r"(a) Heave")
    ax.set_xlim(0, max(E["Time"]) + 1); ax.grid(alpha=0.3); ax.legend(loc="upper right", fontsize=14)

    # (b) TwrBsFzt
    ax = axes[1]
    if A is not None:
        m = A["Time"] > 0.1; ax.plot(A["Time"][m], A["TwrBsFzt"][m], lw=1.0, color="C0", label="A: OpenFAST")
    if legacy_C is not None:
        m = legacy_C["Time"] > 0.1; ax.plot(legacy_C["Time"][m], legacy_C["TwrBsFzt"][m], lw=0.9, color="C2", label=r"legacy OF\textsuperscript{2}", alpha=0.85)
    m = E["Time"] > 0.1; ax.plot(E["Time"][m], E["TwrBsFzt"][m], lw=1.3, color="C4", label=r"OF\textsuperscript{2} coupled")
    ax.axhline(-5870, color="k", lw=0.5, alpha=0.5, label=r"static $-m_{TR}g$")
    ax.set_xlabel(r"$t$ (s)"); ax.set_ylabel(r"$F_z^\mathrm{TwrBs}$ (kN)")
    ax.set_title(r"(b) Tower-base reaction $\sim -m_\mathrm{TR}(a_z+g)$")
    ax.set_xlim(0, max(E["Time"]) + 1); ax.grid(alpha=0.3); ax.legend(loc="upper right", fontsize=14)

    # (c) Mooring mean
    ax = axes[2]
    def smooth(y, n):
        n = min(n, len(y) - (1 if len(y) % 2 == 0 else 0))
        if n < 5: return y
        if n % 2 == 0: n -= 1
        return savgol_filter(y, n, 3)
    if AM:
        meanA = (AM["F1"] + AM["F2"] + AM["F3"]) / 3 / 1e3
        ax.plot(AM["t"], meanA, lw=1.2, color="C0", label="A: OpenFAST")
    if CM:
        meanC = (CM["F1"] + CM["F2"] + CM["F3"]) / 3 / 1e3
        ax.plot(CM["t"], smooth(meanC, 401), lw=1.0, color="C2", label=r"legacy OF\textsuperscript{2}", alpha=0.85)
    if EM:
        meanE = (EM["F1"] + EM["F2"] + EM["F3"]) / 3 / 1e3
        ax.plot(EM["t"], smooth(meanE, 401), lw=1.4, color="C4", label=r"OF\textsuperscript{2} coupled")
    ax.axhline(1110, color="k", lw=0.5, alpha=0.5, label=r"OC4 pretension $1.11$ MN")
    ax.set_xlabel(r"$t$ (s)"); ax.set_ylabel(r"Mean fairlead tension (kN)")
    ax.set_title(r"(c) Mooring: mean of 3 fairleads")
    ax.set_xlim(0, max(E["Time"]) + 1); ax.set_ylim(1000, 1200)
    ax.grid(alpha=0.3); ax.legend(loc="lower right", fontsize=14)

    plt.tight_layout(rect=[0, 0, 1, 0.95])
    out = os.path.abspath(args.out)
    os.makedirs(os.path.dirname(out) or ".", exist_ok=True)
    plt.savefig(out, dpi=130); plt.close()
    print(f"saved {out}")

    # Metrics on the detrended residual: amplitude per cycle, period
    print()
    print("De-trended residual stats (oscillation only, drift removed):")
    from scipy.signal import find_peaks
    rows = [("OF²", E["Time"], E_de)]
    if A is not None:   rows.insert(0, ("A", A["Time"], A_de))
    if legacy_C is not None: rows.insert(1 if A is not None else 0, ("legacy", legacy_C["Time"], C_de))
    for label, t, h in rows:
        pks, _ = find_peaks(h, distance=100)
        trs, _ = find_peaks(-h, distance=100)
        if len(pks) >= 2 and len(trs) >= 1:
            n = min(len(pks), len(trs), 5)
            amps = h[pks][:n] - h[trs][:n]
            print(f"  {label:<10} first {n} peak-to-trough amps: {amps}")
    print()
    # Decay rate from successive peak amplitudes
    def decay_rate(t, h):
        pks, _ = find_peaks(h, distance=100)
        if len(pks) < 2: return None
        amps = h[pks]
        # log-decrement: zeta = ln(A_n / A_{n+1}) / (2pi)  (approximate)
        ratios = amps[:-1] / amps[1:]
        ratios = ratios[ratios > 1]  # only decaying ones
        if len(ratios) == 0: return None
        delta = np.log(ratios.mean()) if len(ratios) > 0 else 0
        zeta = delta / (2*np.pi) * 100  # percent
        return zeta
    zA = decay_rate(A["Time"],         A_de) if A         is not None else None
    zC = decay_rate(legacy_C["Time"],  C_de) if legacy_C is not None else None
    zE = decay_rate(E["Time"],         E_de)
    print(f"Damping ratio from log-decrement on de-trended peaks (%):")
    print("Damping ratio from log-decrement on de-trended peaks (%):")
    print(f"  A       = {zA:.3f}" if zA is not None else "  A       = (n/a)")
    print(f"  legacy  = {zC:.3f}" if zC is not None else "  legacy  = (n/a)")
    print(f"  OF²     = {zE:.3f}" if zE is not None else "  OF²     = (n/a)")


if __name__ == "__main__":
    main()
