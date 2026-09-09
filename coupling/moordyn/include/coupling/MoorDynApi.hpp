#pragma once

#include "coupling/Types.hpp"

#include <functional>
#include <memory>
#include <string>

namespace oc4::coupling {

using MoorDynHandle = void*;

struct MoorDynApi {
    std::function<MoorDynHandle(const char* inputFilePath)> create{};
    std::function<int(MoorDynHandle system, unsigned int* n)> getNCoupledDof{};
    std::function<int(MoorDynHandle system, int verbosity)> setVerbosity{};
    std::function<int(MoorDynHandle system, const char* logFilePath)> setLogFile{};
    std::function<int(MoorDynHandle system, const double* x, const double* xd)> init{};
    std::function<int(MoorDynHandle system, const double* x, const double* xd)> initNoIc{};
    std::function<int(MoorDynHandle system, const double* x, const double* xd, double* f, double* t, double* dt)> step{};
    std::function<int(MoorDynHandle system)> close{};
    std::function<int(MoorDynHandle system, unsigned int* n)> externalWaveKinInit{};
    std::function<int(MoorDynHandle system, unsigned int* n)> externalWaveKinGetN{};
    std::function<int(MoorDynHandle system, double* coordinates)> externalWaveKinGetCoordinates{};
    std::function<int(MoorDynHandle system, const double* velocity, const double* acceleration, double time)> externalWaveKinSet{};
    std::shared_ptr<void> runtimeHandle{};
};

MoorDynApi makeRuntimeLoadedMoorDynApi(const std::string& libraryPath);

}  // namespace oc4::coupling
