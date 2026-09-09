#pragma once

#include "coupling/Types.hpp"

#include <functional>
#include <string>
#include <vector>

namespace oc4::coupling {

using ExternalWaveKinematicsProvider = std::function<void(
    double time,
    const std::vector<Vector3>& samplePointsGlobal,
    std::vector<Vector3>& velocityGlobal,
    std::vector<Vector3>& accelerationGlobal)>;

struct MoorDynConfig {
    std::string inputFilePath{};
    std::string libraryPath{};
    std::string logFilePath{};
    ReferencePoint platformReferencePoint{};
    Vector3 openFoamToMoorDynOffsetGlobal{};
    ExternalWaveKinematicsProvider externalWaveKinematicsProvider{};
    bool skipInitialConditionSolve{false};
    int verbosity{0};
};

class MoorDynAdapter {
public:
    virtual ~MoorDynAdapter() = default;

    virtual void initialize(const MoorDynConfig& config, const PlatformState& initialState) = 0;
    virtual MoorDynResult step(const PlatformState& platformState, double dt) = 0;
    virtual void close() = 0;
};

}  // namespace oc4::coupling
