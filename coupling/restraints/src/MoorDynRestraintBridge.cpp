#include "coupling/MoorDynRestraintBridge.hpp"

#include "coupling/Math.hpp"

#include <stdexcept>
#include <utility>

namespace oc4::coupling {

namespace {

void require(bool condition, const std::string& message)
{
    if (!condition) {
        throw std::runtime_error(message);
    }
}

}  // namespace

MoorDynRestraintBridge::MoorDynRestraintBridge(std::unique_ptr<ExternalLoadProvider> provider)
    : provider_(std::move(provider))
{
    require(static_cast<bool>(provider_), "MoorDynRestraintBridge requires a non-null load provider");
}

MoorDynRestraintBridge::~MoorDynRestraintBridge()
{
    close();
}

Wrench MoorDynRestraintBridge::evaluate(const RigidBodyMotionSnapshot& snapshot)
{
    const PlatformState platformState =
        buildPlatformState(snapshot, previousAngularVelocityGlobal_, initialized_);

    initializeIfNeeded(platformState);
    previousAngularVelocityGlobal_ = snapshot.angularVelocityGlobal;
    return provider_->evaluate(platformState, snapshot.dt);
}

void MoorDynRestraintBridge::close()
{
    if (provider_) {
        provider_->close();
    }
    initialized_ = false;
    previousAngularVelocityGlobal_ = {};
}

PlatformState MoorDynRestraintBridge::buildPlatformState(
    const RigidBodyMotionSnapshot& snapshot,
    const Vector3& previousAngularVelocityGlobal,
    bool hasPreviousAngularVelocity)
{
    require(snapshot.referencePoint.frame == FrameId::global,
            "RigidBodyMotionSnapshot reference point must be in the global frame");
    require(isProperRotationMatrix(snapshot.orientationGlobalFromBody),
            "RigidBodyMotionSnapshot orientationGlobalFromBody is not a proper rotation matrix");
    require(snapshot.dt >= 0.0, "RigidBodyMotionSnapshot dt must be non-negative");

    PlatformState state{};
    state.time = snapshot.time;
    state.referencePoint = snapshot.referencePoint;
    state.orientationGlobalFromBody = snapshot.orientationGlobalFromBody;
    state.positionGlobal = snapshot.referencePoint.position;
    state.velocityGlobal = snapshot.velocityGlobal;
    state.accelerationGlobal = snapshot.accelerationGlobal;
    state.angularVelocityGlobal = snapshot.angularVelocityGlobal;
    state.eulerAnglesRad = matrixToEulerXyz(snapshot.orientationGlobalFromBody);
    state.eulerAngleRatesRadPerSec =
        globalAngularVelocityToEulerXyzRates(state.eulerAnglesRad, snapshot.angularVelocityGlobal);

    if (hasPreviousAngularVelocity && snapshot.dt > 0.0) {
        state.angularAccelerationGlobal =
            (snapshot.angularVelocityGlobal - previousAngularVelocityGlobal) * (1.0 / snapshot.dt);
    }

    return state;
}

void MoorDynRestraintBridge::initializeIfNeeded(const PlatformState& platformState)
{
    if (!initialized_) {
        provider_->initialize(platformState);
        initialized_ = true;
    }
}

}  // namespace oc4::coupling
