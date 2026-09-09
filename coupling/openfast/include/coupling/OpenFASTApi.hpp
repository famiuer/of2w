#pragma once

#include "coupling/Types.hpp"

#include <functional>
#include <memory>
#include <string>

namespace oc4::coupling {

using OpenFASTHandle = void*;

struct OpenFASTApi {
    std::function<OpenFASTHandle(const char* casePath)> create{};
    std::function<int(OpenFASTHandle handle, const double* displacement, const double* velocity, const double* acceleration)> initialize{};
    std::function<int(OpenFASTHandle handle, const double* displacement, const double* velocity, const double* acceleration, double* time, double* dt)> step{};
    std::function<int(OpenFASTHandle handle, double* applicationPoint, double* force, double* moment)> getTowerBaseLoad{};
    std::function<int(OpenFASTHandle handle, double* rotorSpeedRadPerSec)> getRotorSpeed{};
    std::function<int(OpenFASTHandle handle, double* generatorPowerW)> getGeneratorPower{};
    std::function<int(OpenFASTHandle handle, double* bladePitchRad)> getBladePitch{};
    std::function<int(OpenFASTHandle handle, double* towerTopDisplacementGlobal)> getTowerTopDisplacement{};
    std::function<int(OpenFASTHandle handle, int* controllerOk)> getControllerStatus{};
    std::function<int(OpenFASTHandle handle)> close{};
    std::shared_ptr<void> runtimeHandle{};
};

OpenFASTApi makeRuntimeLoadedOpenFASTApi(const std::string& libraryPath);

}  // namespace oc4::coupling
