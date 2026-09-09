#include "coupling/ForcedOpenFASTCApi.h"

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <string>

namespace {

void require(bool condition, const std::string& message)
{
    if (!condition) {
        std::cerr << "Test failure: " << message << '\n';
        std::exit(1);
    }
}

bool nearlyEqual(double a, double b, double tolerance = 1e-12)
{
    return std::fabs(a - b) <= tolerance;
}

void requireVector3(const double* actual, double x, double y, double z, const std::string& label)
{
    require(nearlyEqual(actual[0], x), label + " x mismatch");
    require(nearlyEqual(actual[1], y), label + " y mismatch");
    require(nearlyEqual(actual[2], z), label + " z mismatch");
}

}  // namespace

int main(int argc, char** argv)
{
    require(argc == 2, "expected smoke-case path argument");

    const char* version = ForcedOpenFAST_GetVersion();
    require(version != nullptr, "version string should not be null");

    ForcedOpenFASTHandle handle = ForcedOpenFAST_Create(argv[1]);
    require(handle != nullptr, "wrapper handle should be created from example case");

    const double displacement[6] = {1.0, 2.0, -12.0, 0.1, 0.2, 0.3};
    const double velocity[6] = {4.0, 5.0, 6.0, 0.11, 0.22, 0.33};
    const double acceleration[6] = {0.7, 0.8, 0.9, 0.44, 0.55, 0.66};

    require(ForcedOpenFAST_Initialize(handle, displacement, velocity, acceleration) == FORCED_OPENFAST_SUCCESS,
            "initialize should succeed");

    double time = 5.0;
    double dt = 0.25;
    require(ForcedOpenFAST_Step(handle, displacement, velocity, acceleration, &time, &dt) == FORCED_OPENFAST_SUCCESS,
            "step should succeed");

    double applicationPoint[3] = {};
    double force[3] = {};
    double moment[3] = {};
    require(ForcedOpenFAST_GetTowerBaseLoad(handle, applicationPoint, force, moment) == FORCED_OPENFAST_SUCCESS,
            "tower-base load query should succeed");

    requireVector3(applicationPoint, 0.0, 0.0, -5.0, "application point");
    requireVector3(force, -35000.0, -62500.0, 171000.0, "force");
    requireVector3(moment, -555000.0, -1110000.0, -832500.0, "moment");

    double rotorSpeed = 0.0;
    double generatorPower = 0.0;
    double bladePitch[3] = {};
    double towerTopDisplacement[3] = {};
    int controllerOk = 0;

    require(ForcedOpenFAST_GetRotorSpeed(handle, &rotorSpeed) == FORCED_OPENFAST_SUCCESS,
            "rotor-speed query should succeed");
    require(ForcedOpenFAST_GetGeneratorPower(handle, &generatorPower) == FORCED_OPENFAST_SUCCESS,
            "generator-power query should succeed");
    require(ForcedOpenFAST_GetBladePitch(handle, bladePitch) == FORCED_OPENFAST_SUCCESS,
            "blade-pitch query should succeed");
    require(ForcedOpenFAST_GetTowerTopDisplacement(handle, towerTopDisplacement) == FORCED_OPENFAST_SUCCESS,
            "tower-top displacement query should succeed");
    require(ForcedOpenFAST_GetControllerStatus(handle, &controllerOk) == FORCED_OPENFAST_SUCCESS,
            "controller-status query should succeed");

    require(nearlyEqual(rotorSpeed, 1.04), "rotor speed mismatch");
    require(nearlyEqual(generatorPower, 12000.0), "generator power mismatch");
    requireVector3(bladePitch, 0.2, 0.2, 0.2, "blade pitch");
    requireVector3(towerTopDisplacement, 0.1, 0.2, -1.2, "tower-top displacement");
    require(controllerOk == 1, "controller should report healthy");

    require(ForcedOpenFAST_Close(handle) == FORCED_OPENFAST_SUCCESS, "close should succeed");

    std::cout << "All ForcedOpenFAST runtime tests passed.\n";
    return 0;
}
