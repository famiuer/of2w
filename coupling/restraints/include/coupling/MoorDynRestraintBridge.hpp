#pragma once

#include "coupling/ExternalLoadProvider.hpp"

#include <memory>

namespace oc4::coupling {

struct RigidBodyMotionSnapshot {
    double time{0.0};
    double dt{0.0};
    ReferencePoint referencePoint{};
    Matrix3 orientationGlobalFromBody{Matrix3::identity()};
    Vector3 velocityGlobal{};
    Vector3 accelerationGlobal{};
    Vector3 angularVelocityGlobal{};
};

class MoorDynRestraintBridge {
public:
    explicit MoorDynRestraintBridge(std::unique_ptr<ExternalLoadProvider> provider);
    ~MoorDynRestraintBridge();

    Wrench evaluate(const RigidBodyMotionSnapshot& snapshot);
    void close();

    static PlatformState buildPlatformState(
        const RigidBodyMotionSnapshot& snapshot,
        const Vector3& previousAngularVelocityGlobal,
        bool hasPreviousAngularVelocity);

private:
    std::unique_ptr<ExternalLoadProvider> provider_{};
    bool initialized_{false};
    Vector3 previousAngularVelocityGlobal_{};

    void initializeIfNeeded(const PlatformState& platformState);
};

}  // namespace oc4::coupling
