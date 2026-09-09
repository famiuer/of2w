#pragma once

#ifdef __cplusplus
extern "C" {
#endif

typedef void* ForcedOpenFASTHandle;

enum ForcedOpenFASTStatus {
    FORCED_OPENFAST_SUCCESS = 0,
    FORCED_OPENFAST_INFO = 1,
    FORCED_OPENFAST_WARNING = 2,
    FORCED_OPENFAST_RECOVERABLE_ERROR = 3,
    FORCED_OPENFAST_FATAL_ERROR = 4,
    FORCED_OPENFAST_INVALID_ARGUMENT = 5,
    FORCED_OPENFAST_NOT_IMPLEMENTED = 6
};

/*
 * Create a wrapper instance from a case directory path.
 *
 * The case directory is expected to contain a wrapper manifest such as
 * forced_openfast_case.toml plus the referenced OpenFAST input files.
 * Returns null on failure.
 */
ForcedOpenFASTHandle ForcedOpenFAST_Create(const char* casePath);

/*
 * Initialize the turbine-side model from imposed platform kinematics.
 *
 * Arrays use the order:
 * displacement = [x, y, z, roll, pitch, yaw]
 * velocity     = [vx, vy, vz, omega_x, omega_y, omega_z]
 * acceleration = [ax, ay, az, alpha_x, alpha_y, alpha_z]
 *
 * Units:
 * - position in meters
 * - angles in radians
 * - translational velocity in m/s
 * - angular velocity in rad/s
 * - translational acceleration in m/s^2
 * - angular acceleration in rad/s^2
 */
int ForcedOpenFAST_Initialize(
    ForcedOpenFASTHandle handle,
    const double* displacement,
    const double* velocity,
    const double* acceleration);

/*
 * Advance one loose-coupling time step under imposed platform motion.
 *
 * time points to the OpenFOAM/master time at the beginning of the step.
 * dt points to the requested coupling time step.
 */
int ForcedOpenFAST_Step(
    ForcedOpenFASTHandle handle,
    const double* displacement,
    const double* velocity,
    const double* acceleration,
    double* time,
    double* dt);

/*
 * Return one resultant turbine reaction wrench at the chosen interface point.
 *
 * applicationPoint = [x, y, z] in global coordinates
 * force            = [Fx, Fy, Fz] in global coordinates
 * moment           = [Mx, My, Mz] about applicationPoint in global coordinates
 */
int ForcedOpenFAST_GetTowerBaseLoad(
    ForcedOpenFASTHandle handle,
    double* applicationPoint,
    double* force,
    double* moment);

int ForcedOpenFAST_GetRotorSpeed(
    ForcedOpenFASTHandle handle,
    double* rotorSpeedRadPerSec);

int ForcedOpenFAST_GetGeneratorPower(
    ForcedOpenFASTHandle handle,
    double* generatorPowerW);

/*
 * bladePitchRad must point to storage for 3 doubles.
 */
int ForcedOpenFAST_GetBladePitch(
    ForcedOpenFASTHandle handle,
    double* bladePitchRad);

/*
 * towerTopDisplacementGlobal must point to storage for 3 doubles.
 */
int ForcedOpenFAST_GetTowerTopDisplacement(
    ForcedOpenFASTHandle handle,
    double* towerTopDisplacementGlobal);

int ForcedOpenFAST_GetControllerStatus(
    ForcedOpenFASTHandle handle,
    int* controllerOk);

/*
 * Returns a null-terminated human-readable error string owned by the wrapper.
 * The pointer becomes invalid when the handle is closed.
 */
const char* ForcedOpenFAST_GetLastError(ForcedOpenFASTHandle handle);

/*
 * Returns a null-terminated version string owned by the wrapper.
 */
const char* ForcedOpenFAST_GetVersion(void);

int ForcedOpenFAST_Close(ForcedOpenFASTHandle handle);

#ifdef __cplusplus
}
#endif
