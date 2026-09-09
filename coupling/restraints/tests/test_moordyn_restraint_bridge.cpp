#include "coupling/MoorDynRestraintBridge.hpp"
#include "coupling/Math.hpp"

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <stdexcept>

namespace {

using oc4::coupling::ExternalLoadProvider;
using oc4::coupling::FrameId;
using oc4::coupling::MoorDynRestraintBridge;
using oc4::coupling::PlatformState;
using oc4::coupling::ReferencePoint;
using oc4::coupling::RigidBodyMotionSnapshot;
using oc4::coupling::Vector3;
using oc4::coupling::Wrench;

void require(bool condition, const char* message)
{
    if (!condition) {
        std::cerr << "Test failure: " << message << '\n';
        std::exit(1);
    }
}

struct FakeLoadProvider final : public ExternalLoadProvider {
    PlatformState initializedState{};
    PlatformState evaluatedState{};
    bool initializeCalled{false};
    bool closeCalled{false};
    int evaluateCount{0};
    Wrench nextLoad{};

    void initialize(const PlatformState& initialState) override
    {
        initializedState = initialState;
        initializeCalled = true;
    }

    Wrench evaluate(const PlatformState& platformState, double) override
    {
        evaluatedState = platformState;
        evaluateCount += 1;
        return nextLoad;
    }

    void close() override
    {
        closeCalled = true;
    }
};

RigidBodyMotionSnapshot makeSnapshot(double time, double dt, const Vector3& angularVelocityGlobal)
{
    RigidBodyMotionSnapshot snapshot{};
    snapshot.time = time;
    snapshot.dt = dt;
    snapshot.referencePoint = {{1.0, 2.0, 3.0}, FrameId::global, "platformRef"};
    snapshot.orientationGlobalFromBody = oc4::coupling::eulerXyzToMatrix({0.1, -0.2, 0.3});
    snapshot.velocityGlobal = {4.0, 5.0, 6.0};
    snapshot.accelerationGlobal = {0.7, 0.8, 0.9};
    snapshot.angularVelocityGlobal = angularVelocityGlobal;
    return snapshot;
}

void testPlatformStateExtraction()
{
    const RigidBodyMotionSnapshot snapshot = makeSnapshot(1.25, 0.5, {0.2, -0.1, 0.3});
    const PlatformState state =
        MoorDynRestraintBridge::buildPlatformState(snapshot, Vector3{}, false);

    require(oc4::coupling::nearlyEqual(state.positionGlobal, Vector3{1.0, 2.0, 3.0}, 1e-12),
            "positionGlobal should follow the reference point");
    require(oc4::coupling::nearlyEqual(state.accelerationGlobal, Vector3{0.7, 0.8, 0.9}, 1e-12),
            "accelerationGlobal should be forwarded");
    require(oc4::coupling::nearlyEqual(state.eulerAnglesRad, Vector3{0.1, -0.2, 0.3}, 1e-12),
            "Euler angles should be recovered from the orientation matrix");

    const Vector3 reconstructedOmega{
        state.eulerAngleRatesRadPerSec.x
            + state.eulerAngleRatesRadPerSec.z * std::sin(state.eulerAnglesRad.y),
        state.eulerAngleRatesRadPerSec.y * std::cos(state.eulerAnglesRad.x)
            - state.eulerAngleRatesRadPerSec.z * std::sin(state.eulerAnglesRad.x) * std::cos(state.eulerAnglesRad.y),
        state.eulerAngleRatesRadPerSec.y * std::sin(state.eulerAnglesRad.x)
            + state.eulerAngleRatesRadPerSec.z * std::cos(state.eulerAnglesRad.x) * std::cos(state.eulerAnglesRad.y),
    };
    require(oc4::coupling::nearlyEqual(reconstructedOmega, snapshot.angularVelocityGlobal, 1e-12),
            "Euler-angle rates should be consistent with the angular velocity");
}

void testAngularAccelerationFiniteDifference()
{
    const RigidBodyMotionSnapshot snapshot = makeSnapshot(2.0, 0.25, {1.0, 2.0, 3.0});
    const PlatformState state =
        MoorDynRestraintBridge::buildPlatformState(snapshot, {0.5, 1.0, 2.0}, true);

    require(oc4::coupling::nearlyEqual(state.angularAccelerationGlobal, Vector3{2.0, 4.0, 4.0}, 1e-12),
            "angular acceleration should be computed from finite differences");
}

void testBridgeLifecycleAndDelegation()
{
    auto provider = std::make_unique<FakeLoadProvider>();
    FakeLoadProvider* rawProvider = provider.get();
    rawProvider->nextLoad.force = {11.0, 12.0, 13.0};
    rawProvider->nextLoad.applicationPoint = {{1.0, 2.0, 3.0}, FrameId::global, "platformRef"};
    rawProvider->nextLoad.expressedInFrame = FrameId::global;

    MoorDynRestraintBridge bridge(std::move(provider));
    const RigidBodyMotionSnapshot snapshot0 = makeSnapshot(0.5, 0.1, {0.0, 0.0, 0.0});
    const RigidBodyMotionSnapshot snapshot1 = makeSnapshot(0.6, 0.1, {0.1, 0.2, 0.3});

    bridge.evaluate(snapshot0);
    bridge.evaluate(snapshot1);

    require(rawProvider->initializeCalled, "bridge should initialize the provider on first use");
    require(rawProvider->evaluateCount == 2, "bridge should delegate both evaluate calls");
    require(oc4::coupling::nearlyEqual(
                rawProvider->evaluatedState.angularAccelerationGlobal,
                Vector3{1.0, 2.0, 3.0},
                1e-12),
            "bridge should pass finite-difference angular acceleration on later steps");

    bridge.close();
    require(rawProvider->closeCalled, "bridge close should close the provider");
}

void testRejectsInvalidOrientation()
{
    RigidBodyMotionSnapshot snapshot = makeSnapshot(1.0, 0.1, {0.0, 0.0, 0.0});
    snapshot.orientationGlobalFromBody(0, 0) = 2.0;

    bool threw = false;
    try {
        MoorDynRestraintBridge::buildPlatformState(snapshot, Vector3{}, false);
    } catch (const std::runtime_error&) {
        threw = true;
    }

    require(threw, "bridge should reject invalid orientation matrices");
}

}  // namespace

int main()
{
    testPlatformStateExtraction();
    testAngularAccelerationFiniteDifference();
    testBridgeLifecycleAndDelegation();
    testRejectsInvalidOrientation();

    std::cout << "All MoorDyn restraint-bridge tests passed.\n";
    return 0;
}
