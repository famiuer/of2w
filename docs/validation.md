# Validation results

This page summarises the current state of validation of the OF² coupling
against a standalone OpenFAST + HydroDyn + MoorDyn baseline. All numbers
come from the `cases/oc4-decay-v6` reference case versus the OC4-DeepCwind
free-decay benchmark.

## Test configuration

| | Standalone baseline (A) | OF² coupled (v6) |
|---|---|---|
| Hydrodynamics | HydroDyn (WAMIT linear potential flow) | CFD: interFoam VOF, RANS k-ωSST |
| Mooring | MoorDyn (inside OpenFAST) | MoorDyn (inside OpenFOAM via foamMooring) |
| Tower / nacelle / rotor | ED, rigid (TwFADOF, etc. all False, rotor stopped) | same |
| Platform DOFs | all 6 True, IC PtfmHeave=+1 m | all 6 True (ED), constrained to Pz by OpenFOAM `joint Pz` |
| Mesh | n/a | ≈1.0×10⁶ cells, 64 ranks |
| Run length | 120 s | 120 s |

The "+1 m IC" on the OF² side is imposed by the **setFields water-level
trick**: water is set at z=−1 in `setFieldsDict`, so the body sits 1 m
above its water-relative equilibrium. No re-meshing required.

## Headline numbers

| Property | A | v6 | Δ | Verdict |
|---|---|---|---|---|
| Natural period — FFT peak of de-trended heave | 0.06087 Hz | 0.06090 Hz | +0.05 % | ✓ dynamics match |
| Natural period — peak-to-peak | 17.36 s | 17.6 s | +1.4 % | ✓ |
| Tower-base reaction mean (t > 10 s) | -5874.8 kN | -5874.4 kN | <0.01 % | ✓ wrench transmission correct |
| Mooring pretension per chain | 1105 kN | 1130 kN | +2 % | ≈ ok (within MoorDyn IC convergence tolerance) |
| Heave first trough (de-trended) | -0.87 m | -1.49 m | +0.6 m deeper | ✗ amplitude larger in CFD |
| Damping ratio ζ (log-decrement on de-trended peaks) | 2.58 % | 3.94–9.82 % | higher | ✗ overestimated, partly artifact of incomplete drift removal |
| CFD eq vs design eq (NLS fit) | -0.01 m | -0.99 m | -1 m | ✗ static balance offset |

## What works

- **Coupling wrench transmission** is correct. TwrBsFzt matches the
  standalone OpenFAST baseline to better than 1 kN out of 5874 kN.
- **Natural period** matches WAMIT to 0.05 % on the FFT and to 1.4 % on
  peak-to-peak. This proves the hydrostatic + mooring stiffness in CFD is
  effectively the same as in WAMIT — i.e., the *dynamics* of the coupled
  system are reproduced.
- **Mooring tensions** are within 2 % of OC4 spec pretension at IC.

## What doesn't (yet)

### Static equilibrium offset (≈ -1 m)

The body settles ~1 m below ED's design eq in the OF² simulation. The
leading hypothesis is the fairlead-world-z mismatch (see
`coordinate_conventions.md` for the full mapping):

- Mooring/lines_oc4.txt has fairleads at body-frame z = +1
- With `transform.r.z = -12.722`, fairlead world z = -11.722
- OC4 spec / standalone baseline puts fairleads at world z = -14

The 2.3 m difference in fairlead world z changes the catenary geometry,
which changes the vertical mooring force on the body, which moves the
static equilibrium.

**Fix**: reconcile the body-frame origin in `Mooring/lines_oc4.txt` with
the body's actual world z. Either change the fairlead body-frame z
values, or change the OpenFOAM body's `transform.r.z` so the existing
fairlead body-frame coordinates land at world z = -14.

### Amplitude larger than WAMIT (≈ +33 %)

The first-cycle peak-to-trough amplitude is 1.92 m in v6 vs 1.44 m in
A. Larger amplitude in CFD at a 1 m IC (5 % of draft) is the expected
sign of free-surface nonlinearity that WAMIT linearises away. We don't
have a sharp fix for this — it's a model difference, not a coupling bug.

### Damping ratio higher in CFD

Multiple contributors:

1. Real CFD viscous damping (boundary-layer dissipation on the heave
   plates) that WAMIT's linear potential flow misses entirely.
2. Numerical dissipation in the VOF discretisation and the second-order
   ddt scheme.
3. The log-decrement estimator is contaminated by the slow drift; the
   true damping is closer to A's value than the headline number
   suggests. The 1-period boxcar low-pass leaves some drift signature
   in the de-trended residual, which inflates the apparent decay rate
   when fitted as if it were a pure damped sinusoid.

## Comparison figure

`tools/compare_decay.py` produces a 3-panel figure:

- (a) de-trended heave residual: same period, similar shape, larger
      amplitude in v6 corresponds to v6's larger geometric IC
- (b) TwrBsFzt: three traces overlap at -5874 kN (coupling correct)
- (c) Mean mooring tension: within 30 kN over the run

Output: `compare_v6_detrend.png` in the case directory after running
`compare_decay.py`.

## Earlier experiments (v1–v5)

For context, the family of v1–v5 OF² cases tried different STL z-shifts
and MULES VOF settings. None imposed a true +1 m heave IC — they all
sat at their geometric equilibrium with a +1 m cosmetic shift applied
post-hoc in the plotter. v6 is the first case where the IC is physically
real, via setFields. The MULES iteration count was shown not to matter
for the static-balance offset (v3 → v5 differs by 20 mm RMSE in heave
over 120 s).

These are kept private in the development workspace; only v6 ships
with this repo.
