# Unified OF² coupling — design & implementation plan

Status: drafting. Supersedes the scattered frame handling in `of2Restraint.C`.
Companion analysis: the four-frame review and the two diagnostic tests
(frame-mixing **ruled out** at 0.0002° pitch; strong coupling **not converging**,
moment residual unmonitored). This plan fixes the *frame algebra* cleanly and
separates it from the *FSI control* so the convergence work can land afterwards.

## 1. The four reference frames

| frame | meaning | relation to W |
|---|---|---|
| **W** | OpenFOAM global inertial (CFD, gravity, mesh) | identity |
| **B** | body frame (rides the platform) | `T_WB(t)` from the rigid-body solver |
| **E** | ElastoDyn DOF axis convention | constant axis map `A` |
| **T** | ED tower-base load frame (tilts with platform) | derived: `R_WT = R_WB` (the `A` cancels) |

`A` (W→E, `v_E = A·v_W`), from `frameOfToEdRotation`:
```
A = [[1,0,0],
     [0,0,1],
     [0,-1,0]]          # ED_x=OF_x, ED_y=OF_z, ED_z=-OF_y
```

## 2. Single source of truth

```
R_WB = R_IC · R_X0          # absolute body orientation (IC ∘ motion). ONE object.
```
`R_IC = Rz(yawIC)·Ry(pitchIC)·Rx(rollIC)` built **once**; `R_X0 = model_.X0.E().T()`
(motion only). The IC is composed here, never re-added as a scalar downstream.

## 3. Conversion operators (the entire frame contract)

Motion W → E (sent to ElastoDyn):
```
M     = A · R_WB · Aᵀ                         # body orientation in E basis
(R,P,Y) = eulerExtractED(M)                   # gated on |cosP|
disp[0:3] = A·dPos_W + (surgeIC, swayIC, heaveIC)
disp[3:6] = (R, P, Y)
vel[0:3]  = A·vlin_W
vel[3:6]  = dofRates(M, A·ω_W)                # axis map + Euler-rate Jacobian J⁻¹
acc[0:3]  = A·alin_W
acc[3:6]  = dofAccels(M, A·ω_W, A·α_W)        # full d/dt of J⁻¹
```
Load T → W (received from ElastoDyn):
```
F_W   = R_WB·F_T
M_W   = R_WB·M_T
M_O_W = M_W + (p_twb_W − p_O) × F_W           # transfer to RBD reference (origin)
```

### Euler-rate Jacobian, derived by differentiating the extraction (no order guessing)
With `Ṁ = skew(ω_E)·M`, `M̈ = skew(α_E)·M + skew(ω_E)·Ṁ`, and `cosP = √(1−m10²)`:
```
Ṗ = −Ṁ10 / cosP
Ṙ = (m12·Ṁ11 − m11·Ṁ12) / (m11²+m12²)
Ẏ = (m20·Ṁ00 − m00·Ṁ20) / (m00²+m20²)
P̈ = −M̈10/cosP − Ṁ10·sinP·Ṗ/cosP²
R̈ = (ṅumR·denR − numR·ḋenR)/denR² ,  numR=m12·Ṁ11−m11·Ṁ12 , denR=m11²+m12²
Ÿ = (ṅumY·denY − numY·ḋenY)/denY² ,  numY=m20·Ṁ00−m00·Ṁ20 , denY=m00²+m20²
```
(the `Ṁ11·Ṁ12` cross terms cancel in `ṅumR`, leaving `numR̈ = m12·M̈11 − m11·M̈12`.)

## 4. Singular-matrix gating ("just in case")

A single guard covers extraction, rates and accels — they all share `cosP`:
```
if |cosP| < EPS_COSP (=1e-6)      → gimbal lock  → return false
if denR < EPS2 or denY < EPS2     → degenerate   → return false
```
`motionToModule(...)` returns `bool`; `of2Restraint` aborts with a clear message
(as today's gimbal check does). For OC4 `cosP ≈ cos 5° ≈ 0.996` — the gate never
fires in normal operation; it exists to fail loudly instead of emitting NaNs.

## 5. Files

| file | role | testable |
|---|---|---|
| `of2/frameAlgebra.hpp` | dependency-free core math (Vec3/Mat3, A, euler, J⁻¹, gating) | yes — `g++` standalone |
| `of2/test/frameAlgebra_test.cpp` | unit test: consistency, velocity, accel vs finite-diff, gating | runs locally |
| `of2/CouplingFrame.H` | Foam-typed wrapper over the same formulas (used by `of2Restraint`) | on ham8 build |
| `of2Restraint.C` | shrinks to: read RBD state → `CouplingFrame::motionToModule` → driver → `CouplingFrame::loadToWorld` → `fx` | integration |

`FsiController` (Aitken/IQN, 6-DOF moment residual, tolerance) is the **separate
follow-up** — out of scope for this frame refactor.

## 6. Acceptance checks (the test must pass)

1. pure pitch: imposed-attitude == load-attitude to ~1e-15 (today: ok).
2. pitch+roll+yaw: imposed-attitude == load-attitude to ~1e-15 (today: 0.027° off).
3. `dofRates` matches central finite-diff of `eulerExtractED` to <1e-9 (today: raw ω, 0.7% off).
4. `dofAccels` matches finite-diff of `dofRates` to <1e-7.
5. gimbal: at pitch→90° the gate returns false (no NaN).
