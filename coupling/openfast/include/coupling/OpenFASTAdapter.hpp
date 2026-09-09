#pragma once

#include "coupling/Types.hpp"

#include <array>
#include <string>

namespace oc4::coupling {

struct OpenFASTConfig {
    std::string casePath{};
    std::string libraryPath{};
    ReferencePoint platformReferencePoint{};
    ReferencePoint turbineInterfacePoint{};
    Vector3 openFoamToOpenFastOffsetGlobal{};
};

class OpenFASTAdapter {
public:
    virtual ~OpenFASTAdapter() = default;

    virtual void initialize(const OpenFASTConfig& config, const PlatformState& initialState) = 0;
    virtual OpenFASTResult step(const PlatformState& platformState, double dt) = 0;
    virtual void close() = 0;
};

}  // namespace oc4::coupling
