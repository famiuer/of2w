#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Custom OF2/WES-style bridge symbols expected by the forced OpenFAST runtime.
 *
 * These symbols are intended to live in the same shared library as the stock
 * FAST_* exports. The runtime dlopens one handle and resolves both symbol sets
 * from that handle.
 */

/*
 * Store imposed platform motion for turbine iTurb.
 *
 * displacement = [x, y, z, roll, pitch, yaw]
 * velocity     = [vx, vy, vz, omega_x, omega_y, omega_z]
 * acceleration = [ax, ay, az, alpha_x, alpha_y, alpha_z]
 */
void FAST_OC4_Platform_SetMotion(
    int* iTurb,
    const double* displacement,
    const double* velocity,
    const double* acceleration,
    int* ErrStat,
    char* ErrMsg);

/*
 * Return one structural/interface reaction wrench.
 *
 * applicationPoint = [x, y, z] in the global coupling frame
 * force            = [Fx, Fy, Fz] in the global coupling frame
 * moment           = [Mx, My, Mz] about applicationPoint in the global coupling frame
 */
void FAST_OC4_Platform_GetReactionLoad(
    int* iTurb,
    double* applicationPoint,
    double* force,
    double* moment,
    int* ErrStat,
    char* ErrMsg);

/*
 * Return the minimum diagnostic set used by the OpenFOAM/OpenFAST wrapper.
 *
 * bladePitchRad must point to storage for 3 doubles.
 * towerTopDisplacementGlobal must point to storage for 3 doubles.
 */
void FAST_OC4_Platform_GetDiagnostics(
    int* iTurb,
    double* rotorSpeedRadPerSec,
    double* generatorPowerW,
    double* bladePitchRad,
    double* towerTopDisplacementGlobal,
    int* controllerOk,
    int* ErrStat,
    char* ErrMsg);

#ifdef __cplusplus
}
#endif
